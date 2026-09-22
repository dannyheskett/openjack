// Capture the Google Play and App Store screenshots from the web build.
//
// openjack has ONE adaptive layout, so the web build renders what a phone, a
// tablet and an iPad render -- a headless browser at the right viewport size
// produces pixel-equivalent frames without a device or a farm. Both stores want
// phone and tablet sets, and this game ships in both orientations, so each slot
// gets its own folder and you upload whichever orientation you want it to show.
//
// Regenerate whenever the table UI changes. The committed PNGs under
// android/play-assets/screenshots/ and ios/app-store-assets/screenshots/ are what
// scripts/play_release.py and scripts/asc_release.py push to the stores.
//
// Usage:
//   npm i playwright-core                    # not a repo dependency; dev-only
//   make web   (or: gh release download release-N -p '*-web-wasm.zip' && unzip it)
//   node scripts/gen_store_screenshots.mjs --src build/web
//
// Options:
//   --src <dir>      web bundle directory (contains openjack.html) [required]
//   --out <dir>      repo root to write screenshots under          [default: cwd]
//   --chrome <path>  Chromium binary  [default: $CHROME, else the Playwright cache]
//   --only <name>    capture a single target (see TARGETS below)
//
// Why a browser and not the real app: the maintainer has no Android device and no
// Mac, and Device Farm returns video, not clean full-resolution stills.
//
// The deal is made repeatable by pinning the page clock. The game seeds its shoe
// from time() and rand() (src/main.c), and time() in the WebAssembly build is
// Date.now(); at SHOT_CLOCK the first hand is a pair of eights against a small
// dealer card, which shows a split. If the shuffle or the seeding ever changes,
// the script stops with an error rather than capturing some other hand; find a
// new clock value with the same property and put it here.

import { chromium } from 'playwright-core';
import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { existsSync, readdirSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { homedir } from 'node:os';

// ---------------------------------------------------------------------------
// Targets. Play's tablet slot takes the same frames for its 7-inch and 10-inch
// slots (see play-assets/LISTING.md). The App Store's 6.9" slot is the only
// iPhone size that covers every device; the 13" iPad slot covers every iPad.
// ---------------------------------------------------------------------------
const TARGETS = [
  { name: 'play-phone',            w: 1080, h: 1920, out: 'android/play-assets/screenshots/phone' },
  { name: 'play-phone-landscape',  w: 1920, h: 1080, out: 'android/play-assets/screenshots/phone-landscape' },
  { name: 'play-tablet',           w: 1600, h: 2560, out: 'android/play-assets/screenshots/tablet' },
  { name: 'play-tablet-landscape', w: 2560, h: 1600, out: 'android/play-assets/screenshots/tablet-landscape' },
  { name: 'ios-6.9',               w: 1290, h: 2796, out: 'ios/app-store-assets/screenshots/iphone-6.9' },
  { name: 'ios-6.9-landscape',     w: 2796, h: 1290, out: 'ios/app-store-assets/screenshots/iphone-6.9-landscape' },
  { name: 'ipad-13',               w: 2064, h: 2752, out: 'ios/app-store-assets/screenshots/ipad-13' },
  { name: 'ipad-13-landscape',     w: 2752, h: 2064, out: 'ios/app-store-assets/screenshots/ipad-13-landscape' },
];

// Seconds since the epoch the page clock is pinned to (see the header).
const SHOT_CLOCK = 1790000775;

// Colours from src/render.c, as read back from a screenshot.
const BTN_FILL = [22, 120, 72];   // an enabled button

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
               '.data': 'application/octet-stream', '.png': 'image/png' };

