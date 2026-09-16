// Run with node test/test_pattern_previews_ui.cjs. No hardware needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../lib/WebServer/src/WebUI.h'), 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');

const observers = [];
const requests = [];
const timers = new Map();
const revoked = [];
let timerId = 0;
let objectId = 0;
const container = { innerHTML: '', querySelectorAll: () => [], removeEventListener() {} };
const context = vm.createContext({
    AbortController,
    document: { getElementById: () => container },
    IntersectionObserver: class {
        constructor(callback, options) { this.callback = callback; this.options = options; this.targets = []; observers.push(this); }
        observe(target) { this.targets.push(target); }
        disconnect() { this.disconnected = true; }
    },
    setTimeout: callback => { timers.set(++timerId, callback); return timerId; },
    clearTimeout: id => timers.delete(id),
    URL: { createObjectURL: () => `blob:${++objectId}`, revokeObjectURL: url => revoked.push(url) },
    fetch: (url, options) => new Promise((resolve, reject) => {
        options.signal.addEventListener('abort', () => reject(new Error('Timed out')));
        requests.push({ url, options, resolve, reject });
    })
});
vm.runInContext(script, context);
const controller = Object.create(context.Controller.prototype);
const flush = async () => { for (let i = 0; i < 12; ++i) await Promise.resolve(); };
function image(name) {
    const img = { dataset: { previewUrl: `/api/pattern/image?file=${name}` }, isConnected: true,
        hidden: true, previousElementSibling: { textContent: 'Preview', hidden: false } };
    img.parentElement = { querySelector: () => img };
    return img;
}
const visible = (observer, img, value = true) => observer.callback([{ target: img.parentElement, isIntersecting: value }]);
const ok = { ok: true, blob: async () => ({}) };

