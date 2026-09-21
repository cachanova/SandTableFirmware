// Exercise the real page controller with delayed/reordered network replies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');
const script = fs.readFileSync('lib/WebServer/src/WebUI.h', 'utf8')
    .split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
const flush = async () => { for (let i = 0; i < 20; ++i) await Promise.resolve(); };
const ok = () => ({ok: true, json: async () => ({success: true})});

function page() {
    let now = 0, nextTimer = 0;
    const nodes = {}, timers = new Map(), requests = [];
    const context = vm.createContext({
        document: {getElementById: id => nodes[id] ||= {
            value: 0, textContent: '', listeners: {},
            setAttribute(name, value) { this[name] = value; },
            setPointerCapture() {},
            addEventListener(event, callback) { this.listeners[event] = callback; }
        }},
        Date: {now: () => now}, FormData, AbortController, console,
        fetch: (url, options) => new Promise((resolve, reject) => {
            requests.push({url, options, resolve, reject});
            options.signal.addEventListener('abort', () => reject(new Error('timeout')));
        }),
        setTimeout: (fn, delay) => { timers.set(++nextTimer, {fn, delay}); return nextTimer; },
        clearTimeout: id => timers.delete(id)
    });
    vm.runInContext(script, context);
    const controller = Object.create(context.Controller.prototype);
    controller.apiBase = '/api';
    function fire(delay) {
        const entry = [...timers].find(([, timer]) => timer.delay === delay);
        assert.ok(entry, `Missing ${delay}ms timer`);
        timers.delete(entry[0]);
        now += delay;
        entry[1].fn();
    }
    return {controller, nodes, requests, timers, fire, advance: ms => { now += ms; }};
}

test('rapid toggles serialize writes and retain the final selection', async () => {
    const h = page();
    h.controller.setBrightness(100);
    h.controller.setBrightness(0);
    h.controller.setBrightness(100);
    h.controller.setBrightness(0);
    assert.equal(h.requests.length, 1);
    h.controller.syncBrightnessControl({ledTargetBrightness: 100});
    assert.equal(h.nodes['light-toggle'].checked, false);
    h.requests[0].resolve(ok()); await flush();
    assert.equal(h.requests.length, 2);
    assert.equal(h.requests[1].options.body.get('brightness'), '0');
    h.requests[1].resolve(ok()); await flush();
    assert.equal(h.controller.brightnessInFlight, false);
    assert.equal(h.timers.size, 0);
});

test('only Off and On are accepted; duplicate selections do not write', async () => {
    const h = page();
    for (let value = 1; value < 100; ++value) h.controller.setBrightness(value);
    assert.equal(h.requests.length, 0);
    h.controller.setBrightness(100);
    h.requests[0].resolve(ok()); await flush();
    h.controller.setBrightness(100);
    assert.equal(h.requests.length, 1);
});

test('a lost acknowledgement drops pending toggles and permits explicit retry', async () => {
    const h = page();
    h.controller.setBrightness(100);
    h.controller.setBrightness(0);
    h.fire(2500); await flush();
    assert.equal(h.controller.brightnessInFlight, false);
    assert.equal(h.controller.brightnessPending, undefined);
    assert.equal(h.requests.length, 1);
    assert.match(h.nodes['brightness-message'].textContent, /unconfirmed/);
    h.controller.setBrightness(0);
    assert.equal(h.requests.length, 2);
    h.requests[1].resolve(ok()); await flush();
});

test('deadline also covers an incomplete acknowledgement body', async () => {
    const h = page();
    h.controller.setBrightness(0);
    const request = h.requests[0];
    request.resolve({ok: true, json: () => new Promise((resolve, reject) => {
        request.options.signal.addEventListener('abort', () => reject(new Error('body timeout')));
    })});
    await flush(); h.fire(2500); await flush();
    assert.match(h.nodes['brightness-message'].textContent, /unconfirmed/);
    assert.equal(h.controller.brightnessInFlight, false);
});

test('server rejection is visible', async () => {
    const h = page();
    h.controller.setBrightness(100);
    h.requests[0].resolve({ok: false, status: 503,
        json: async () => ({success: false, message: 'Light output could not be changed'})});
    await flush();
    assert.match(h.nodes['brightness-message'].textContent, /could not be changed/);
    assert.equal(h.controller.brightnessFailed, true);
});

test('stale status replies cannot undo a toggle', async () => {
    const h = page();
    h.controller.updateUI = (status, revision) => h.controller.syncBrightnessControl(status, revision);
    const before = h.controller.pollStatusOnce();
    h.controller.setBrightness(100);
    h.requests[0].resolve({ok: true, json: async () => ({ledTargetBrightness: 0})});
    await before;
    assert.equal(h.nodes['light-toggle'].checked, true);
    const during = h.controller.pollStatusOnce();
    h.requests[1].resolve(ok()); await flush();
    h.requests[2].resolve({ok: true, json: async () => ({ledTargetBrightness: 0})});
    await during;
    assert.equal(h.nodes['light-toggle'].checked, true);
    h.controller.syncBrightnessControl({ledTargetBrightness: 0});
    assert.equal(h.nodes['light-toggle'].checked, false);
});

test('checkbox changes issue binary requests and intermediate status is ignored', async () => {
    const h = page();
    h.controller.setupEventListeners();
    const toggle = h.nodes['light-toggle'];
    toggle.listeners.change({target: {checked: true}});
    assert.equal(h.requests[0].options.body.get('brightness'), '100');
    h.requests[0].resolve(ok()); await flush();
    h.controller.syncBrightnessControl({ledTargetBrightness: 45});
    assert.equal(toggle.checked, true);
    toggle.listeners.change({target: {checked: false}});
    assert.equal(h.requests[1].options.body.get('brightness'), '0');
    h.requests[1].resolve(ok()); await flush();
    assert.equal(h.nodes['brightness-value'].textContent, 'Off');
});
