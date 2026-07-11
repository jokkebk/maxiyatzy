// Maxi Yatzy optimal-play engine: dice multiset tables, category scoring and
// one-turn evaluation against the solved value table. JavaScript port of
// src/policy.cpp; the numeric results are cross-checked against the C++
// advisor (see docs/verification-v1.md for the table itself).

export const CATEGORIES = [
  'ones', 'twos', 'threes', 'fours', 'fives', 'sixes',
  'one pair', 'two pairs', 'three pairs',
  'three of a kind', 'four of a kind', 'five of a kind',
  'small straight', 'large straight', 'full straight',
  'full house', 'super house', 'tower', 'chance', 'maxi yatzy',
];
export const CATEGORY_COUNT = 20;
export const FULL_MASK = (1 << 20) - 1;
export const BONUS_THRESHOLD = 75;
export const BONUS = 50;

// ---- dice multisets (counts per face, sizes 0..6) ----

function encode(counts) {
  let e = 0, m = 1;
  for (let f = 0; f < 6; f++) { e += counts[f] * m; m *= 7; }
  return e;
}

const dice = [];            // id -> Uint8Array(6)
const idOf = new Int16Array(117649).fill(-1);
const bySize = [[], [], [], [], [], [], []];

{
  const generate = (face, left, counts) => {
    if (face === 5) {
      counts[5] = left;
      dice.push(Uint8Array.from(counts));
      counts[5] = 0;
      return;
    }
    for (let n = 0; n <= left; n++) {
      counts[face] = n;
      generate(face + 1, left - n, counts);
    }
    counts[face] = 0;
  };
  const counts = [0, 0, 0, 0, 0, 0];
  for (let size = 0; size <= 6; size++) generate(0, size, counts);
  for (let id = 0; id < dice.length; id++) {
    idOf[encode(dice[id])] = id;
    bySize[dice[id].reduce((a, b) => a + b, 0)].push(id);
  }
}

const addFace = new Int16Array(dice.length * 6).fill(-1);
for (let id = 0; id < dice.length; id++) {
  const size = dice[id].reduce((a, b) => a + b, 0);
  if (size === 6) continue;
  for (let f = 0; f < 6; f++) {
    const next = Uint8Array.from(dice[id]);
    next[f]++;
    addFace[id * 6 + f] = idOf[encode(next)];
  }
}

function contains(outer, inner) {
  for (let f = 0; f < 6; f++) if (inner[f] > outer[f]) return false;
  return true;
}

const keepers = [];         // full id -> array of contained ids
const multiplicity = new Float64Array(dice.length);
{
  const fact = n => { let r = 1; for (let i = 2; i <= n; i++) r *= i; return r; };
  for (const full of bySize[6]) {
    let denom = 1;
    for (const c of dice[full]) denom *= fact(c);
    multiplicity[full] = 720 / denom;
    const list = [];
    for (let k = 0; k < dice.length; k++)
      if (contains(dice[full], dice[k])) list.push(k);
    keepers[full] = list;
  }
}

// ---- category scoring (port of src/scoring.cpp) ----

function bestNOfKind(c, needed) {
  for (let f = 5; f >= 0; f--) if (c[f] >= needed) return needed * (f + 1);
  return 0;
}

function bestPairs(c, needed) {
  let result = 0, found = 0;
  for (let f = 5; f >= 0 && found < needed; f--)
    if (c[f] >= 2) { result += 2 * (f + 1); found++; }
  return found === needed ? result : 0;
}

export function scoreCategory(cat, c) {
  if (cat < 6) return (cat + 1) * c[cat];
  switch (cat) {
    case 6: return bestPairs(c, 1);
    case 7: return bestPairs(c, 2);
    case 8: return bestPairs(c, 3);
    case 9: return bestNOfKind(c, 3);
    case 10: return bestNOfKind(c, 4);
    case 11: return bestNOfKind(c, 5);
    case 12: return c[0] >= 1 && c[1] >= 1 && c[2] >= 1 && c[3] >= 1 && c[4] >= 1 ? 15 : 0;
    case 13: return c[1] >= 1 && c[2] >= 1 && c[3] >= 1 && c[4] >= 1 && c[5] >= 1 ? 20 : 0;
    case 14: return c.every(n => n === 1) ? 21 : 0;
    case 15: {
      let best = 0;
      for (let t = 0; t < 6; t++) {
        if (c[t] < 3) continue;
        for (let p = 0; p < 6; p++)
          if (p !== t && c[p] >= 2) best = Math.max(best, 3 * (t + 1) + 2 * (p + 1));
      }
      return best;
    }
    case 16:
      for (let a = 0; a < 6; a++)
        for (let b = a + 1; b < 6; b++)
          if (c[a] === 3 && c[b] === 3) return 3 * (a + b + 2);
      return 0;
    case 17: {
      let best = 0;
      for (let q = 0; q < 6; q++) {
        if (c[q] < 4) continue;
        for (let p = 0; p < 6; p++)
          if (p !== q && c[p] >= 2) best = Math.max(best, 4 * (q + 1) + 2 * (p + 1));
      }
      return best;
    }
    case 18: return c.reduce((sum, n, f) => sum + n * (f + 1), 0);
    case 19: return c.some(n => n === 6) ? 100 : 0;
    default: return 0;
  }
}

