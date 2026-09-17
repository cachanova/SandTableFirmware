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
            value: 50, textContent: '', listeners: {},
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

test('rapid edits keep one request in flight and send only the final pending value', async () => {
    const h = page();
    h.controller.setBrightness(20);
    for (let value = 21; value <= 80; ++value) h.controller.setBrightness(value);
    assert.equal(h.requests.length, 1);
    h.controller.syncBrightnessControl({ledTargetBrightness: 50});
    assert.equal(h.nodes['brightness-value'].textContent, '80%');
    h.advance(500);
    h.requests[0].resolve(ok()); await flush();
    assert.equal(h.requests.length, 2);
    assert.equal(h.requests[1].options.body.get('brightness'), '80');
    assert.equal(h.nodes['brightness-message'].textContent, '');
    h.requests[1].resolve(ok()); await flush();
    assert.equal(h.controller.brightnessInFlight, false);
    assert.equal(h.nodes['brightness-message'].textContent, '');
    assert.equal(h.timers.size, 0);
});

test('fast responses are rate limited and input/change duplicates are suppressed', async () => {
    const h = page();
    h.controller.setBrightness(60);
    h.requests[0].resolve(ok()); await flush();
    h.controller.setBrightness(60);
    assert.equal(h.requests.length, 1);
    h.controller.setBrightness(70);
    h.controller.setBrightness(90);
    assert.equal(h.requests.length, 1);
    h.fire(100);
    assert.equal(h.requests.length, 2);
    assert.equal(h.requests[1].options.body.get('brightness'), '90');
    h.requests[1].resolve(ok()); await flush();
});

test('a lost acknowledgement expires, discards the backlog and permits explicit retry', async () => {
    const h = page();
    h.controller.setBrightness(60);
    h.controller.setBrightness(80);
    h.fire(2500); await flush();
    assert.equal(h.controller.brightnessInFlight, false);
    assert.equal(h.controller.brightnessPending, undefined);
    assert.equal(h.requests.length, 1);
    assert.equal(h.timers.size, 0);
    assert.match(h.nodes['brightness-message'].textContent, /unconfirmed/);
    h.controller.setBrightness(80);
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

test('server rejection is visible and does not claim the light changed', async () => {
    const h = page();
    h.controller.setBrightness(40);
    h.requests[0].resolve({ok: false, status: 503,
        json: async () => ({success: false, message: 'LED PWM update failed'})});
    await flush();
    assert.match(h.nodes['brightness-message'].textContent, /LED PWM update failed/);
    assert.equal(h.controller.brightnessFailed, true);
});

test('status replies begun before or during a write cannot restore old brightness', async () => {
    const h = page();
    h.controller.updateUI = (status, revision) => h.controller.syncBrightnessControl(status, revision);
    const before = h.controller.pollStatusOnce();
    h.controller.setBrightness(75);
    h.requests[0].resolve({ok: true, json: async () => ({ledTargetBrightness: 50})});
    await before;
    assert.equal(h.nodes['brightness-value'].textContent, '75%');
    const during = h.controller.pollStatusOnce();
    h.requests[1].resolve(ok()); await flush();
    h.requests[2].resolve({ok: true, json: async () => ({ledTargetBrightness: 50})});
    await during;
    assert.equal(h.nodes['brightness-value'].textContent, '75%');
    h.controller.syncBrightnessControl({ledTargetBrightness: 75});
    assert.equal(h.nodes['brightness-slider'].value, 75);
});

test('a later external change permits selecting the previously sent value again', async () => {
    const h = page();
    h.controller.setBrightness(100);
    h.requests[0].resolve(ok()); await flush();
    h.controller.syncBrightnessControl({ledTargetBrightness: 30});
    h.advance(100);
    h.controller.setBrightness(100);
    assert.equal(h.requests.length, 2);
    h.requests[1].resolve(ok()); await flush();
});

test('slider input sends live updates and polling never moves it during a drag', async () => {
    const h = page();
    h.controller.setupEventListeners();
    const slider = h.nodes['brightness-slider'];
    slider.listeners.pointerdown({pointerId: 1});
    h.controller.syncBrightnessControl({ledTargetBrightness: 10});
    assert.equal(slider.value, 50);
    slider.listeners.input({target: {value: '0'}});
    assert.equal(h.requests[0].options.body.get('brightness'), '0');
    slider.listeners.pointerup();
    slider.listeners.change({target: {value: '0'}});
    assert.equal(h.requests.length, 1);
    h.requests[0].resolve(ok()); await flush();
});