(async () => {
    controller.storageAvailable = true;
    controller.selectedPattern = 'spiral.thr';
    controller.files = [
        { name: 'spiral.thr', hasImage: true, imageTime: 123, size: 8192 },
        { name: 'missing.thr', hasImage: false, size: 1024 }
    ];
    controller.escapeHtml = value => value;
    controller.renderPatternList();
    assert.match(container.innerHTML, /<button type="button"[^>]*aria-pressed="true"/);
    assert.equal((container.innerHTML.match(/data-preview-url=/g) || []).length, 1);
    assert.match(container.innerHTML, /file=spiral.thr&t=123/);
    assert.doesNotMatch(container.innerHTML, / KB|pattern-size|<img[^>]*\ssrc=/);
    assert.match(container.innerHTML, /No preview/);
    assert.equal(requests.length, 0, 'rendering must not issue image requests');
    console.log('PASS: accessible rows, no file sizes, no requests for missing images');

    const [first, second, third] = ['one', 'two', 'three'].map(image);
    container.querySelectorAll = () => [first, second, third];
    controller.observePatternThumbnails(container);
    const observer = observers.at(-1);
    assert.equal(observer.options.root, container);
    assert.equal(observer.targets[0], first.parentElement, 'observe the frame, not the hidden image');
    assert.equal(requests.length, 0);
    visible(observer, first);
    visible(observer, second);
    visible(observer, third);
    assert.equal(requests.length, 1, 'only one image request can run');
    assert.equal(requests[0].options.cache, 'default');
    visible(observer, second, false);
    requests[0].resolve(ok);
    await flush();
    assert.equal(requests.length, 2);
    assert.equal(requests[1].url, third.dataset.previewUrl, 'skip rows scrolled offscreen while queued');
    first.onload();
    assert.equal(first.hidden, false);
    assert.equal(first.previousElementSibling.hidden, true);
    assert.deepEqual(revoked, ['blob:1']);
    requests[1].resolve({ ok: false });
    await flush();
    assert.equal(third.previousElementSibling.textContent, 'No preview');
    assert.equal(third.hidden, true);
    visible(observer, second);
    assert.equal(requests.length, 3, 'offscreen skipped rows can load when visible again');
    console.log('PASS: viewport-only serial loading, image cleanup, graceful HTTP failures');

    // A file-list refresh must not create a second loader beside an in-flight one.
    second.isConnected = false;
    container.querySelectorAll = () => [];
    controller.renderPatternList();
    assert.equal(observer.disconnected, true);
    const replacement = image('replacement');
    container.querySelectorAll = () => [replacement];
    controller.observePatternThumbnails(container);
    visible(observers.at(-1), replacement);
    assert.equal(requests.length, 3);
    requests[2].resolve(ok);
    await flush();
    assert.equal(requests.length, 4);
    assert.equal(second.src, undefined, 'detached rows must not acquire object URLs');
    // A stuck chip request times out and frees the loader for the next visible row.
    const afterTimeout = image('after-timeout');
    controller.visibleThumbnails.add(afterTimeout);
    controller.thumbnailQueue.push(afterTimeout);
    assert.equal(timers.size, 1);
    [...timers.values()][0]();
    await flush();
    assert.equal(requests.length, 5);
    assert.equal(replacement.previousElementSibling.textContent, 'No preview');
    requests[4].resolve(ok);
    await flush();
    afterTimeout.onerror();
    assert.equal(afterTimeout.hidden, true);
    assert.equal(timers.size, 0);
    assert.equal(controller.thumbnailLoading, false);
    assert.deepEqual(revoked, ['blob:1', 'blob:2']);
    console.log('PASS: refresh preserves concurrency limit; timeouts allow subsequent previews');

    const overlay = { style: {}, src: '' };
    context.document.getElementById = id => id === 'pattern-overlay' ? overlay : container;
    const overlays = Object.create(context.Controller.prototype);
    Object.assign(overlays, { overlayRequestId: 0, fileHasImage: { 'one.thr': true, 'two.thr': true },
        fileImageTimes: {}, fileTimes: {} });
    const pendingOverlays = [];
    overlays.fetchPatternImageUrl = url => new Promise(resolve => pendingOverlays.push({ url, resolve }));
    const firstOverlay = overlays.setOverlayImage('one.thr');
    await overlays.setOverlayImage('None');
    pendingOverlays[0].resolve({ src: 'blob:stopped', revoke: true });
    await firstOverlay;
    assert.equal(overlay.src, '');
    assert.equal(overlay.style.display, 'none');
    assert.ok(revoked.includes('blob:stopped'));

    const older = overlays.setOverlayImage('one.thr');
    const newer = overlays.setOverlayImage('two.thr');
    pendingOverlays[2].resolve({ src: 'blob:new', revoke: true });
    await newer;
    overlay.onload();
    pendingOverlays[1].resolve({ src: 'blob:old', revoke: true });
    await older;
    assert.equal(overlay.src, 'blob:new');
    assert.equal(overlay.style.display, 'block');
    assert.ok(revoked.includes('blob:old'));
    assert.ok(revoked.includes('blob:new'));
    assert.equal(overlays.overlayObjectUrlIsTemp, false);
    const failed = overlays.setOverlayImage('one.thr');
    pendingOverlays[3].resolve({ src: 'blob:decode-error', revoke: true });
    await failed;
    overlay.onerror();
    assert.equal(overlay.style.display, 'none');
    assert.ok(revoked.includes('blob:decode-error'));
    console.log('PASS: stopped/superseded overlays stay hidden and release image URLs');

    overlays.fileHasImage = {};
    await overlays.setOverlayImage('early.thr');
    assert.equal(pendingOverlays.length, 4, 'missing library metadata must not trigger a fetch');
    overlays.renderPatternList = overlays.updateStorageControls = () => {};
    overlays.requestJSON = async () => ({ revision: 1, files: [
        { name: 'early.thr', time: 111, imageTime: 222, hasImage: true }
    ] });
    await overlays.loadFileList();
    assert.equal(pendingOverlays.length, 5, 'library completion retries the current overlay');
    assert.equal(pendingOverlays[4].url, '/api/pattern/image?file=early.thr&t=222');
    pendingOverlays[4].resolve({ src: 'blob:early', revoke: true });
    await flush();
    overlay.onload();
    assert.equal(overlay.src, 'blob:early');
    assert.equal(overlay.style.display, 'block');
    console.log('PASS: status arriving before the file library still displays its pattern overlay');
})().catch(error => { console.error(error); process.exitCode = 1; });