const scores = [];          // cat -> Int16Array by dice id
for (let cat = 0; cat < CATEGORY_COUNT; cat++) {
  const s = new Int16Array(dice.length);
  for (const full of bySize[6]) s[full] = scoreCategory(cat, dice[full]);
  scores.push(s);
}

export function diceId(counts) { return idOf[encode(counts)]; }
export function diceCounts(id) { return dice[id]; }

// valid scores a category can take (for manual entry validation/chips)
export function validScores(cat) {
  const set = new Set([0]);
  for (const full of bySize[6]) set.add(scores[cat][full]);
  return [...set].sort((a, b) => a - b);
}

// ---- turn evaluation ----

function keeperExpectation(finals, k) {
  for (const full of bySize[6]) k[full] = finals[full];
  for (let size = 5; size >= 0; size--)
    for (const id of bySize[size]) {
      let t = 0;
      for (let f = 0; f < 6; f++) t += k[addFace[id * 6 + f]];
      k[id] = t / 6;
    }
}

// table: { value(mask, state), canonicalState(mask, upperTotal) } with the
// needed successor masks already cached (see table.js ensure()).
export function evaluateTurn(table, mask, upperTotal) {
  const open = [];
  const cont = [];
  for (let cat = 0; cat < CATEGORY_COUNT; cat++) {
    open.push(!(mask & (1 << cat)));
    if (!open[cat]) { cont.push(null); continue; }
    const nextMask = mask | (1 << cat);
    const row = new Float64Array(7);
    if (cat < 6) {
      for (let n = 0; n <= 6; n++) {
        const nt = upperTotal + (cat + 1) * n;
        const bonus = upperTotal < BONUS_THRESHOLD && nt >= BONUS_THRESHOLD ? BONUS : 0;
        row[n] = bonus + table.valueFromTotal(nextMask, nt);
      }
    } else {
      row.fill(table.valueFromTotal(nextMask, upperTotal));
    }
    cont.push(row);
  }

  const f3 = new Float64Array(dice.length);
  for (const full of bySize[6]) {
    let best = -Infinity;
    for (let cat = 0; cat < CATEGORY_COUNT; cat++) {
      if (!open[cat]) continue;
      const n = cat < 6 ? dice[full][cat] : 0;
      const v = scores[cat][full] + cont[cat][n];
      if (v > best) best = v;
    }
    f3[full] = best;
  }
  const k2 = new Float64Array(dice.length);
  keeperExpectation(f3, k2);
  const f2 = new Float64Array(dice.length);
  for (const full of bySize[6]) {
    let best = -Infinity;
    for (const held of keepers[full]) best = Math.max(best, k2[held]);
    f2[full] = best;
  }
  const k1 = new Float64Array(dice.length);
  keeperExpectation(f2, k1);
  let preroll = 0;
  for (const full of bySize[6]) {
    let best = -Infinity;
    for (const held of keepers[full]) best = Math.max(best, k1[held]);
    preroll += multiplicity[full] * best;
  }
  preroll /= 46656;

  return {
    preroll,
    // EV of holding exactly `keepId` with `rerollsLeft` rerolls remaining
    keepValue(keepId, rerollsLeft) {
      return rerollsLeft === 2 ? k1[keepId] : k2[keepId];
    },
    keepOptions(fullId, rerollsLeft, topK) {
      const k = rerollsLeft === 2 ? k1 : k2;
      const options = keepers[fullId].map(id => ({ id, counts: dice[id], ev: k[id] }));
      options.sort((a, b) => b.ev - a.ev);
      return options.slice(0, topK);
    },
    categoryOptions(fullId, topK) {
      const options = [];
      for (let cat = 0; cat < CATEGORY_COUNT; cat++) {
        if (!open[cat]) continue;
        const n = cat < 6 ? dice[fullId][cat] : 0;
        options.push({ cat, score: scores[cat][fullId], ev: scores[cat][fullId] + cont[cat][n] });
      }
      options.sort((a, b) => b.ev - a.ev);
      return options.slice(0, topK);
    },
  };
}
