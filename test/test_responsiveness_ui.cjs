// node test/test_responsiveness_ui.cjs: exercise slow/offline chip behavior.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('lib/WebServer/src/WebUI.h', 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
const deferred = () => { let resolve; const promise = new Promise(r => resolve = r); return {promise, resolve}; };
function setup(fetch) {
    const elements = {}, timers = new Map(); let id = 0;
    const context = vm.createContext({
        document: {hidden: false, getElementById: key => elements[key] ||= {}},
        fetch, AbortController, FormData, console, alert: () => {},
        setTimeout: (fn, delay) => { timers.set(++id, {fn, delay}); return id; },
        clearTimeout: id => timers.delete(id)
    });
    vm.runInContext(script, context);
    const controller = Object.create(context.Controller.prototype);
    Object.assign(controller, {apiBase: '/api', files: [], fileTimes: {}, fileHasImage: {}});
    return {controller, elements, timers};
}
const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
(async () => {
    {
        let signal;
        const h = setup(async (url, opts) => {
            signal = opts.signal;
            return {ok: true, json: () => new Promise((resolve, reject) => {
                signal.addEventListener('abort', () => reject(new Error('body timeout')));
            })};
        });
        const result = h.controller.requestJSON('/status');
        await flush();
        [...h.timers.values()][0].fn();
        await assert.rejects(result, /body timeout/);
        assert.equal(signal.aborted, true);
        assert.equal(h.timers.size, 0);
        console.log('PASS: deadline covers response body and clears timer');
    }
    {
        const h = setup(); const pending = deferred(); let calls = 0, updates = 0;
        h.controller.getStatus = () => { calls++; return pending.promise; };
        h.controller.updateUI = () => updates++;
        h.controller.startStatusPolling();
        const first = h.controller.pollStatusOnce();
        const second = h.controller.pollStatusOnce();
        assert.equal(first, second);
        assert.equal(calls, 1);
        assert.equal(h.timers.size, 0, 'no next poll while previous is pending');
        pending.resolve({state: 'IDLE'}); await flush();
        assert.equal(updates, 1);
        assert.equal([...h.timers.values()][0].delay, 500);
        console.log('PASS: periodic and command polls share one request');
    }
    {
        const h = setup();
        h.controller.getStatus = async () => { throw new Error('offline'); };
        await assert.rejects(h.controller.pollStatusOnce(), /offline/);
        assert.equal(h.elements['state-badge'].textContent, 'CONNECTION LOST');
        assert.equal(h.elements['btn-start'].disabled, true);
        assert.equal(h.controller.statusRequest, null);
        console.log('PASS: lost connection replaces stale state and permits recovery');
    }
    {
        const h = setup(async () => ({ok: false, status: 503, json: async () => ({message: 'Indexing'})}));
        h.controller.files = [{name: 'keep.thr'}]; h.controller.selectedPattern = 'keep.thr';
        h.controller.renderPatternList = () => { throw new Error('must retain existing library'); };
        await h.controller.loadFileList();
        assert.equal(h.controller.files[0].name, 'keep.thr');
        assert.equal(h.controller.selectedPattern, 'keep.thr');
        assert.equal([...h.timers.values()][0].delay, 750);
        assert.equal(h.controller.fileListInFlight, false);
        console.log('PASS: indexing response preserves library and schedules retry');
    }
    {
        const h = setup(); const never = deferred(); let statusStarted = false;
        for (const method of ['setupCanvas', 'setupEventListeners', 'setupUploadHandlers', 'connectStream', 'startErrorPolling']) h.controller[method] = () => {};
        h.controller.startStatusPolling = () => { statusStarted = true; };
        h.controller.loadFileList = () => never.promise;
        h.controller.loadSystemInfo = h.controller.loadPlaylistStatus = async () => {};
        await h.controller.init();
        assert.equal(statusStarted, true);
        console.log('PASS: a stalled library does not prevent status or controls initializing');
    }
    {
        let requests = 0; const pending = deferred(); const h = setup();
        h.controller.selectedPattern = 'Spiral7.thr'; h.elements['clearing-select'] = {value: '0'};
        h.controller.requestJSON = async () => { requests++; await pending.promise; throw new Error('offline'); };
        h.controller.pollStatusOnce = async () => {};
        const start = h.controller.startPattern();
        await h.controller.startPattern();
        assert.equal(requests, 1);
        pending.resolve(); await start;
        assert.match(h.elements['playback-message'].textContent, /Start is unconfirmed/);
        assert.equal(h.controller.startInFlight, false);
        console.log('PASS: double click is deduplicated and lost acknowledgement is explicit');
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
