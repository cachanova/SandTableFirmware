// node test/test_canvas_trace_ui.cjs: geometry, SSE continuity, and rendering work.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('lib/WebServer/src/WebUI.h', 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');
function canvas() {
    const calls = [];
    const ctx = Object.fromEntries(['clearRect', 'beginPath', 'arc', 'fill', 'moveTo', 'lineTo', 'stroke', 'drawImage']
        .map(name => [name, (...args) => calls.push([name, ...args])]));
    ctx.createRadialGradient = (...args) => { calls.push(['gradient', ...args]); return { addColorStop() {} }; };
    return { width: 800, height: 800, calls, getContext: () => ctx };
}
const path = canvas(), ball = canvas(), sprite = canvas();
let labelWrites = 0, now = 1000, frameId = 0;
const label = { set textContent(value) { this.value = value; labelWrites++; } };
const frames = new Map(), streams = [];
const context = vm.createContext({
    document: { getElementById: id => ({'path-canvas':path, 'ball-canvas':ball, 'position-coords':label})[id],
        createElement: () => sprite },
    performance: { now: () => now },
    requestAnimationFrame: callback => { frames.set(++frameId, callback); return frameId; },
    EventSource: class {
        constructor() { this.handlers = {}; streams.push(this); }
        addEventListener(name, callback) { this.handlers[name] = callback; }
        close() { this.closed = true; }
    }
});
vm.runInContext(script, context);
const controller = Object.create(context.Controller.prototype);
controller.setupCanvas();
const count = (target, name) => target.calls.filter(call => call[0] === name).length;
const sample = (x, y, extra = {}) => ({ x, y, r: 10, t: Math.PI / 2, vr: 1, vt: 1, vc: 2, ...extra });
function flush() {
    const current = [...frames.values()]; frames.clear(); current.forEach(callback => callback());
}
controller.drawStreamPosition(sample(.5, .5));
controller.drawStreamPosition(sample(1, .5));
controller.drawStreamPosition(sample(.5, 1));
assert.equal(frames.size, 1, 'an SSE burst schedules one render');
flush();
assert.deepEqual(path.calls.filter(c => c[0] === 'lineTo'), [['lineTo',780,400], ['lineTo',400,780]],
    'retain every sampled corner and align normalized radius to the PNG 20px inset');
assert.equal(count(ball, 'drawImage'), 1, 'render the latest ball once per frame');
assert.equal(count(sprite, 'gradient'), 1);
assert.match(label.value, /θ 90\.0°/);
console.log('PASS: SSE bursts preserve trace corners, align image geometry, and render one cached ball');

let segments = count(path, 'stroke');
controller.drawStreamPosition(sample(.5, 1)); flush();
assert.equal(count(path, 'stroke'), segments, 'stationary heartbeats do not darken trace');
assert.equal(count(ball, 'drawImage'), 1);
assert.equal(labelWrites, 1, 'coordinate text is throttled while moving');
controller.drawStreamPosition(sample(.5, 1, {vr:0, vt:0, vc:0})); flush();
assert.equal(labelWrites, 2, 'stopping velocity is displayed immediately');
controller.clearPath();
assert.equal(controller.ballY, 780, 'history reset preserves stationary ball');
assert.equal(count(ball, 'clearRect'), 0);
controller.drawStreamPosition(sample(.6, .6)); flush();
assert.equal(count(path, 'stroke'), segments, 'reset starts a new trace without a connecting chord');
assert.deepEqual(ball.calls.filter(c => c[0] === 'clearRect').at(-1).slice(3), [32,32]);
console.log('PASS: stationary updates avoid repaint, stop readout is immediate, resets preserve the ball');

controller.drawStreamPosition(sample(NaN, .5));
controller.drawStreamPosition(sample(.5, Infinity));
controller.drawStreamPosition(null);
assert.equal(frames.size, 0);
controller.drawStreamPosition(sample(.1, .1));
controller.drawStreamPosition(sample(.2, .2, {clear:true}));
controller.drawStreamPosition(sample(.3, .3));
segments = count(path, 'stroke');
flush();
assert.equal(count(path, 'stroke'), segments + 1, 'clear drops older queued points');
const clearCount = count(path, 'clearRect');
controller.drawStreamPosition(sample(.5, .5, {clear:true}));
segments = count(path, 'stroke');
const ballsBeforeHidden = count(ball, 'drawImage');
for (let i=0; i<5000; ++i) {
    controller.drawStreamPosition(sample(.5 + .3*Math.sin(i/20), .5 + .3*Math.cos(i/20)));
    assert.ok(controller.pendingPositions.length <= 128);
    assert.equal(frames.size, 1, 'hidden batches never add animation callbacks');
}
assert.equal(count(ball, 'drawImage'), ballsBeforeHidden, 'background batches only rasterize path');
assert.ok(count(path, 'stroke') > segments + 4800, 'history is drawn before animation resumes');
flush();
assert.equal(count(path, 'clearRect'), clearCount + 1, 'background batch applies pattern clear once');
assert.equal(count(path, 'stroke'), segments + 5000, 'every hidden-tab segment survives without gaps');
assert.equal(count(ball, 'drawImage'), ballsBeforeHidden + 1, 'returning to tab paints latest ball once');
console.log('PASS: 5000 hidden-tab points retain all trace segments with bounded memory and one pending frame');

controller.connectStream();
controller.drawStreamPosition(sample(.1, .1));
streams[0].handlers.open();
streams[0].handlers.pos({data:JSON.stringify(sample(.9, .9))});
segments = count(path, 'stroke');
flush();
assert.equal(count(path, 'stroke'), segments, 'reconnect never connects across missing movement');
controller.connectStream();
assert.equal(streams[0].closed, true);
assert.equal(count(sprite, 'gradient'), 1, 'ball gradient remains cached across all frames');
console.log('PASS: SSE reconnection breaks missing trace segments and closes replaced streams');