function arg(flag, fallback) {
  const i = process.argv.indexOf(flag);
  return i > -1 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

function findChrome() {
  const explicit = arg('--chrome', process.env.CHROME);
  if (explicit) return explicit;
  const cache = join(homedir(), '.cache/ms-playwright');
  if (!existsSync(cache)) return null;
  // Highest chromium-<rev> wins; Playwright keeps several revisions side by side.
  const dirs = readdirSync(cache)
    .filter(d => /^chromium-\d+$/.test(d))
    .sort((a, b) => +b.split('-')[1] - +a.split('-')[1]);
  for (const d of dirs) {
    for (const exe of ['chrome-linux64/chrome', 'chrome-linux/chrome', 'chrome-mac/Chromium.app/Contents/MacOS/Chromium']) {
      const p = join(cache, d, exe);
      if (existsSync(p)) return p;
    }
  }
  return null;
}

async function serve(dir) {
  const server = createServer(async (req, res) => {
    const rel = decodeURIComponent(req.url.split('?')[0]).replace(/^\/+/, '') || 'openjack.html';
    try {
      const body = await readFile(join(dir, rel));
      res.writeHead(200, { 'Content-Type': MIME[extname(rel)] || 'application/octet-stream' });
      res.end(body);
    } catch {
      res.writeHead(404).end('not found');
    }
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  return { server, port: server.address().port };
}

// Store listings reject screenshots with an alpha channel; the browser writes
// RGBA. Re-encode as plain RGB PNG (colour type 2) with zlib, no dependencies.
// The same decoder reads the board back from screenshots while playing.
import { deflateSync, inflateSync } from 'node:zlib';
function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) {
    c = (crc ^ buf[n]) & 0xff;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
// Decode an 8-bit RGB or RGBA PNG (as the browser writes) to RGB rows.
function decodePng(png) {
  let off = 8, w = 0, h = 0, ct = 0;
  const idat = [];
  while (off < png.length) {
    const len = png.readUInt32BE(off), type = png.toString('ascii', off + 4, off + 8);
    const data = png.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); ct = data[9]; }
    if (type === 'IDAT') idat.push(data);
    off += 12 + len;
  }
  if (ct !== 2 && ct !== 6) throw new Error(`unexpected PNG colour type ${ct}`);
  const bpp = ct === 6 ? 4 : 3, stride = w * bpp;
  const raw = inflateSync(Buffer.concat(idat));
  const rgb = Buffer.alloc(w * h * 3);
  const prev = Buffer.alloc(stride), cur = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)];
    raw.copy(cur, 0, y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {          // undo the PNG row filter
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = cur[i];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
                          v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[i] = v & 0xff;
    }
    for (let x = 0; x < w; x++) cur.copy(rgb, (y * w + x) * 3, x * bpp, x * bpp + 3);
    cur.copy(prev);
  }
  return { w, h, rgb };
}

function encodeRgbPng({ w, h, rgb }) {
  const out = Buffer.alloc(h * (1 + w * 3));
  for (let y = 0; y < h; y++) {
    out[y * (1 + w * 3)] = 0;
    rgb.copy(out, y * (1 + w * 3) + 1, y * w * 3, (y + 1) * w * 3);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
                        chunk('IDAT', deflateSync(out)), chunk('IEND', Buffer.alloc(0))]);
}

