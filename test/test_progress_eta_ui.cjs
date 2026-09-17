// node test/test_progress_eta_ui.cjs: progress text carries a speed-based ETA.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('lib/WebServer/src/WebUI.h', 'utf8');
const script = source.split('<script>')[1].split('</script>')[0]
    .replace('const controller = new SisyphusController();', 'globalThis.Controller = SisyphusController;');

const elements = new Map();
function el(id) {
    if (!elements.has(id)) {
        elements.set(id, {
            id, style: {}, dataset: {},
            classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
            textContent: '', value: '', disabled: false, src: '',
            appendChild() {}, addEventListener() {},
            querySelector() { return el(`${id}:child`); },
        });
    }
    return elements.get(id);
}
const context = vm.createContext({
    document: {
        getElementById: id => el(id),
        createElement: tag => el(`created:${tag}:${elements.size}`),
        activeElement: null,
    },
    URL: { revokeObjectURL() {} },
    Number,
});
vm.runInContext(script, context);
const controller = Object.create(context.Controller.prototype);
controller.clearPath = () => {};
controller.setOverlayImage = () => {};
controller.loadFileList = () => {};
controller.syncBrightnessControl = () => {};

const baseStatus = { presence: {}, homing: {}, drivers: {} };

controller.updateUI({ ...baseStatus, state: 'RUNNING', progress: 3, clearingProgress: -1,
    etaSeconds: 754 }, 0);
assert.equal(el('file-progress-text').textContent, '3% · ~13 min left');
assert.equal(el('np-file-progress-text').textContent, '3% · ~13 min left');
assert.equal(el('file-progress-bar').style.width, '3%');
console.log('PASS: running progress text includes the remaining-time estimate');

controller.updateUI({ ...baseStatus, state: 'RUNNING', progress: 42, clearingProgress: -1,
    etaSeconds: -1 }, 0);
assert.equal(el('file-progress-text').textContent, '42%');
console.log('PASS: an unknown ETA leaves the plain percentage');

controller.updateUI({ ...baseStatus, state: 'CLEARING', progress: 10, clearingProgress: 40,
    etaSeconds: 90 }, 0);
assert.equal(el('file-progress-text').textContent, '40%');
console.log('PASS: clearing progress never shows a pattern ETA');

assert.equal(controller.formatEta(0), '0s');
assert.equal(controller.formatEta(59), '59s');
assert.equal(controller.formatEta(60), '1 min');
assert.equal(controller.formatEta(3569), '59 min');
assert.equal(controller.formatEta(3600), '1h');
assert.equal(controller.formatEta(5400), '1h 30m');
console.log('PASS: ETA formatting covers seconds, minutes, and hours');
