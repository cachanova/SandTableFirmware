// node test/test_ui_dialog.cjs: the shared in-page confirm/alert/notice module.
// No browser needed; the DOM below is only as real as the module requires.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const SOURCE = path.join(__dirname, '../lib/WebServer/src');
const PAGES = ['WebUI.h', 'FileUI.h', 'ManualUI.h', 'SettingsUI.h'];

const dialogSource = fs.readFileSync(path.join(SOURCE, 'UIDialog.h'), 'utf8');
// Take the literal's contents, not the <script> the file header talks about.
const script = dialogSource.split('UI_DIALOG_JS[] PROGMEM =')[1].match(/<script>([\s\S]*?)<\/script>/)[1];

class Element {
    constructor(tag) {
        this.tagName = tag.toUpperCase();
        this.children = [];
        this.parent = null;
        this.attributes = {};
        this.listeners = {};
        this.className = '';
        this.hidden = false;
        this.focusCount = 0;
        this._text = '';
    }
    get textContent() {
        return this.children.length ? this.children.map(c => c.textContent).join('') : this._text;
    }
    set textContent(value) {
        this.children.forEach(child => { child.parent = null; });
        this.children = [];
        this._text = String(value);
    }
    append(...nodes) { nodes.forEach(node => this.appendChild(node)); }
    appendChild(node) { node.parent = this; this._text = ''; this.children.push(node); return node; }
    remove() {
        if (!this.parent) return;
        this.parent.children = this.parent.children.filter(child => child !== this);
        this.parent = null;
    }
    contains(node) {
        for (let cursor = node; cursor; cursor = cursor.parent) if (cursor === this) return true;
        return false;
    }
    setAttribute(name, value) { this.attributes[name] = String(value); }
    getAttribute(name) { return this.attributes[name]; }
    addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
    dispatch(type, event = {}) {
        const target = Object.assign({ type, preventDefault() { this.defaultPrevented = true; } }, event);
        (this.listeners[type] || []).forEach(callback => callback(target));
        return target;
    }
    querySelectorAll(selector) {
        assert.equal(selector, 'button', 'the focus trap only ever looks for buttons');
        return this.children.flatMap(child =>
            (child.tagName === 'BUTTON' ? [child] : []).concat(child.querySelectorAll(selector)));
    }
    get lastElementChild() { return this.children[this.children.length - 1] || null; }
}

function page() {
    let activeElement = null;
    const create = tag => {
        const element = new Element(tag);
        element.focus = function () { this.focusCount++; activeElement = this; };
        return element;
    };
    const body = create('body');
    const trigger = body.appendChild(create('main')); // What the operator last touched.
    activeElement = trigger;
    const timers = new Map();
    const keyListeners = [];
    let nextTimer = 0;
    const context = vm.createContext({
        document: {
            body,
            createElement: create,
            get activeElement() { return activeElement; },
            contains: node => body.contains(node),
            addEventListener: (type, callback) => {
                assert.equal(type, 'keydown', 'only the key handler belongs on the document');
                keyListeners.push(callback);
            }
        },
        setTimeout: (fn, ms) => { timers.set(++nextTimer, { fn, ms }); return nextTimer; },
        clearTimeout: id => timers.delete(id)
    });
    context.window = context; // As in a browser, the page globals are window.
    vm.runInContext(script, context);
    const api = {
        context,
        body,
        timers,
        trigger,
        focus: element => { activeElement = element; },
        key: (key, event = {}) => {
            const target = Object.assign({key, preventDefault() { this.defaultPrevented = true; }}, event);
            keyListeners.forEach(callback => callback(target));
            return target;
        },
        get host() { return body.children.find(child => child.className === 'ui-dialog-host'); },
        get panel() { return api.host.children.find(c => c.className === 'ui-dialog-panel'); },
        get scrim() { return api.host.children.find(c => c.className === 'ui-dialog-scrim'); },
        get actions() { return api.panel.children.find(c => c.className === 'ui-dialog-actions'); },
        get buttons() { return api.actions.children; },
        get notices() {
            const stack = body.children.find(child => child.className === 'ui-notice-stack');
            return stack ? stack.children : [];
        },
        button: label => {
            const found = api.buttons.find(b => b.textContent === label);
            assert.ok(found, `Missing ${label} button`);
            return found;
        },
        runTimer: () => {
            const [id, timer] = [...timers][0];
            timers.delete(id);
            timer.fn();
            return timer.ms;
        }
    };
    return api;
}

const flush = async () => { for (let i = 0; i < 10; ++i) await Promise.resolve(); };

