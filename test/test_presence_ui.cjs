// Run with: node --test test/test_presence_ui.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const html = fs.readFileSync(path.join(__dirname,
    '../lib/WebServer/src/SettingsUI.h'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const ready = { available: true, receiving: true, calibrated: true,
    suppressed: false, calibrating: false, motion: false, score: 0.1 };
const response = (data, ok = true) => ({ ok, status: ok ? 200 : 503,
    json: async () => data });

function page(fetch) {
    const nodes = new Map();
    for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
        nodes.set(match[1], { disabled: /\bdisabled\b/.test(match[0]),
            value: '', textContent: '', addEventListener() {} });
    }
    const timers = new Map();
    let timerId = 0;
    const context = vm.createContext({
        document: { getElementById: id => {
            assert.ok(nodes.has(id), `Missing element ${id}`);
            return nodes.get(id);
        } }, window: {}, fetch, AbortController, FormData, console,
        confirm: () => true, alert() {},
        setTimeout: (fn, ms) => { timers.set(++timerId, { fn, ms }); return timerId; },
        clearTimeout: id => timers.delete(id)
    });
    vm.runInContext(script, context);
    return { context, nodes, timers };
}

test('failed settings loads are visible and cannot overwrite the stored response', async () => {
    let requests = 0;
    const { context, nodes } = page(async () => { ++requests; return response({}, false); });
    await context.loadPresenceSettings();
    assert.equal(nodes.get('btn-save-presence').disabled, true);
    assert.equal(nodes.get('presence-action').disabled, true);
    assert.match(nodes.get('presence-feedback').textContent, /Could not load/);
    await context.savePresenceSettings();
    assert.equal(requests, 1);
});

test('polling never overwrites save results or errors', async () => {
    let saveFails = false;
    const { context, nodes } = page(async (url, options) => {
        if (options.method === 'POST') return response({ success: !saveFails,
            message: 'Storage failed' }, !saveFails);
        return response(url.endsWith('/settings/presence') ? { action: 'none' } : ready);
    });
    await context.loadPresenceSettings();
    assert.equal(nodes.get('presence-action').value, 'none');
    await context.savePresenceSettings();
    await context.refreshPresenceStatus();
    assert.equal(nodes.get('presence-feedback').textContent, 'Movement response saved.');
    saveFails = true;
    await context.savePresenceSettings();
    await context.refreshPresenceStatus();
    assert.match(nodes.get('presence-feedback').textContent, /Storage failed/);
});

test('calibration cannot be submitted twice or reenabled by a poll while pending', async () => {
    let complete, posts = 0;
    const { context, nodes } = page(async (url, options) => {
        if (options.method !== 'POST') return response(ready);
        ++posts;
        return new Promise(resolve => { complete = resolve; });
    });
    const pending = context.calibratePresence();
    await context.calibratePresence();
    await context.refreshPresenceStatus();
    assert.equal(posts, 1);
    assert.equal(nodes.get('btn-presence-calibrate').disabled, true);
    complete(response({ success: true }));
    await pending;
    await context.refreshPresenceStatus();
    assert.equal(nodes.get('btn-presence-calibrate').disabled, false);
    assert.match(nodes.get('presence-feedback').textContent, /Calibration started/);
});

test('stale or suppressed samples never display a live score or room-clear claim', async () => {
    let status = { ...ready, receiving: false, calibrating: true };
    const { context, nodes } = page(async () => response(status));
    await context.refreshPresenceStatus();
    assert.equal(nodes.get('presence-state').textContent, 'No CSI samples');
    assert.equal(nodes.get('presence-score').textContent, '—');
    assert.equal(nodes.get('btn-presence-calibrate').disabled, true);
    status = { ...ready, suppressed: true };
    await context.refreshPresenceStatus();
    assert.equal(nodes.get('presence-score').textContent, '—');
});

test('hung requests time out, and polls schedule only after completion', async () => {
    const { context, nodes, timers } = page(async (url, { signal }) =>
        new Promise((resolve, reject) => signal.addEventListener('abort',
            () => reject(new Error('Request timed out')))));
    const pending = context.pollPresence();
    assert.equal([...timers.values()].filter(t => t.ms === 1000).length, 0);
    [...timers.values()].find(t => t.ms === 5000).fn();
    await pending;
    assert.equal([...timers.values()].filter(t => t.ms === 1000).length, 1);
    assert.match(nodes.get('presence-status-note').textContent, /timed out/);
});

test('slow commissioning settings do not block presence initialization', async () => {
    const requests = [];
    const { context } = page(url => { requests.push(url); return new Promise(() => {}); });
    context.window.onload();
    assert.deepEqual(requests, ['/api/tuning', '/api/settings/presence', '/api/presence']);
});
