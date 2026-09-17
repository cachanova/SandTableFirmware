// Run with node test/test_homing_abort_ui.cjs. No browser or hardware needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
for (const name of ['WebUI.h', 'ManualUI.h', 'SettingsUI.h']) {
    const html = fs.readFileSync(path.join(__dirname, '../lib/WebServer/src', name), 'utf8');
    for (const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) new vm.Script(match[1]);
    assert.ok(html.includes('/home/abort'), `${name} must use the homing-aware abort route`);
}
const source = fs.readFileSync(path.join(__dirname, '../lib/WebServer/src/WebUI.h'), 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
assert.match(source, /id="btn-home-abort"[^>]*>Abort homing<\/button>/);
assert.match(source, /'btn-home-abort'\).addEventListener\('click', \(\) => this.abortHoming\(\)\)/);
assert.match(source.match(/<button[^>]*id="btn-home-abort"[^>]*>/)[0], /\bdisabled\b/);

function setup(fetchImpl, {route = '/api/home/abort', confirm} = {}) {
    const elements = {};
    let cleared = false;
    let requests = 0;
    const context = vm.createContext({
        document: {getElementById: id => elements[id] ||= {style: {}}},
        AbortController,
        setTimeout: fn => { context.expire = fn; return 1; },
        clearTimeout: () => { cleared = true; },
        fetch: (url, options) => {
            requests++;
            assert.equal(url, route);
            assert.equal(options.method, 'POST');
            return fetchImpl(context, options);
        },
        // Any confirmation dialog is a regression: abort must send first.
        confirm: confirm || (() => { throw new Error('Unexpected confirmation'); }),
        alert: message => { context.alertMessage = message; }
    });
    vm.runInContext(script, context);
    context.Controller.prototype.init = () => {};
    const controller = new context.Controller();
    controller.clearPath = () => {};
    controller.setOverlayImage = () => {};
    controller.pollStatusOnce = async () => { throw new Error('Offline status'); };
    context.controller = controller;
    context.elements = elements;
    return {controller, elements, wasCleared: () => cleared, requestCount: () => requests};
}

async function check(name, fetchImpl, expected, disabled) {
    const {controller, elements, wasCleared, requestCount} = setup(fetchImpl);
    controller.updateUI({state: 'HOMING'});
    await controller.abortHoming();
    assert.match(elements['home-abort-status'].textContent, expected);
    assert.equal(elements['btn-home-abort'].disabled, disabled);
    assert.equal(controller.abortInFlight, false);
    assert.equal(wasCleared(), true);
    assert.equal(requestCount(), 1);
    console.log('PASS:', name);
}