(async () => {
    {
        // No page may fall back to a blocking browser popup, and each must be
        // assembled with the shared dialog spans rather than its own copy.
        // scripts/build_ui_gz.py joins the spans; test_ui_gzip.cjs checks the
        // blobs it emits, so what matters here is that no page is left out.
        const generator = fs.readFileSync(
            path.join(__dirname, '../scripts/build_ui_gz.py'), 'utf8');
        const composition = generator
            .match(/joined = \(([\s\S]*?)\)\n/)[1].replace(/\s+/g, ' ');
        assert.equal(composition,
            "page[prefix + '_HEAD'] + dialog['UI_DIALOG_CSS'] + " +
            "page[prefix + '_BODY'] + dialog['UI_DIALOG_JS'] + page[prefix + '_SCRIPT']",
            'pages must be sent as head, dialog CSS, body, dialog script, page script');
        for (const name of PAGES) {
            const html = fs.readFileSync(path.join(SOURCE, name), 'utf8');
            assert.ok(!/(^|[^.\w])(confirm|alert|prompt)\s*\(/.test(html),
                `${name} must use the in-page dialogs, not window.confirm/alert/prompt`);
            const prefix = html.match(/const char (\w+)_HEAD\[\]/)[1];
            assert.ok(generator.includes(`('${prefix}', '${name}')`),
                `${name} must be listed in the page table build_ui_gz.py compresses`);
        }
        console.log('PASS: every page is assembled from the shared in-page dialogs');
    }
    {
        const h = page();
        const trigger = h.trigger;
        const answer = h.context.uiConfirm('Body text', {title: 'Title', confirmLabel: 'Go'});
        await flush();
        assert.equal(h.host.hidden, false);
        assert.equal(h.panel.getAttribute('role'), 'alertdialog');
        assert.equal(h.panel.getAttribute('aria-modal'), 'true');
        assert.equal(h.panel.children[0].textContent, 'Title');
        assert.equal(h.panel.children[1].textContent, 'Body text');
        // The affirmative action is focused and rightmost; cancel comes first.
        assert.deepEqual(h.buttons.map(b => b.textContent), ['Cancel', 'Go']);
        assert.equal(h.context.document.activeElement, h.button('Go'));
        h.button('Go').dispatch('click');
        assert.equal(await answer, true);
        assert.equal(h.host.hidden, true);
        // Focus returns to whatever opened the dialog.
        assert.equal(h.context.document.activeElement, trigger);
        console.log('PASS: confirm resolves true, labels its buttons, and restores focus');
    }
    {
        const h = page();
        const cancelled = h.context.uiConfirm('Body');
        await flush();
        h.button('Cancel').dispatch('click');
        assert.equal(await cancelled, false);

        const scrimmed = h.context.uiConfirm('Body');
        await flush();
        h.scrim.dispatch('click');
        assert.equal(await scrimmed, false);

        const escaped = h.context.uiConfirm('Body');
        await flush();
        const event = h.key('Escape');
        assert.equal(event.defaultPrevented, true);
        assert.equal(await escaped, false);
        console.log('PASS: cancel, backdrop and Escape all decline');
    }
    {
        const h = page();
        h.context.uiConfirm('Body', {danger: true});
        await flush();
        // A destructive action must not sit under the return key on open.
        assert.equal(h.context.document.activeElement, h.button('Cancel'));
        assert.ok(h.button('Confirm').className.includes('ui-dialog-danger'));
        h.button('Cancel').dispatch('click');
        console.log('PASS: a destructive confirmation opens on Cancel');
    }
    {
        const h = page();
        h.context.uiConfirm('Body');
        await flush();
        const [cancel, confirm] = h.buttons;
        h.focus(confirm);
        assert.equal(h.key('Tab').defaultPrevented, true);
        assert.equal(h.context.document.activeElement, cancel, 'Tab wraps to the first button');
        assert.equal(h.key('Tab', {shiftKey: true}).defaultPrevented, true);
        assert.equal(h.context.document.activeElement, confirm, 'Shift+Tab wraps to the last');
        // Focus that escaped the panel is pulled back in.
        h.focus(h.trigger);
        h.key('Tab');
        assert.equal(h.context.document.activeElement, cancel);
        console.log('PASS: Tab stays inside the open dialog');
    }
    {
        const h = page();
        const first = h.context.uiConfirm('First');
        const second = h.context.uiConfirm('Second');
        await flush();
        // A second request waits rather than replacing the open question.
        assert.equal(h.panel.children[1].textContent, 'First');
        h.button('Confirm').dispatch('click');
        assert.equal(await first, true);
        await flush();
        assert.equal(h.panel.children[1].textContent, 'Second');
        assert.equal(h.buttons.length, 2, 'the previous buttons are cleared');
        h.button('Cancel').dispatch('click');
        assert.equal(await second, false);
        console.log('PASS: overlapping confirmations queue in order');
    }
    {
        const h = page();
        const acknowledged = h.context.uiAlert('Switch off motor power.', {title: 'No acknowledgement'});
        await flush();
        assert.deepEqual(h.buttons.map(b => b.textContent), ['OK']);
        assert.equal(h.context.document.activeElement, h.button('OK'));
        h.key('Escape');
        assert.equal(await acknowledged, undefined);
        console.log('PASS: alert acknowledges with a single button');
    }
    {
        const h = page();
        h.context.uiNotify('Saved.');
        assert.equal(h.notices.length, 1);
        assert.equal(h.notices[0].children[0].textContent, 'Saved.');
        assert.ok(!h.notices[0].className.includes('ui-notice-error'));
        assert.equal(h.runTimer(), 4000);
        assert.equal(h.notices.length, 0, 'an informational notice clears itself');

        const dismiss = h.context.uiNotify('Upload failed.', {tone: 'error'});
        assert.ok(h.notices[0].className.includes('ui-notice-error'));
        assert.equal(h.notices[0].getAttribute('role'), 'alert');
        assert.equal([...h.timers][0][1].ms, 9000, 'errors stay on screen longer');
        dismiss();
        assert.equal(h.notices.length, 0);
        assert.equal(h.timers.size, 0, 'dismissing a notice cancels its timer');

        h.context.uiNotify('Kept.', {tone: 'error'});
        h.notices[0].children[1].dispatch('click');
        assert.equal(h.notices.length, 0, 'the close button removes the notice');
        assert.equal(h.timers.size, 0);
        console.log('PASS: notices announce, expire, and can be dismissed');
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
