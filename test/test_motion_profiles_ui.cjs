// Run with: node --test test/test_motion_profiles_ui.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const html = fs.readFileSync(path.join(__dirname,
    '../lib/WebServer/src/SettingsUI.h'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const fields = ['rMaxVelocity', 'rMaxAccel', 'rMaxJerk', 'tMaxVelocity', 'tMaxAccel',
    'tMaxJerk', 'ballMaxVelocity', 'ballMaxAccel', 'cornerTolerance'];
const factory = { rMaxVelocity: 5.5, rMaxAccel: 20, rMaxJerk: 100, tMaxVelocity: 0.225,
    tMaxAccel: 2, tMaxJerk: 10, ballMaxVelocity: 30, ballMaxAccel: 100, cornerTolerance: 0.1 };
// Firmware caps rho at 40 mm/s: 16 kHz at 8 microsteps.
const superSpeed = { rMaxVelocity: 40, rMaxAccel: 200, rMaxJerk: 1000, tMaxVelocity: 2.25,
    tMaxAccel: 20, tMaxJerk: 100, ballMaxVelocity: 300, ballMaxAccel: 1000, cornerTolerance: 0.1 };
const response = (data, ok = true) => ({ ok, status: ok ? 200 : 409, json: async () => data });

function tuning(motion, persistenceAvailable = true) {
    return { motion, motionProfiles: { factory, superSpeed }, persistenceAvailable,
        limits: { thetaMaxRunCurrentMa: 1000, rhoMaxRunCurrentMa: 1000 },
        homing: {}, thetaDriver: {}, rhoDriver: {} };
}

function page({ motion = factory, persistenceAvailable = true, saveOk = true, confirm = true } = {}) {
    const nodes = new Map();
    for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
        const attributes = {};
        nodes.set(match[1], { disabled: /\bdisabled\b/.test(match[0]), value: '', textContent: '',
            checked: false, style: {}, addEventListener() {},
            setAttribute: (name, value) => { attributes[name] = String(value); },
            getAttribute: name => attributes[name] });
    }
    const posts = [];
    const notices = [];
    const context = vm.createContext({
        document: { getElementById: id => {
            assert.ok(nodes.has(id), `Missing element ${id}`);
            return nodes.get(id);
        } }, window: {}, AbortController, FormData, console,
        fetch: async (url, options = {}) => {
            if (options.method === 'POST') {
                posts.push({ url, body: Object.fromEntries(options.body) });
                return response(saveOk ? { success: true }
                    : { success: false, message: 'Settings are unsafe or the system is busy' }, saveOk);
            }
            return response(url.endsWith('/tuning') ? tuning(motion, persistenceAvailable) : {});
        },
        uiConfirm: async () => confirm, uiAlert: async () => {},
        uiNotify: (message, options) => notices.push({ message, tone: options && options.tone }),
        setTimeout: () => 0, clearTimeout() {}
    });
    vm.runInContext(script, context);
    const pressed = name => nodes.get('btn-profile-' + name).getAttribute('aria-pressed');
    const form = () => Object.fromEntries(fields.map(f => [f, Number(nodes.get('tune-' + f).value)]));
    return { context, nodes, posts, notices, pressed, form };
}

test('the saved profile is highlighted and the rho cap is explained', async () => {
    const { context, nodes, pressed } = page();
    await context.loadSettings();
    assert.equal(pressed('factory'), 'true');
    assert.equal(pressed('superSpeed'), 'false');
    assert.equal(nodes.get('btn-profile-superSpeed').disabled, false);
    assert.equal(nodes.get('btn-reset-motion').disabled, false);
    assert.equal(nodes.get('profile-superSpeed-detail').textContent,
        'Ball 300 mm/s · Rho 40 mm/s · Theta 2.25 rad/s');
    assert.match(nodes.get('motion-profile-note').textContent, /^Factory profile is active\./);
    assert.match(nodes.get('motion-profile-note').textContent, /caps rho at 40 mm\/s/);
});

test('float noise from the firmware still matches a profile', async () => {
    const { context, pressed } = page({ motion: { ...factory, tMaxVelocity: 0.2249999940395355 } });
    await context.loadSettings();
    assert.equal(pressed('factory'), 'true');
});

test('choosing super speed saves every profile value and moves the highlight', async () => {
    const { context, posts, pressed, form } = page();
    await context.loadSettings();
    assert.equal(await context.selectMotionProfile('superSpeed'), true);
    assert.equal(posts.length, 1);
    assert.equal(posts[0].url, '/api/tuning/motion');
    assert.deepEqual(Object.fromEntries(Object.entries(posts[0].body).map(([k, v]) => [k, Number(v)])),
        superSpeed);
    assert.deepEqual(form(), superSpeed);
    assert.equal(pressed('superSpeed'), 'true');
    assert.equal(pressed('factory'), 'false');
});

test('choosing the active profile does nothing', async () => {
    const { context, posts } = page({ motion: superSpeed });
    await context.loadSettings();
    assert.equal(await context.selectMotionProfile('superSpeed'), false);
    assert.equal(posts.length, 0);
});

test('reset restores factory values from custom settings', async () => {
    const custom = { ...factory, rMaxVelocity: 8 };
    const { context, nodes, posts, pressed, form } = page({ motion: custom });
    await context.loadSettings();
    assert.equal(pressed('factory'), 'false');
    assert.match(nodes.get('motion-profile-note').textContent, /^Custom values are active/);
    assert.equal(await context.adoptMotionProfile('factory'), true);
    assert.equal(posts.length, 1);
    assert.deepEqual(form(), factory);
    assert.equal(pressed('factory'), 'true');
});

test('a declined confirmation or a rejected save changes nothing', async () => {
    const declined = page({ confirm: false });
    await declined.context.loadSettings();
    assert.equal(await declined.context.selectMotionProfile('superSpeed'), false);
    assert.equal(declined.posts.length, 0);

    const rejected = page({ saveOk: false });
    await rejected.context.loadSettings();
    assert.equal(await rejected.context.selectMotionProfile('superSpeed'), false);
    assert.equal(rejected.pressed('factory'), 'true');
    assert.deepEqual(rejected.form(), factory);
    assert.equal(rejected.notices.at(-1).tone, 'error');
    assert.match(rejected.notices.at(-1).message, /unsafe or the system is busy/);
    assert.equal(rejected.nodes.get('btn-profile-superSpeed').disabled, false);
});

test('profiles and reset stay disabled without settings storage', async () => {
    const { context, nodes } = page({ persistenceAvailable: false });
    await context.loadSettings();
    for (const id of ['btn-profile-factory', 'btn-profile-superSpeed', 'btn-reset-motion']) {
        assert.equal(nodes.get(id).disabled, true, id);
    }
});
