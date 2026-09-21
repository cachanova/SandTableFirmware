// node test/test_thumbnail_upload_ui.cjs: both upload entry points generate small previews.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

async function check(page) {
    const html = fs.readFileSync(`lib/WebServer/src/${page}`, 'utf8');
    const script = html.split('<script>')[1].split('</script>')[0]
        .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
    const requests = [], notices = [], warnings = [], draws = [], timers = new Set();
    let closed = 0, failThumbnail = false, failDecode = false;
    const elements = {};
    const canvas = { getContext: () => ({ drawImage: (...args) => draws.push(args) }),
        toBlob: callback => callback({ thumbnail: true }) };
    const bitmap = { width: 400, height: 800, close: () => closed++ };
    const context = vm.createContext({
        document: { createElement: () => canvas, getElementById: id => elements[id] ||= {
            addEventListener() {}, classList: {add() {}, remove() {}}, querySelector: () => ({})
        } },
        window: {}, AbortController,
        setTimeout: fn => { timers.add(fn); return fn; }, clearTimeout: fn => timers.delete(fn),
        uiNotify: message => notices.push(message),
        console: {warn: (...args) => warnings.push(args)},
        createImageBitmap: async () => { if (failDecode) throw new Error('Not an image'); return bitmap; },
        FormData: class { constructor() { this.fields=[]; } append(...args) { this.fields.push(args); } },
        fetch: async (url, options) => {
            requests.push({url, options});
            return {ok: !(failThumbnail && url.includes('thumbnail=1')), json: async () => ({})};
        }
    });
    vm.runInContext(script, context);
    let upload;
    if (page === 'WebUI.h') {
        const controller = Object.create(context.Controller.prototype);
        Object.assign(controller, {apiBase:'/api', storageAvailable:true, loadFileList:async()=>{}});
        upload = (...args) => controller.performUpload(...args);
    } else {
        context.loadFileList = async () => {};
        upload = (...args) => context.performUpload(...args);
    }
    const pattern = {name:'x'.repeat(80)+'.thr'}, original = {name:'photo.png'};
    await upload(pattern, original);
    assert.deepEqual(requests.map(r=>r.url), ['/api/files/upload','/api/files/upload','/api/files/upload?thumbnail=1']);
    assert.equal(requests[1].options.body.fields[0][1], original, 'preserve the full original image');
    assert.equal(requests[2].options.body.fields[0][2], 'x'.repeat(80)+'.png', 'thumbnail uses the unchanged legal base name');
    assert.equal(canvas.width, 128); assert.equal(canvas.height, 128);
    assert.deepEqual(draws[0].slice(1), [32,0,64,128], 'contain nonsquare images without distortion');
    assert.equal(closed, 1); assert.equal(timers.size, 0);
    failThumbnail = true; requests.length = 0;
    await upload(pattern, original);
    assert.equal(requests.length, 3);
    assert.equal(warnings.length, 1);
    assert.ok(notices.every(message => !message.startsWith('Upload failed')));
    assert.equal(closed, 2); assert.equal(timers.size, 0);
    failDecode = true; requests.length = 0;
    await upload(pattern, original);
    assert.equal(requests.length, 2, 'decode failure retains successful original upload');
    assert.equal(warnings.length, 2);
    console.log(`PASS: ${page} preserves originals, uploads 128px previews, handles failures, and releases decoder resources`);
}
(async()=>{ await check('WebUI.h'); await check('FileUI.h'); })()
    .catch(error=>{console.error(error);process.exitCode=1;});
