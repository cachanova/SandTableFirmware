#pragma once

// Shared in-page confirmations and notices for the four UI pages.
//
// Browser confirm()/alert() block the event loop, which stalls the status
// polling every page runs, and they cannot be styled or dismissed by touch
// outside the box. These replace them:
//
//   uiConfirm(message, {title, confirmLabel, cancelLabel, danger}) -> boolean
//   uiAlert(message, {title, dismissLabel})                        -> void
//   uiPrompt(message, {title, value, confirmLabel, cancelLabel})   -> string | null
//   uiNotify(message, {tone: 'error'})                             -> dismiss fn
//
// All four return promises, and calls are serialized so overlapping
// requests queue instead of stacking.
//
// Each page is one standalone document. scripts/build_ui_gz.py splices these
// two into every page before compressing it: the CSS at the end of the page's
// <style>, the script in the body ahead of the page's own.

const char UI_DIALOG_CSS[] PROGMEM = R"uidialogcss(
        /* ── In-page dialogs ────────────── */
        .ui-dialog-host {
            position: fixed;
            inset: 0;
            z-index: 90;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 24px;
        }
        .ui-dialog-host[hidden] { display: none; }
        .ui-dialog-scrim { position: absolute; inset: 0; background: rgba(26, 25, 23, .38); }
        .ui-dialog-panel {
            position: relative;
            width: min(100%, 430px);
            max-height: calc(100vh - 48px);
            overflow-y: auto;
            background: var(--paper);
            border: 1px solid var(--ink);
            box-shadow: 0 18px 44px rgba(26, 25, 23, .22);
            padding: 26px 28px 22px;
        }
        .ui-dialog-title {
            font-family: var(--serif);
            font-size: 19px;
            font-weight: 400;
            margin-bottom: 10px;
        }
        .ui-dialog-body {
            font-size: 13.5px;
            line-height: 1.55;
            color: var(--ink-soft);
            overflow-wrap: anywhere;
        }
        .ui-dialog-input {
            display: block;
            width: 100%;
            margin-top: 14px;
            padding: 10px 12px;
            border: 1px solid var(--ink);
            background: var(--paper);
            color: var(--ink);
            font: inherit;
            font-size: 15px;
        }
        .ui-dialog-input[hidden] { display: none; }
        .ui-dialog-input:focus-visible { outline: 2px solid var(--ink); outline-offset: 1px; }
        .ui-dialog-actions {
            display: flex;
            flex-wrap: wrap;
            width: fit-content;
            max-width: 100%;
            margin: 24px 0 0 auto;
            border: 1px solid var(--ink);
        }
        .ui-dialog-btn {
            min-width: 96px;
            padding: 12px 18px;
            border: none;
            border-right: 1px solid var(--ink);
            background: none;
            color: var(--ink);
            font-size: 11.5px;
            font-weight: 600;
            letter-spacing: 1.5px;
            text-transform: uppercase;
            cursor: pointer;
            transition: background .12s;
        }
        .ui-dialog-btn:last-child { border-right: none; }
        .ui-dialog-btn:hover { background: var(--sand); }
        .ui-dialog-btn:focus-visible { outline: 2px solid var(--ink); outline-offset: -5px; }
        .ui-dialog-btn.ui-dialog-primary { background: var(--ink); color: var(--paper); }
        .ui-dialog-btn.ui-dialog-primary:hover { background: #34322c; }
        .ui-dialog-btn.ui-dialog-danger { background: var(--danger); color: var(--paper); }
        .ui-dialog-btn.ui-dialog-danger:hover { background: #873227; }
        .ui-dialog-btn.ui-dialog-primary:focus-visible,
        .ui-dialog-btn.ui-dialog-danger:focus-visible { outline-color: var(--paper); }

        /* ── In-page notices ────────────── */
        .ui-notice-stack {
            position: fixed;
            right: 20px;
            bottom: 20px;
            z-index: 95;
            display: flex;
            flex-direction: column;
            align-items: stretch;
            gap: 8px;
            width: min(360px, calc(100vw - 40px));
            pointer-events: none;
        }
        .ui-notice {
            display: flex;
            align-items: flex-start;
            gap: 12px;
            padding: 12px 14px;
            background: var(--ink);
            color: var(--paper);
            font-size: 13px;
            line-height: 1.45;
            overflow-wrap: anywhere;
            box-shadow: 0 10px 26px rgba(26, 25, 23, .24);
            pointer-events: auto;
        }
        .ui-notice-error { background: var(--danger); }
        .ui-notice-text { flex: 1; }
        .ui-notice-dismiss {
            flex: none;
            border: none;
            background: none;
            color: inherit;
            font-size: 16px;
            line-height: 1;
            padding: 0 2px;
            cursor: pointer;
            opacity: .7;
        }
        .ui-notice-dismiss:hover { opacity: 1; }
        .ui-notice-dismiss:focus-visible { outline: 1px solid var(--paper); outline-offset: 2px; }
)uidialogcss";

const char UI_DIALOG_JS[] PROGMEM = R"uidialogjs(
    <script>
    (function () {
        'use strict';

        let host = null, panelEl = null, titleEl = null, bodyEl = null, inputEl = null, actionsEl = null;
        let current = null;
        let chain = Promise.resolve();

        function build() {
            if (host) return;
            host = document.createElement('div');
            host.className = 'ui-dialog-host';
            host.hidden = true;

            const scrim = document.createElement('div');
            scrim.className = 'ui-dialog-scrim';

            panelEl = document.createElement('div');
            panelEl.className = 'ui-dialog-panel';
            panelEl.setAttribute('role', 'alertdialog');
            panelEl.setAttribute('aria-modal', 'true');
            panelEl.setAttribute('aria-labelledby', 'ui-dialog-title');
            panelEl.setAttribute('aria-describedby', 'ui-dialog-body');

            titleEl = document.createElement('h2');
            titleEl.className = 'ui-dialog-title';
            titleEl.id = 'ui-dialog-title';

            bodyEl = document.createElement('p');
            bodyEl.className = 'ui-dialog-body';
            bodyEl.id = 'ui-dialog-body';

            inputEl = document.createElement('input');
            inputEl.type = 'text';
            inputEl.className = 'ui-dialog-input';
            inputEl.hidden = true;
            inputEl.setAttribute('aria-labelledby', 'ui-dialog-title');
            inputEl.addEventListener('keydown', event => {
                // Return submits the text the way a native prompt does.
                if (event.key === 'Enter' && current && current.submit) {
                    event.preventDefault();
                    current.submit();
                }
            });

            actionsEl = document.createElement('div');
            actionsEl.className = 'ui-dialog-actions';

            panelEl.append(titleEl, bodyEl, inputEl, actionsEl);
            host.append(scrim, panelEl);
            document.body.appendChild(host);

            scrim.addEventListener('click', () => { if (current) settle(current.cancelValue); });
            // On the document, not the dialog: clicking the dialog's own text
            // moves focus to the body, and Escape must still close from there.
            document.addEventListener('keydown', onKeyDown);
        }

        function onKeyDown(event) {
            if (!current) return;
            if (event.key === 'Escape') {
                event.preventDefault();
                settle(current.cancelValue);
                return;
            }
            if (event.key !== 'Tab') return;
            // Keep focus inside the dialog for as long as it is open.
            const buttons = Array.from(actionsEl.querySelectorAll('button'));
            if (!inputEl.hidden) buttons.unshift(inputEl);
            if (!buttons.length) return;
            const first = buttons[0], last = buttons[buttons.length - 1];
            const focused = document.activeElement;
            const outside = !panelEl.contains(focused);
            if (event.shiftKey && (outside || focused === first)) {
                event.preventDefault();
                last.focus();
            } else if (!event.shiftKey && (outside || focused === last)) {
                event.preventDefault();
                first.focus();
            }
        }

        function settle(value) {
            if (!current) return;
            const pending = current;
            current = null;
            host.hidden = true;
            inputEl.hidden = true;
            actionsEl.textContent = '';
            const restore = pending.previousFocus;
            if (restore && typeof restore.focus === 'function' && document.contains(restore)) {
                restore.focus({ preventScroll: true });
            }
            pending.resolve(value);
        }

        function present(options) {
            build();
            return new Promise(resolve => {
                current = {
                    resolve: resolve,
                    cancelValue: options.cancelValue,
                    previousFocus: document.activeElement,
                    submit: null
                };
                const hasInput = options.input !== undefined;
                inputEl.hidden = !hasInput;
                inputEl.value = hasInput ? options.input : '';
                titleEl.textContent = options.title || '';
                titleEl.hidden = !options.title;
                bodyEl.textContent = options.message || '';
                bodyEl.hidden = !options.message;
                actionsEl.textContent = '';

                let initial = null;
                options.buttons.forEach(spec => {
                    const button = document.createElement('button');
                    button.type = 'button';
                    button.className = 'ui-dialog-btn' + (spec.style ? ' ui-dialog-' + spec.style : '');
                    button.textContent = spec.label;
                    const choose = () => settle(spec.fromInput ? inputEl.value : spec.value);
                    button.addEventListener('click', choose);
                    if (spec.fromInput) current.submit = choose;
                    actionsEl.appendChild(button);
                    if (spec.initialFocus) initial = button;
                });

                host.hidden = false;
                if (hasInput) {
                    inputEl.focus();
                    if (typeof inputEl.select === 'function') inputEl.select();
                    return;
                }
                const focusTarget = initial || actionsEl.lastElementChild;
                if (focusTarget) focusTarget.focus();
            });
        }

        // One dialog at a time: a second request waits for the first to close.
        function queue(options) {
            const run = () => present(options);
            const result = chain.then(run, run);
            chain = result.then(() => {}, () => {});
            return result;
        }

        window.uiConfirm = function (message, options) {
            const opts = options || {};
            const danger = !!opts.danger;
            return queue({
                title: opts.title || 'Confirm',
                message: message,
                cancelValue: false,
                buttons: [
                    {
                        label: opts.cancelLabel || 'Cancel',
                        value: false,
                        // A destructive or motion-starting action never opens
                        // with its own confirm button under the return key.
                        initialFocus: danger
                    },
                    {
                        label: opts.confirmLabel || 'Confirm',
                        value: true,
                        style: danger ? 'danger' : 'primary',
                        initialFocus: !danger
                    }
                ]
            });
        };

        window.uiAlert = function (message, options) {
            const opts = options || {};
            return queue({
                title: opts.title || 'Notice',
                message: message,
                cancelValue: undefined,
                buttons: [{
                    label: opts.dismissLabel || 'OK',
                    value: undefined,
                    style: 'primary',
                    initialFocus: true
                }]
            });
        };

        window.uiPrompt = function (message, options) {
            const opts = options || {};
            return queue({
                title: opts.title || 'Enter a value',
                message: message,
                input: opts.value === undefined ? '' : String(opts.value),
                cancelValue: null,
                buttons: [
                    { label: opts.cancelLabel || 'Cancel', value: null },
                    { label: opts.confirmLabel || 'OK', fromInput: true, style: 'primary' }
                ]
            });
        };

        let noticeStack = null;

        window.uiNotify = function (message, options) {
            const opts = options || {};
            const isError = opts.tone === 'error';
            if (!noticeStack) {
                noticeStack = document.createElement('div');
                noticeStack.className = 'ui-notice-stack';
                noticeStack.setAttribute('aria-live', 'polite');
                document.body.appendChild(noticeStack);
            }

            const notice = document.createElement('div');
            notice.className = 'ui-notice' + (isError ? ' ui-notice-error' : '');
            if (isError) notice.setAttribute('role', 'alert');

            const text = document.createElement('span');
            text.className = 'ui-notice-text';
            text.textContent = message;

            const dismiss = document.createElement('button');
            dismiss.type = 'button';
            dismiss.className = 'ui-notice-dismiss';
            dismiss.setAttribute('aria-label', 'Dismiss');
            dismiss.textContent = '×';

            notice.append(text, dismiss);
            noticeStack.appendChild(notice);

            let timer = 0;
            const remove = () => { clearTimeout(timer); notice.remove(); };
            dismiss.addEventListener('click', remove);
            timer = setTimeout(remove, isError ? 9000 : 4000);
            return remove;
        };
    })();
    </script>
)uidialogjs";
