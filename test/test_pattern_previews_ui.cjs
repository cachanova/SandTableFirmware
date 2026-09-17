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
        { name: 'spiral.thr', hasImage: true, imageTime: 123, thumbnailTime: 456, size: 8192 },
        { name: 'missing.thr', hasImage: false, size: 1024 }
    ];
    controller.escapeHtml = value => value;
    controller.renderPatternList();
    assert.match(container.innerHTML, /<button type="button"[^>]*aria-pressed="true"/);
    assert.equal((container.innerHTML.match(/data-preview-url=/g) || []).length, 1);
    assert.match(container.innerHTML, /file=spiral.thr&thumbnail=1&t=456/);
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

    const shared = Object.create(context.Controller.prototype);
    const deferredThumb = image('deferred-thumb'), nextThumb = image('next-thumb');
    Object.assign(shared, { overlayRequestId: 0, fileHasImage: { 'one.thr': true, 'two.thr': true },
        fileImageTimes: {}, fileTimes: {}, thumbnailQueue: [deferredThumb, nextThumb],
        visibleThumbnails: new Set([deferredThumb, nextThumb]) });
    shared.loadNextPatternThumbnail();
    const thumbRequest = requests.at(-1), beforeOverlay = requests.length;
    const firstPriority = shared.setOverlayImage('one.thr');
    assert.equal(thumbRequest.options.signal.aborted, true);
    assert.equal(requests.length, beforeOverlay, 'overlay waits for thumbnail abort to settle');
    await flush();
    const firstPriorityRequest = requests.at(-1);
    assert.match(firstPriorityRequest.url, /file=one.thr/);
    assert.equal(deferredThumb.dataset.previewState, 'queued');
    assert.equal(deferredThumb.previousElementSibling.textContent, 'Preview');
    assert.equal(shared.thumbnailLoading, false);
    const secondPriority = shared.setOverlayImage('two.thr');
    assert.equal(firstPriorityRequest.options.signal.aborted, true);
    await flush();
    const secondPriorityRequest = requests.at(-1);
    assert.match(secondPriorityRequest.url, /file=two.thr/);
    assert.equal(shared.overlayLoading, true);
    await shared.setOverlayImage('None');
    assert.equal(secondPriorityRequest.options.signal.aborted, true);
    await Promise.all([firstPriority, secondPriority]);
    await flush();
    assert.equal(requests.at(-1).url, deferredThumb.dataset.previewUrl, 'canceled overlay resumes deferred preview first');
    assert.equal(shared.overlayLoading, false);
    requests.at(-1).resolve(ok); await flush(); deferredThumb.onload();
    assert.equal(requests.at(-1).url, nextThumb.dataset.previewUrl);
    requests.at(-1).resolve(ok); await flush(); nextThumb.onload();
    console.log('PASS: overlays preempt and defer thumbnails; supersession and stop cancel work and resume previews');

    const successThumb = image('after-success');
    shared.thumbnailQueue.push(successThumb); shared.visibleThumbnails.add(successThumb);
    const successOverlay = shared.setOverlayImage('one.thr');
    const successRequest = requests.at(-1);
    await shared.setOverlayImage('one.thr');
    assert.equal(successRequest.options.signal.aborted, false, 'same-version library refresh reuses the active overlay');
    shared.loadNextPatternThumbnail();
    assert.equal(requests.at(-1), successRequest, 'thumbnails stay paused while overlay fetch is active');
    successRequest.resolve(ok); await successOverlay;
    assert.equal(requests.at(-1).url, successThumb.dataset.previewUrl);
    overlay.onload(); requests.at(-1).resolve(ok); await flush(); successThumb.onload();
    const afterSuccessCount = requests.length;
    await shared.setOverlayImage('one.thr');
    assert.equal(requests.length, afterSuccessCount, 'already visible same-version overlay is not fetched again');
    const retryOverlay = shared.setOverlayImage('two.thr');
    requests.at(-1).resolve({ ok: false, status: 503, headers: { get: () => '5' } });
    await flush();
    const countBeforeCancel = requests.length;
    await shared.setOverlayImage('None');
    await retryOverlay;
    assert.equal(requests.length, countBeforeCancel, 'cancel interrupts Retry-After without another request');
    assert.equal(timers.size, 0);
    assert.equal(shared.overlayLoading, false);
    console.log('PASS: successful overlays resume previews; canceled retry waits release all timers');

    const busy = { ok: false, status: 503, headers: { get: () => '1' } };
    const retryThumb = image('busy-preview'), afterBusy = image('after-busy');
    shared.thumbnailQueue.push(retryThumb, afterBusy);
    shared.visibleThumbnails.add(retryThumb); shared.visibleThumbnails.add(afterBusy);
    const beforeBusy = requests.length;
    shared.loadNextPatternThumbnail();
    for (let attempt = 0; attempt < 3; attempt++) {
        requests.at(-1).resolve(busy); await flush();
        if (attempt < 2) {
            assert.equal(requests.length, beforeBusy + attempt + 1, 'busy retry waits instead of spinning');
            assert.equal(timers.size, 2, 'one request deadline plus one retry delay');
            [...timers.values()].at(-1)(); await flush();
        }
    }
    assert.equal(requests.filter(r => r.url === retryThumb.dataset.previewUrl).length, 3);
    assert.equal(retryThumb.dataset.previewState, 'unavailable');
    assert.equal(requests.at(-1).url, afterBusy.dataset.previewUrl, 'exhausted retries let next preview proceed');
    requests.at(-1).resolve(ok); await flush(); afterBusy.onload();
    assert.equal(timers.size, 0);
    console.log('PASS: explicit busy responses get at most two delayed retries and then advance the queue');

    const scrollAway = image('scroll-away');
    shared.thumbnailQueue.push(scrollAway); shared.visibleThumbnails.add(scrollAway);
    shared.loadNextPatternThumbnail(); requests.at(-1).resolve(busy); await flush();
    shared.visibleThumbnails.delete(scrollAway);
    const beforeScrollAway = requests.length;
    [...timers.values()].at(-1)(); await flush();
    assert.equal(requests.length, beforeScrollAway, 'offscreen retry is not sent');
    assert.equal(scrollAway.dataset.previewState, undefined);
    assert.equal(scrollAway.dataset.previewRetries, '1', 'visibility changes do not reset retry budget');
    assert.equal(timers.size, 0);
    console.log('PASS: scrolling offscreen cancels delayed retries without resetting their budget');

    const busyDeferred = image('busy-deferred');
    shared.thumbnailQueue.push(busyDeferred); shared.visibleThumbnails.add(busyDeferred);
    shared.loadNextPatternThumbnail(); requests.at(-1).resolve(busy); await flush();
    const prioritised = shared.setOverlayImage('one.thr');
    await flush();
    assert.match(requests.at(-1).url, /file=one.thr/);
    assert.equal(busyDeferred.dataset.previewState, 'queued');
    assert.equal(timers.size, 1, 'overlay preemption cancels thumbnail retry timer');
    requests.at(-1).resolve(ok); await prioritised; overlay.onload();
    assert.equal(requests.at(-1).url, busyDeferred.dataset.previewUrl);
    requests.at(-1).resolve(ok); await flush(); busyDeferred.onload();
    assert.equal(busyDeferred.dataset.previewState, 'loaded');
    assert.equal(timers.size, 0);
    console.log('PASS: overlays interrupt busy retry waits, and deferred previews resume successfully');

    const retired = image('retired-generation');
    shared.thumbnailQueue.push(retired); shared.visibleThumbnails.add(retired);
    shared.loadNextPatternThumbnail();
    const retiredRequest = requests.at(-1);
    retiredRequest.resolve(busy); await flush();
    retired.isConnected = false; shared.storageAvailable = false;
    const beforeRetirement = requests.length;
    shared.renderPatternList(); await flush();
    assert.equal(retiredRequest.options.signal.aborted, true);
    assert.equal(timers.size, 0, 'rerender cancels both deadline and retry timer');
    assert.equal(requests.length, beforeRetirement, 'old generation cannot retry after rerender');
    console.log('PASS: replacing the pattern list cleans up pending retry work');
})().catch(error => { console.error(error); process.exitCode = 1; });
