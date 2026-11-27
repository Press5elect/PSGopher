// PSGopher - the Live Board's static mainboard as a picture for the console's front end.
//
//   NODE_PATH=$(npm root -g) node ps5/tools/make-board-art.mjs   (Playwright; build-deps.sh's fonts)
//
// ps5/art/live-board.html is the front end's design (the prototype the owner chose). Its board is a
// flat 2000x1250 canvas drawn by its script (drawStill: the board, its chips, traces and silkscreen),
// tilted by CSS. This renders that canvas at twice its size into ps5/art/board.png; the console
// draws it tilted itself (ps5/host/board.cpp) and draws the moving parts over it (the signals, the
// meters, the lights, the cartridge), as the prototype's drawDynamic does. The silkscreen is in
// JetBrains Mono, from the fetched files, so the picture is the same wherever it is made.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';

// Playwright as CommonJS resolves it (NODE_PATH, a global install)
const { chromium } = createRequire(import.meta.url)('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const SCALE = 2;
const fonts = join(root, '.deps/src/jetbrains-mono/fonts/ttf');
let html = readFileSync(join(root, 'ps5/art/live-board.html'), 'utf8');
const hook = "const still = document.createElement('canvas'); still.width = W; still.height = H; const sx = still.getContext('2d');";
if (!html.includes(hook)) throw new Error('live-board.html: the still canvas line changed');
html = html.replace(hook, `const still = document.createElement('canvas'); still.width = W * ${SCALE}; still.height = H * ${SCALE}; const sx = still.getContext('2d'); sx.scale(${SCALE}, ${SCALE}); window.__still = still; window.addEventListener('psgopher-redraw', () => drawStill());`);
// the fetched fonts in place of Google's
const face = (weight, file) => `@font-face{font-family:"JetBrains Mono";font-weight:${weight};src:url(data:font/ttf;base64,${readFileSync(join(fonts, file)).toString('base64')}) format("truetype")}`;
const google = /<link[^>]+fonts\.googleapis[^>]*>/;
if (!google.test(html)) throw new Error('live-board.html: the fonts link changed');
html = html.replace(google, `<style>${face(400, 'JetBrainsMono-Regular.ttf')}${face(600, 'JetBrainsMono-SemiBold.ttf')}${face(800, 'JetBrainsMono-ExtraBold.ttf')}</style>`);
const page_file = join(tmpdir(), 'psgopher-live-board-art.html');
// (the page's text is UTF-8: said first, as a file:// page has no header to say it)
writeFileSync(page_file, '<meta charset="utf-8">' + html);

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
await page.route(/fonts\.(googleapis|gstatic)\.com/, (r) => r.abort());
await page.goto('file://' + page_file);
await page.waitForFunction(() => window.__still && document.fonts.status === 'loaded', null, { timeout: 20000 });
await page.waitForTimeout(800);
const used = await page.evaluate(async () => {
  await document.fonts.load('600 16px "JetBrains Mono"');
  await document.fonts.load('800 16px "JetBrains Mono"');
  return [...document.fonts].filter((f) => f.status === 'loaded').map((f) => f.family + ' ' + f.weight);
});
if (!used.some((f) => f.includes('JetBrains Mono'))) throw new Error('JetBrains Mono did not load: ' + used.join(', '));
// drawn again now the faces are certainly there
await page.evaluate(() => window.dispatchEvent(new Event('psgopher-redraw')));
await page.waitForTimeout(300);
const data = await page.evaluate(() => window.__still.toDataURL('image/png'));
await browser.close();
mkdirSync(join(root, 'ps5/art'), { recursive: true });
const out = join(root, 'ps5/art/board.png');
writeFileSync(out, Buffer.from(data.split(',')[1], 'base64'));
console.log(`==> ${out} (${2000 * SCALE}x${1250 * SCALE}; fonts: ${used.join(', ')})`);