(async () => {
    {
        const h = setup(() => { throw new Error('Unexpected home request'); });
        await h.controller.homeDevice();
        for (const state of ['UNINITIALIZED', 'RUNNING', 'CLEARING', 'PAUSED',
            'STOPPING', 'PREPARING', 'HOMING', 'HOMING_REVIEW', 'UNKNOWN']) {
            h.controller.updateUI({state});
            assert.equal(h.elements['btn-home'].disabled, true, state);
            await h.controller.homeDevice();
        }
        for (const state of ['IDLE', 'INITIALIZED', 'HOMING_FAILED']) {
            h.controller.updateUI({state});
            assert.equal(h.elements['btn-home'].disabled, false, state);
            assert.equal(h.elements['btn-home'].textContent, state === 'IDLE' ? 'Re-home' : 'Home');
        }
        assert.equal(h.requestCount(), 0);
        console.log('PASS: re-home is available when idle and rejects busy states');
    }
    {
        const h = setup(() => { throw new Error('Unexpected home request'); }, {confirm: () => false});
        h.controller.updateUI({state: 'IDLE'});
        h.controller.clearPath = () => { throw new Error('Cancelled homing must preserve the trace'); };
        await h.controller.homeDevice();
        assert.equal(h.requestCount(), 0);
        assert.equal(h.elements['btn-home'].disabled, false);
        console.log('PASS: cancelling re-home preserves the trace');
    }
    {
        let cleared = 0;
        const h = setup(async context => {
            context.controller.updateUI({state: 'IDLE'});
            assert.equal(context.elements['btn-home'].disabled, true);
            assert.equal(context.elements['btn-home-retry'].disabled, true);
            await context.controller.homeDevice();
            return {ok: true, json: async () => ({success: true})};
        }, {route: '/api/home', confirm: () => true});
        h.controller.updateUI({state: 'IDLE'});
        h.controller.clearPath = () => cleared++;
        await h.controller.homeDevice();
        assert.equal(h.requestCount(), 1);
        assert.equal(cleared, 1);
        assert.equal(h.controller.homeInFlight, false);
        assert.equal(h.elements['btn-home'].disabled, true);
        assert.equal(h.elements['btn-home-abort'].disabled, false);
        console.log('PASS: re-home sends once and enables abort despite a failed status poll');
    }
    {
        let cleared = 0;
        const h = setup(async context => {
            context.controller.updateUI({state: 'RUNNING'});
            return {ok: false, status: 409, json: async () => ({success: false, message: 'System must be idle to home'})};
        }, {route: '/api/home', confirm: () => true});
        h.controller.updateUI({state: 'IDLE'});
        h.controller.clearPath = () => cleared++;
        await h.controller.homeDevice();
        assert.equal(h.requestCount(), 1);
        assert.equal(cleared, 0);
        assert.equal(h.elements['btn-home'].disabled, true);
        assert.equal(h.controller.homeInFlight, false);
        console.log('PASS: a busy response leaves re-home disabled and preserves the trace');
    }
    const {controller, elements, requestCount} = setup(() => { throw new Error('Unexpected abort request'); });
    assert.equal(controller.homingActive, false);
    await controller.abortHoming();
    assert.equal(requestCount(), 0);
    for (const state of ['UNINITIALIZED', 'INITIALIZED', 'IDLE', 'RUNNING', 'CLEARING',
        'PAUSED', 'STOPPING', 'PREPARING', 'HOMING_REVIEW', 'HOMING_FAILED', 'UNKNOWN']) {
        controller.updateUI({state: 'HOMING'});
        assert.equal(elements['btn-home-abort'].disabled, false);
        controller.updateUI({state});
        assert.equal(elements['btn-home-abort'].disabled, true, state);
        await controller.abortHoming();
        assert.equal(requestCount(), 0, state);
    }
    console.log('PASS: abort is only enabled and sent during active homing');

    await check('abort acknowledged despite failed status poll', async () => ({
        ok: true, json: async () => ({success: true, requiresHoming: true})
    }), /Position is untrusted/, true);
    await check('already idle', async () => ({
        ok: true, json: async () => ({success: true})
    }), /no active motion/, true);
    await check('HTTP failure', async () => ({
        ok: false, json: async () => ({success: false})
    }), /SWITCH OFF MOTOR POWER/, false);
    await check('network failure', async () => { throw new Error('Offline'); },
        /SWITCH OFF MOTOR POWER/, false);
    await check('timeout allows retry', async (context, options) => {
        context.expire();
        assert.equal(options.signal.aborted, true);
        throw new Error('AbortError');
    }, /You can retry Abort/, false);
    await check('polling cannot enable abort while a request is pending', async context => {
        context.controller.updateUI({state: 'HOMING'});
        assert.equal(context.elements['btn-home-abort'].disabled, true);
        await context.controller.abortHoming();
        return {ok: true, json: async () => ({success: true, requiresHoming: true})};
    }, /Position is untrusted/, true);
    await check('failed request cannot re-enable abort after homing ends', async context => {
        context.controller.updateUI({state: 'HOMING_FAILED'});
        throw new Error('Offline');
    }, /SWITCH OFF MOTOR POWER/, true);
})().catch(error => { console.error(error); process.exitCode = 1; });
