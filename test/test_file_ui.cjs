// node test/test_file_ui.cjs: Files page rows carry a thumbnail, date, Rename and Delete.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('lib/WebServer/src/FileUI.h', 'utf8');
const script = source.split('<script>')[1].split('</script>')[0];

function element(tag) {
    return {
        tagName: tag, children: [], style: {}, listeners: {}, className: '', textContent: '',
        innerHTML: '', classList: { add() {}, remove() {} },
        appendChild(child) { this.children.push(child); return child; },
        addEventListener(type, fn) { this.listeners[type] = fn; },
        querySelector() { return element('span'); },
    };
}
const elements = {};
const byId = id => (elements[id] ||= element('div'));
const requests = [];
let prompted = null;
const files = [
    { name: 'Newest.thr', size: 2048, time: 1790000000, hasImage: true, thumbnailTime: 7 },
    { name: 'Old upload.thr', size: 1024, time: 315533724, hasImage: false },
];
const context = vm.createContext({
    document: { getElementById: byId, createElement: element },
    window: {},
    URL: { createObjectURL: () => 'blob:x', revokeObjectURL() {} },
    FormData: class { constructor() { this.fields = {}; } append(k, v) { this.fields[k] = v; } },
    AbortController: class { constructor() { this.signal = {}; } abort() {} },
    setTimeout: () => 0, clearTimeout() {},
    console,
    uiPrompt: async (message, options) => { prompted = options; return 'Renamed '; },
    uiNotify: message => { throw new Error('unexpected notice: ' + message); },
    fetch: async (url, options = {}) => {
        requests.push({ url, body: options.body && options.body.fields });
        if (url === '/api/files') return { ok: true, json: async () => ({ files }) };
        if (url.startsWith('/api/pattern/image')) return { ok: true, status: 200, blob: async () => ({}) };
        return { ok: true, status: 200, json: async () => ({ success: true }) };
    },
});
vm.runInContext(script, context);
const flush = async () => { for (let i = 0; i < 20; ++i) await new Promise(r => setImmediate(r)); };

(async () => {
    context.window.onload();
    await flush();
    assert.deepEqual(requests[0], { url: '/api/system/time', body: { epoch: requests[0].body.epoch } });
    assert.ok(Math.abs(Number(requests[0].body.epoch) - Date.now() / 1000) < 5);
    console.log('PASS: the page lends the board its clock on load');

    const rows = byId('file-list').children;
    assert.equal(rows.length, 2);
    const [thumb, info, actions] = rows[0].children;
    assert.equal(info.children[0].textContent, 'Newest');
    assert.match(info.children[1].textContent, /^2\.0 KB · added /);
    assert.equal(rows[1].children[1].children[1].textContent, '1.0 KB', '1980 stamps show no date');
    assert.deepEqual(actions.children.map(b => b.textContent), ['Rename', 'Delete']);
    assert.equal(thumb.children[0].tagName, 'img');
    assert.ok(requests.some(r => r.url === '/api/pattern/image?file=Newest.thr&thumbnail=1&t=7'));
    assert.equal(rows[1].children[0].textContent, 'None');
    console.log('PASS: rows show thumbnail, size and date added, Rename and Delete');

    requests.length = 0;
    actions.children[0].listeners.click();
    await flush();
    assert.equal(prompted.value, 'Newest');
    assert.deepEqual(requests[0], { url: '/api/files/rename', body: { file: 'Newest.thr', name: 'Renamed' } });
    assert.equal(requests[1].url, '/api/files', 'the list reloads after renaming');
    console.log('PASS: Rename prompts with the current name and posts the trimmed new one');
})().catch(error => { console.error(error); process.exitCode = 1; });
