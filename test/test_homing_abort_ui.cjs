// Run with node test/test_homing_abort_ui.cjs. No browser or hardware needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
for (const name of ['WebUI.h', 'ManualUI.h', 'TuningUI.h']) {
    const html = fs.readFileSync(path.join(__dirname, '../lib/WebServer/src', name), 'utf8');
    for (const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) new vm.Script(match[1]);
    assert.ok(html.includes('/home/abort'), `${name} must use the homing-aware abort route`);
}
const source = fs.readFileSync(path.join(__dirname, '../lib/WebServer/src/WebUI.h'), 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
assert.match(source, /id="btn-home-abort"[^>]*>Abort homing<\/button>/);
assert.match(source, /'btn-home-abort'\).addEventListener\('click', \(\) => this.abortHoming\(\)\)/);
assert.doesNotMatch(source.match(/<button[^>]*id="btn-home-abort"[^>]*>/)[0], /display:\s*none|disabled/);

async function check(name, fetchImpl, expected) {
    const elements = {'btn-home-abort': {}, 'home-abort-status': {}};
    let cleared = false;
    const context = vm.createContext({
        document: {getElementById: id => elements[id]},
        AbortController,
        setTimeout: fn => { context.expire = fn; return 1; },
        clearTimeout: () => { cleared = true; },
        fetch: (url, options) => {
            assert.equal(url, '/api/home/abort');
            assert.equal(options.method, 'POST');
            return fetchImpl(context, options);
        },
        // Any confirmation dialog is a regression: abort must send first.
        confirm: () => { throw new Error('Unexpected confirmation'); }
    });
    vm.runInContext(script, context);
    const controller = Object.create(context.Controller.prototype);
    controller.apiBase = '/api';
    controller.pollStatusOnce = async () => { throw new Error('Offline status'); };
    await controller.abortHoming();
    assert.match(elements['home-abort-status'].textContent, expected);
    assert.equal(elements['btn-home-abort'].disabled, false);
    assert.equal(controller.abortInFlight, false);
    assert.equal(cleared, true);
    console.log('PASS:', name);
}

(async () => {
    await check('abort acknowledged despite failed status poll', async () => ({
        ok: true, json: async () => ({success: true, requiresHoming: true})
    }), /Position is untrusted/);
    await check('already idle', async () => ({
        ok: true, json: async () => ({success: true})
    }), /no active motion/);
    await check('HTTP failure', async () => ({
        ok: false, json: async () => ({success: false})
    }), /SWITCH OFF MOTOR POWER/);
    await check('network failure', async () => { throw new Error('Offline'); },
        /SWITCH OFF MOTOR POWER/);
    await check('timeout allows retry', async (context, options) => {
        context.expire();
        assert.equal(options.signal.aborted, true);
        throw new Error('AbortError');
    }, /You can retry Abort/);
})().catch(error => { console.error(error); process.exitCode = 1; });