async function capture(target, url, chrome) {
  const { w, h, out } = target;
  const browser = await chromium.launch({
    executablePath: chrome,
    // SwiftShader: the runner has no GPU, and raylib needs a real WebGL context.
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
  });
  // hasTouch makes the page a touch device, so the build shows its touch UI
  // (no keyboard hints) and taps go through the same recognizer a phone uses.
  const ctx = await browser.newContext({
    viewport: { width: w, height: h }, deviceScaleFactor: 1, hasTouch: true,
  });
  await ctx.addInitScript(`Date.now = () => ${SHOT_CLOCK * 1000};`);
  const page = await ctx.newPage();
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForTimeout(w * h > 3000000 ? 9000 : 6000);   // WASM boot + font upload

  const wait = ms => page.waitForTimeout(ms);
  const { writeFile } = await import('node:fs/promises');
  const grab = async () => decodePng(await page.screenshot());
  const shot = async name => writeFile(join(out, `${name}.png`), encodeRgbPng(await grab()));

  // The input layer samples the pointer once a frame and decides a tap on
  // release, so every press settles first and is held across several frames.
  // SwiftShader draws a large frame slowly, so the timings scale with the view.
  const slow = w * h > 3000000;
  const T = { settle: slow ? 220 : 80, hold: slow ? 500 : 120, after: slow ? 900 : 450 };
  const near = (p, q, tol) => p.every((v, i) => Math.abs(v - q[i]) <= tol);

  // Every tap must change the picture; a tap that lands on bare felt is an
  // error here, not a silently wrong screenshot.
  async function tap(x, y, what) {
    const before = (await grab()).rgb;
    await page.mouse.move(x, y); await wait(T.settle);
    await page.mouse.down(); await wait(T.hold); await page.mouse.up(); await wait(T.after);
    const after = (await grab()).rgb;
    if (before.equals(after)) throw new Error(`${target.name}: tapping ${what} changed nothing`);
  }

  // The menu's highlighted row is the only yellow text on screen; its centre is
  // where New Game sits before anything is touched.
  async function selectedMenuRowY() {
    const img = await grab();
    const hits = [];
    for (let y = 0; y < img.h; y += 2) {
      for (let x = Math.round(w * 0.3); x < w * 0.7; x += 4) {
        const i = (y * img.w + x) * 3;
        const r = img.rgb[i], g = img.rgb[i + 1], b = img.rgb[i + 2];
        if (r > 210 && g > 180 && b < 150 && b > 50) { hits.push(y); break; }
      }
    }
    if (!hits.length) throw new Error(`${target.name}: no highlighted menu row`);
    return (Math.min(...hits) + Math.max(...hits)) / 2;
  }

  // The enabled buttons, read back out of the picture rather than computed from
  // the layout formula, in reading order (top to bottom, then left to right).
  // They are found as blobs of the button fill on a coarse grid.
  async function buttons() {
    const img = await grab();
    const step = 4;
    const gw = Math.ceil(img.w / step), gh = Math.ceil(img.h / step);
    const on = new Uint8Array(gw * gh);
    for (let gy = 0; gy < gh; gy++)
      for (let gx = 0; gx < gw; gx++) {
        const i = ((gy * step) * img.w + gx * step) * 3;
        if (near([img.rgb[i], img.rgb[i + 1], img.rgb[i + 2]], BTN_FILL, 6)) on[gy * gw + gx] = 1;
      }
    const seen = new Uint8Array(gw * gh), found = [];
    for (let s = 0; s < gw * gh; s++) {
      if (!on[s] || seen[s]) continue;
      let x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
      const stack = [s]; seen[s] = 1;
      while (stack.length) {
        const c = stack.pop(), cx = c % gw, cy = (c - cx) / gw;
        x0 = Math.min(x0, cx); x1 = Math.max(x1, cx); y0 = Math.min(y0, cy); y1 = Math.max(y1, cy);
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
          const nx = cx + dx, ny = cy + dy, n = ny * gw + nx;
          if (nx >= 0 && ny >= 0 && nx < gw && ny < gh && on[n] && !seen[n]) { seen[n] = 1; stack.push(n); }
        }
      }
      const b = { x: x0 * step, y: y0 * step, w: (x1 - x0 + 1) * step, h: (y1 - y0 + 1) * step };
      if (b.w > 24 && b.h > 24) found.push(b);   // not the rule under the title bar
    }
    found.sort((a, b) => (Math.abs(a.y - b.y) > a.h / 2 ? a.y - b.y : a.x - b.x));
    return found.map(b => ({ ...b, cx: b.x + b.w / 2, cy: b.y + b.h / 2 }));
  }

  // Dealt cards take a quarter of a second each to land, and the buttons only
  // appear once they have; wait for the expected number of buttons.
  async function waitButtons(n, what) {
    for (let i = 0; i < 40; i++) {
      const b = await buttons();
      if (b.length === n) return b;
      await wait(250);
    }
    throw new Error(`${target.name}: expected ${n} buttons ${what}, found ${(await buttons()).length}`);
  }

  await mkdir(out, { recursive: true });

  // 1. Title menu, untouched. Sound reads "Off" because it genuinely is off by
  //    default on every platform (src/sound.c) -- not a capture artifact.
  await shot('01-menu');

  await tap(w / 2, await selectedMenuRowY(), 'New Game');
  // The bet screen: - Deal +, with Deal the widest.
  let b = await waitButtons(3, 'on the bet screen');
  const deal = b.reduce((a, c) => (c.w > a.w ? c : a));
  await tap(deal.cx, deal.cy, 'Deal');

  // 2. A pair of eights: Hit, Stand, Double and Split all enabled.
  b = await waitButtons(4, 'after the deal (the pinned clock no longer deals a pair?)');
  await shot('02-deal');

  // Split, then stand on the first hand; the second is dealt its card.
  await tap(b[3].cx, b[3].cy, 'Split');
  b = await waitButtons(3, 'on the first split hand (Hit, Stand, Double)');
  await tap(b[1].cx, b[1].cy, 'Stand');
  b = await waitButtons(3, 'on the second split hand');
  // 3. Two hands, the second one in play.
  await shot('03-split');

  // Stand again: the dealer plays and the round settles.
  await tap(b[1].cx, b[1].cy, 'Stand');
  await waitButtons(1, 'after the round (Next Hand)');
  await wait(600);
  // 4. The result.
  await shot('04-result');

  await browser.close();
  console.log(`${target.name}: 4 frames -> ${out}`);
}

const src = arg('--src');
if (!src) { console.error('--src <web bundle dir> is required'); process.exit(2); }
const root = resolve(arg('--out', process.cwd()));
const chrome = findChrome();
if (!chrome) { console.error('no Chromium found; pass --chrome or set $CHROME'); process.exit(2); }

const { server, port } = await serve(resolve(src));
const url = `http://127.0.0.1:${port}/openjack.html`;
const only = arg('--only');

// A run can stall if a tap lands during an animation, so a target is worth
// replaying rather than failing the batch.
for (const t of TARGETS) {
  if (only && t.name !== only) continue;
  const target = { ...t, out: join(root, t.out) };
  for (let attempt = 1; ; attempt++) {
    try { await capture(target, url, chrome); break; }
    catch (e) {
      if (attempt === 3) throw e;
      console.log(`${t.name}: attempt ${attempt} failed (${e.message}); retrying`);
    }
  }
}
server.close();
