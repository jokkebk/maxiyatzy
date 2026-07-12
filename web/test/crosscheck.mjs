// Cross-check the JavaScript engine (web/engine.js + web/table.js) against the
// C++ Advisor (src/policy.cpp) that solved the table in the first place.
//
// engine.js is a hand port of policy.cpp, so a silent divergence in either is
// exactly the kind of bug this catches. We sample a few hundred (scorecard,
// upper-total) states, evaluate each turn in both engines, and diff the
// pre-roll value plus the keep and category rankings/EVs for random rolls.
//
//   node web/test/crosscheck.mjs [states] [rolls-per-state]
//
// Requires the decompressed value table and the built C++ helper:
//   zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o maxiyatzy-values.mytz
//   make build/maxiyatzy-crosscheck
// Override their locations with MYTZ_TABLE and CROSSCHECK_BIN if needed.
//
// Exit codes: 0 = engines agree, 1 = a mismatch was found, 2 = prerequisites
// (table or binary) are missing so the check could not run.

import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const webDir = path.resolve(here, '..');
const repoRoot = path.resolve(webDir, '..');

const STATES = Number(process.argv[2] || 300);
const ROLLS_PER_STATE = Number(process.argv[3] || 2);
const EV_TOL = 1e-3;           // float32 table reads are identical; only fp64
                               // rounding differs, so this stays comfortably tight

const tablePath = process.env.MYTZ_TABLE || path.join(repoRoot, 'maxiyatzy-values.mytz');
const binPath = process.env.CROSSCHECK_BIN || path.join(repoRoot, 'build', 'maxiyatzy-crosscheck');

if (!existsSync(tablePath)) {
  console.error(`SKIP: value table not found at ${tablePath}\n` +
    '  zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o maxiyatzy-values.mytz');
  process.exit(2);
}
if (!existsSync(binPath)) {
  console.error(`SKIP: cross-check binary not found at ${binPath}\n` +
    '  make build/maxiyatzy-crosscheck');
  process.exit(2);
}

// table.js speaks HTTP range requests; in Node we hand it the whole file once
// via a fetch shim that answers 200, which trips its full-download fallback.
const fileBytes = readFileSync(tablePath);
const fileBuffer = fileBytes.buffer.slice(
  fileBytes.byteOffset, fileBytes.byteOffset + fileBytes.byteLength);
globalThis.fetch = async () => ({
  status: 200,
  async arrayBuffer() { return fileBuffer; },
});

const { ValueTable, ensureTurn } =
  await import(pathToFileURL(path.join(webDir, 'table.js')).href);
const { evaluateTurn, diceId, CATEGORY_COUNT, FULL_MASK } =
  await import(pathToFileURL(path.join(webDir, 'engine.js')).href);

