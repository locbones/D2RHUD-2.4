const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {pathToFileURL} = require('node:url');
const root = path.resolve(__dirname, '..');
const editor = path.join(root, 'translations/editor.html');
const source = fs.readFileSync(editor, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const context = {}; vm.createContext(context); vm.runInContext(source, context);
const tools = context.HudTranslationTools;
assert.equal(tools.validate('Reward: %d', 'Récompense : %d'), '');
assert.notEqual(tools.validate('Reward: %d', 'Récompense : %s'), '');
assert.notEqual(tools.validate('Reward', '%n'), '');
assert.notEqual(tools.validate('Reward', 'Récompense###changed-id'), '');
assert.notEqual(tools.validate('Reward', ''), '');
let document = {_meta: {locale: 'frFR'}, strings: {'Old': 'Ancien', 'Keep': 'Garder'}, reviewed: ['Keep', 'Old']};
document = tools.mergeEnglish(document, {_meta: {locale: 'enUS'}, strings: {'Keep': 'Keep', 'New': 'New'}});
assert.equal(document.strings.Keep, 'Garder'); assert.equal(document.strings.New, 'New');
assert.equal(document.strings.Old, undefined); assert.deepEqual(Array.from(document.reviewed), ['Keep']);
assert.equal(JSON.parse(tools.serialize(document)).strings.Keep, 'Garder');
console.log('PASS: offline editor validation, placeholder safety, English-source merging and UTF-8 export.');

if (process.argv.includes('--browser')) (async () => {
    const {chromium} = require('playwright');
    const output = process.argv[process.argv.indexOf('--browser') + 1]; fs.mkdirSync(output, {recursive: true});
    const browser = await chromium.launch({channel: 'msedge', headless: true});
    try {
        const page = await browser.newPage({viewport: {width: 1440, height: 1000}});
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        page.on('request', request => assert.match(request.url(), /^(file:|blob:|data:)/));
        await page.goto(pathToFileURL(editor).href);
        await page.locator('#open').setInputFiles(path.join(root, 'translations/frFR.json'));
        await page.locator('#search').fill('Quest Journal');
        await page.locator('#entries .entry').filter({hasText: /^○ Quest Journal$/}).click();
        await page.locator('#translation').fill('Journal des quêtes corrigé');
        await page.locator('#reviewed').check();
        await page.screenshot({path: path.join(output, 'translation-editor.png'), fullPage: true});
        const downloaded = page.waitForEvent('download'); await page.locator('#download').click();
        const file = await downloaded; const exported = path.join(output, 'frFR-edited.json'); await file.saveAs(exported);
        const pack = JSON.parse(fs.readFileSync(exported, 'utf8'));
        assert.equal(pack.strings['Quest Journal'], 'Journal des quêtes corrigé');
        assert.ok(pack.reviewed.includes('Quest Journal')); assert.ok(pack.strings['Store a skull']);
        assert.deepEqual(errors, []);
        console.log('PASS: actual browser import, search, editing, review flag, download and export contents.');
    } finally {await browser.close();}
})().catch(error => {console.error(error); process.exitCode = 1;});
