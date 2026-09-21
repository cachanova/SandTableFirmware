// node test/test_ui_gzip.cjs: the flash-resident gzip blobs the pages ship as.
// The generator is the only thing standing between the readable page sources
// and what the browser receives, so inflate its output and compare.
const assert = require('node:assert/strict');
const zlib = require('node:zlib');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const ROOT = path.join(__dirname, '..');
const SOURCE = path.join(ROOT, 'lib/WebServer/src');
const GENERATED = path.join(SOURCE, 'UIPagesGz.h');
const PAGES = [
    ['WEB_UI', 'WebUI.h'],
    ['MANUAL_UI', 'ManualUI.h'],
    ['SETTINGS_UI', 'SettingsUI.h'],
    ['FILE_UI', 'FileUI.h'],
];

function literals(file) {
    const text = fs.readFileSync(path.join(SOURCE, file), 'utf8');
    const found = {};
    const re = /const char (\w+)\[\] PROGMEM = R"(\w+)\(/g;
    let m;
    while ((m = re.exec(text))) {
        const start = m.index + m[0].length;
        const end = text.indexOf(')' + m[2] + '"', start);
        assert.ok(end > 0, `unterminated literal ${m[1]} in ${file}`);
        found[m[1]] = text.slice(start, end);
    }
    return found;
}

function blob(header, name) {
    const body = new RegExp(`${name}_GZ\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};`).exec(header);
    assert.ok(body, `${name}_GZ missing from UIPagesGz.h`);
    return Buffer.from([...body[1].matchAll(/0x([0-9a-f]{2})/g)].map(b => parseInt(b[1], 16)));
}

const generate = () => execFileSync('python3', [path.join(ROOT, 'scripts/build_ui_gz.py')], { cwd: ROOT });

generate();
const header = fs.readFileSync(GENERATED, 'utf8');
const dialog = literals('UIDialog.h');
let raw = 0, packed = 0;

for (const [prefix, file] of PAGES) {
    const page = literals(file);
    const expected = page[`${prefix}_HEAD`] + dialog.UI_DIALOG_CSS + page[`${prefix}_BODY`] +
                     dialog.UI_DIALOG_JS + page[`${prefix}_SCRIPT`];
    const gz = blob(header, prefix);
    assert.equal(gz[0], 0x1f, `${prefix} is not a gzip stream`);
    assert.equal(gz[1], 0x8b, `${prefix} is not a gzip stream`);

    const inflated = zlib.gunzipSync(gz).toString();
    assert.equal(inflated, expected, `${prefix} does not inflate back to its page sources`);

    // A whole document, with the shared dialog spliced in once, in order.
    assert.ok(inflated.startsWith('\n<!DOCTYPE html>'), `${prefix} lost its doctype`);
    assert.ok(inflated.trimEnd().endsWith('</html>'), `${prefix} is truncated`);
    assert.equal(inflated.split('<body>').length - 1, 1, `${prefix} has a duplicated body`);
    assert.ok(inflated.indexOf(dialog.UI_DIALOG_JS) > inflated.indexOf(dialog.UI_DIALOG_CSS),
              `${prefix} splices the dialog script before its CSS`);
    assert.ok(gz.length < expected.length / 3, `${prefix} barely compressed: ${gz.length}/${expected.length}`);
    raw += expected.length;
    packed += gz.length;
}

// A rerun that changed a byte would relink every dependent object, and a
// non-reproducible build would hide which page an edit actually touched.
generate();
assert.equal(fs.readFileSync(GENERATED, 'utf8'), header, 'generator output is not reproducible');

// Flash headroom is the whole point; fail loudly if a page stops compressing.
assert.ok(packed < 64 * 1024, `pages grew to ${packed} bytes of flash`);
console.log(`PASS: four pages inflate byte-for-byte; ${raw} bytes of HTML in ${packed} bytes of flash`);