// deterministic PRNG so a failure reproduces
function mulberry32(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const rng = mulberry32(0x1234abcd);
const randInt = (lo, hi) => lo + Math.floor(rng() * (hi - lo + 1)); // inclusive

// an upper total reachable given which upper categories `mask` has filled
function sampleUpper(mask) {
  let upper = 0;
  for (let c = 0; c < 6; c++) if (mask & (1 << c)) upper += (c + 1) * randInt(0, 6);
  return upper;
}

function sampleRoll() {
  const counts = [0, 0, 0, 0, 0, 0];
  let faces = '';
  for (let i = 0; i < 6; i++) { const f = randInt(0, 5); counts[f]++; faces += (f + 1); }
  return { counts, faces };
}

const states = [];
for (let i = 0; i < STATES; i++) {
  const mask = randInt(0, FULL_MASK - 1);          // never the full board
  const rolls = [];
  for (let r = 0; r < ROLLS_PER_STATE; r++) rolls.push(sampleRoll());
  states.push({ mask, upper: sampleUpper(mask), rolls });
}

// One command line per query; the C++ helper answers one line each, in order.
const commands = [];
for (const s of states) {
  commands.push(`S ${s.mask} ${s.upper}`);
  for (const r of s.rolls) {
    commands.push(`K 2 ${r.faces}`);
    commands.push(`K 1 ${r.faces}`);
    commands.push(`C ${r.faces}`);
  }
}

function runHelper(lines) {
  return new Promise((resolve, reject) => {
    const child = spawn(binPath, [tablePath]);
    let out = '', err = '';
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', d => { out += d; });
    child.stderr.on('data', d => { err += d; });
    child.on('error', reject);
    child.on('close', code => {
      if (code !== 0) reject(new Error(`helper exited ${code}: ${err.trim()}`));
      else resolve(out.split('\n').filter(l => l.length > 0));
    });
    child.stdin.write(lines.join('\n') + '\n');
    child.stdin.end();
  });
}

const cppLines = await runHelper(commands);
if (cppLines.length !== commands.length) {
  console.error(`helper returned ${cppLines.length} lines, expected ${commands.length}`);
  process.exit(1);
}

// ---- comparison ----

let checks = 0, mismatches = 0;
let maxEvResidual = 0, worst = '';
let bestKeepAgree = 0, bestKeepTotal = 0;
let bestCatAgree = 0, bestCatTotal = 0;

function note(evDiff, label) {
  if (evDiff > maxEvResidual) { maxEvResidual = evDiff; worst = label; }
}
function fail(msg) {
  mismatches++;
  if (mismatches <= 20) console.error(`  MISMATCH: ${msg}`);
}

function parseKeeps(line) {
  const map = new Map();
  let best = null;
  for (const tok of line.split(' ')) {
    if (!tok) continue;
    const [counts, ev] = tok.split(':');
    const value = Number(ev);
    map.set(counts, value);
    if (!best || value > best.ev) best = { counts, ev: value };
  }
  return { map, best };
}

function parseCats(line) {
  const map = new Map();
  let best = null;
  for (const tok of line.split(' ')) {
    if (!tok) continue;
    const [cat, score, ev] = tok.split(',');
    const value = Number(ev);
    map.set(Number(cat), { score: Number(score), ev: value });
    if (!best || value > best.ev) best = { cat: Number(cat), ev: value };
  }
  return { map, best };
}

const table = new ValueTable(tablePath);
await table.init();

let idx = 0;
for (const s of states) {
  await ensureTurn(table, s.mask);
  const ev = evaluateTurn(table, s.mask, s.upper);

  const cppPreroll = Number(cppLines[idx++]);
  checks++;
  const prerollDiff = Math.abs(ev.preroll - cppPreroll);
  note(prerollDiff, `preroll mask=${s.mask} upper=${s.upper}`);
  if (prerollDiff > EV_TOL)
    fail(`preroll mask=${s.mask} upper=${s.upper}: js=${ev.preroll.toFixed(5)} cpp=${cppPreroll.toFixed(5)}`);

  for (const r of s.rolls) {
    const fullId = diceId(r.counts);

    for (const rerolls of [2, 1]) {
      const cpp = parseKeeps(cppLines[idx++]);
      const js = ev.keepOptions(fullId, rerolls, 462);
      const jsMap = new Map(js.map(o => [Array.from(o.counts).join(''), o.ev]));

      bestKeepTotal++;
      const jsBest = js[0];
      if (jsBest && cpp.best &&
          (jsBest.counts.join('') === cpp.best.counts ||
           Math.abs(jsBest.ev - cpp.best.ev) <= EV_TOL)) bestKeepAgree++;

      if (jsMap.size !== cpp.map.size)
        fail(`keep set size mask=${s.mask} roll=${r.faces} rr=${rerolls}: js=${jsMap.size} cpp=${cpp.map.size}`);
      for (const [key, jsEv] of jsMap) {
        checks++;
        const cppEv = cpp.map.get(key);
        if (cppEv === undefined) { fail(`keep ${key} missing in cpp (mask=${s.mask} roll=${r.faces})`); continue; }
        const diff = Math.abs(jsEv - cppEv);
        note(diff, `keep ${key} mask=${s.mask} rr=${rerolls}`);
        if (diff > EV_TOL)
          fail(`keep ${key} mask=${s.mask} roll=${r.faces} rr=${rerolls}: js=${jsEv.toFixed(5)} cpp=${cppEv.toFixed(5)}`);
      }
    }

    const cppCats = parseCats(cppLines[idx++]);
    const jsCats = ev.categoryOptions(fullId, CATEGORY_COUNT);
    const jsCatMap = new Map(jsCats.map(o => [o.cat, o]));

    bestCatTotal++;
    const jsBestCat = jsCats[0];
    if (jsBestCat && cppCats.best &&
        (jsBestCat.cat === cppCats.best.cat ||
         Math.abs(jsBestCat.ev - cppCats.best.ev) <= EV_TOL)) bestCatAgree++;

    if (jsCatMap.size !== cppCats.map.size)
      fail(`category set size mask=${s.mask} roll=${r.faces}: js=${jsCatMap.size} cpp=${cppCats.map.size}`);
    for (const [cat, jsOpt] of jsCatMap) {
      checks++;
      const cppOpt = cppCats.map.get(cat);
      if (cppOpt === undefined) { fail(`category ${cat} missing in cpp (mask=${s.mask} roll=${r.faces})`); continue; }
      if (jsOpt.score !== cppOpt.score)
        fail(`category ${cat} score mask=${s.mask} roll=${r.faces}: js=${jsOpt.score} cpp=${cppOpt.score}`);
      const diff = Math.abs(jsOpt.ev - cppOpt.ev);
      note(diff, `category ${cat} mask=${s.mask}`);
      if (diff > EV_TOL)
        fail(`category ${cat} ev mask=${s.mask} roll=${r.faces}: js=${jsOpt.ev.toFixed(5)} cpp=${cppOpt.ev.toFixed(5)}`);
    }
  }
}

console.log(`states=${states.length} rolls/state=${ROLLS_PER_STATE} EV comparisons=${checks}`);
console.log(`max EV residual = ${maxEvResidual.toExponential(3)} (${worst})`);
console.log(`top keep agrees ${bestKeepAgree}/${bestKeepTotal}, ` +
            `top category agrees ${bestCatAgree}/${bestCatTotal}`);
if (mismatches) {
  console.error(`FAIL: ${mismatches} mismatch(es) beyond tol ${EV_TOL}`);
  process.exit(1);
}
console.log('OK: JS and C++ engines agree');
