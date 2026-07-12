// Maxi Yatzy scorekeeper with optional optimal-play hints.
// Scorekeeping works standalone; hints stream value-table slices from
// maxiyatzy-values.mytz next to this file via HTTP range requests.

import {
  CATEGORY_COUNT, diceId, evaluateTurn, validScores,
  BONUS_THRESHOLD, BONUS,
} from './engine.js';
import { ValueTable, ensureTurn } from './table.js';

const FI = [
  'Ykköset', 'Kakkoset', 'Kolmoset', 'Neloset', 'Viitoset', 'Kuutoset',
  '1 pari', '2 paria', '3 paria', 'Kolmiluku', 'Neliluku', 'Viisiluku',
  'Pieni suora', 'Iso suora', 'Täyssuora', 'Täyskäsi', 'Superkäsi',
  'Torni', 'Sattuma', 'Maxi-Yatzy',
];

const ROW_DICE = [
  [1,1,1,1,1,1], [2,2,2,2,2,2], [3,3,3,3,3,3], [4,4,4,4,4,4], [5,5,5,5,5,5], [6,6,6,6,6,6],
  [6,6], [6,6,5,5], [6,6,5,5,4,4], [6,6,6], [6,6,6,6], [6,6,6,6,6],
  [1,2,3,4,5], [2,3,4,5,6], [1,2,3,4,5,6], [2,2,6,6,6], [3,3,3,5,5,5], [1,1,4,4,4,4],
  null,   // Sattuma: question marks
  null,   // Maxi-Yatzy: logo
];

const STORAGE_KEY = 'maxiyatzy.game.v1';
const TABLE_URL = 'maxiyatzy-values.mytz';

let game = null;      // { players: [{name, assist, card[20]}] }
let undoStack = [];
let assist = null;    // transient per-turn assistant state
let table = null;
let tablePromise = null;

// ---------- dice rendering ----------

const PIPS = {
  1: [[8, 8]],
  2: [[4.5, 4.5], [11.5, 11.5]],
  3: [[4, 4], [8, 8], [12, 12]],
  4: [[4.5, 4.5], [11.5, 4.5], [4.5, 11.5], [11.5, 11.5]],
  5: [[4, 4], [12, 4], [8, 8], [4, 12], [12, 12]],
  6: [[4.5, 3.8], [11.5, 3.8], [4.5, 8], [11.5, 8], [4.5, 12.2], [11.5, 12.2]],
};

function dieSvg(face, size, cls = '') {
  const pips = PIPS[face].map(([x, y]) =>
    `<circle cx="${x}" cy="${y}" r="1.75" fill="currentColor"/>`).join('');
  return `<svg class="die-svg ${cls}" width="${size}" height="${size}" viewBox="0 0 16 16">` +
    `<rect x="0.75" y="0.75" width="14.5" height="14.5" rx="3" fill="none" ` +
    `stroke="currentColor" stroke-width="1.3"/>${pips}</svg>`;
}

function questionSvg(size) {
  return `<svg class="die-svg" width="${size}" height="${size}" viewBox="0 0 16 16">` +
    `<rect x="0.75" y="0.75" width="14.5" height="14.5" rx="3" fill="none" ` +
    `stroke="currentColor" stroke-width="1.3"/>` +
    `<text x="8" y="12" text-anchor="middle" font-size="10.5" font-family="inherit" ` +
    `fill="currentColor">?</text></svg>`;
}

function diceRow(cat) {
  if (cat === 19) return `<span class="logo" style="font-size:0.95rem">maxi<span style="font-size:1.1rem">Yatzy</span></span>`;
  const faces = ROW_DICE[cat];
  const size = 17;
  if (!faces) return `<span class="dice-row">${questionSvg(size).repeat(6)}</span>`;
  // cluster consecutive equal faces so combos read as distinct groups
  // (full house = 3+2, two pairs = 2+2, …); straights stay one flat run.
  const groups = [];
  for (const f of faces) {
    const last = groups[groups.length - 1];
    if (last && last[0] === f) last.push(f);
    else groups.push([f]);
  }
  const clustered = groups.length > 1 && groups.some(g => g.length > 1);
  if (!clustered)
    return `<span class="dice-row">${faces.map(f => dieSvg(f, size)).join('')}</span>`;
  return `<span class="dice-row grouped">${groups.map(g =>
    `<span class="dice-group">${g.map(f => dieSvg(f, size)).join('')}</span>`).join('')}</span>`;
}

function diceText(counts) {
  const out = [];
  for (let f = 0; f < 6; f++) for (let i = 0; i < counts[f]; i++) out.push(f + 1);
  return out.join(' ');
}

// small dice-row illustration for a hint option: the whole rolled hand with
// that option's kept dice highlighted, so a suggestion reads as a picture
// instead of a run of digits.
function miniHandRow(counts, keepCounts, size) {
  const marked = [...keepCounts];
  let html = '';
  for (let f = 0; f < 6; f++)
    for (let i = 0; i < counts[f]; i++) {
      const kept = marked[f] > i;
      html += dieSvg(f + 1, size, `mini-die${kept ? ' kept' : ''}`);
    }
  return `<span class="mini-hand">${html}</span>`;
}

// ---------- game state helpers ----------

function maskOf(card) {
  let mask = 0;
  for (let c = 0; c < CATEGORY_COUNT; c++) if (card[c] !== null) mask |= 1 << c;
  return mask;
}
const upperTotalOf = card => card.slice(0, 6).reduce((a, b) => a + (b ?? 0), 0);
const lowerTotalOf = card => card.slice(6).reduce((a, b) => a + (b ?? 0), 0);
const bonusOf = card => upperTotalOf(card) >= BONUS_THRESHOLD ? BONUS : 0;
const grandTotalOf = card => upperTotalOf(card) + bonusOf(card) + lowerTotalOf(card);
const filledCount = card => card.filter(v => v !== null).length;

function activePlayerIndex() {
  let min = CATEGORY_COUNT + 1, index = -1;
  game.players.forEach((p, i) => {
    const n = filledCount(p.card);
    if (n < min) { min = n; index = i; }
  });
  return min >= CATEGORY_COUNT ? -1 : index;
}

// per-player play modes, cycled by the header/setup buttons
const MODES = ['pen', 'assist', 'peek'];
const MODE_LABEL = { pen: 'kynä', assist: '✨ apuri', peek: '👁 kurkista' };
const nextMode = m => MODES[(MODES.indexOf(m) + 1) % MODES.length];
// tolerate the older { assist: bool } shape saved before modes existed
const normalizeMode = p => p.mode ?? (p.assist ? 'assist' : 'pen');

function save() {
  localStorage.setItem(STORAGE_KEY,
    JSON.stringify({ players: game.players, undo: undoStack }));
}

function load() {
  try {
    const data = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (data && Array.isArray(data.players) && data.players.length) {
      data.players.forEach(p => { p.mode = normalizeMode(p); delete p.assist; });
      data.undo = Array.isArray(data.undo) ? data.undo : [];
      return data;
    }
  } catch { /* corrupt storage: start fresh */ }
  return null;
}

function pushUndo() {
  undoStack.push(JSON.stringify(game.players));
  if (undoStack.length > 40) undoStack.shift();
}

async function getTable() {
  if (table) return table;
  if (!tablePromise) {
    tablePromise = (async () => {
      const t = new ValueTable(TABLE_URL);
      await t.init();
      table = t;
      return t;
    })().catch(err => { tablePromise = null; throw err; });
  }
  return tablePromise;
}

// ---------- scorecard rendering ----------

const $ = id => document.getElementById(id);

function cellScore(value) {
  if (value === null) return '';
  return `<span class="score${value === 0 ? ' zero' : ''}">${value === 0 ? '—' : value}</span>`;
}

function render() {
  const card = $('card');
  const n = game.players.length;
  const active = activePlayerIndex();
  const over = active === -1;
  const totals = game.players.map(p => grandTotalOf(p.card));
  const best = Math.max(...totals);

  let html = `<div class="card-logo"><span class="logo">maxi<span>Yatzy</span></span></div>`;
  html += `<div class="grid" style="grid-template-columns:minmax(8rem,2.2fr) repeat(${n},minmax(2.4rem,1fr))">`;

  html += `<div class="cell head"></div>`;
  game.players.forEach((p, i) => {
    html += `<div class="cell head mode-${p.mode}${i === active ? ' col-active' : ''}">
      <span class="name">${escapeHtml(p.name)}</span>
      <button class="mode-btn" data-player="${i}" data-action="cycle-mode">${MODE_LABEL[p.mode]}</button></div>`;
  });

  const row = (cat) => {
    html += `<div class="cell label">${diceRow(cat)}<span class="caption">${FI[cat]}</span></div>`;
    game.players.forEach((p, i) => {
      const value = p.card[cat];
      const open = value === null;
      html += `<div class="cell${open ? ' open' : ''}${i === active ? ' col-active' : ''}"
        ${open ? `data-player="${i}" data-cat="${cat}" data-action="enter"` : ''}>
        ${cellScore(value)}</div>`;
    });
  };

  for (let cat = 0; cat < 6; cat++) row(cat);

  html += `<div class="rule-row" style="display:contents">
    <div class="cell rule-label" style="justify-content:flex-start;padding-left:0.2rem">Yhteensä</div>`;
  game.players.forEach((p, i) => {
    html += `<div class="cell${i === active ? ' col-active' : ''}"><span class="total-value">${upperTotalOf(p.card)}</span></div>`;
  });
  html += `</div>`;

  html += `<div class="cell label"><span class="caption" style="font-size:0.78rem;letter-spacing:0.14em">Bonus +50</span></div>`;
  game.players.forEach((p, i) => {
    const upperDone = p.card.slice(0, 6).every(v => v !== null);
    const mark = bonusOf(p.card) ? '50' : (upperDone ? '—' : '');
    html += `<div class="cell${i === active ? ' col-active' : ''}"><span class="bonus-mark">${mark}</span></div>`;
  });

  for (let cat = 6; cat < CATEGORY_COUNT; cat++) row(cat);

  html += `<div class="rule-row grand" style="display:contents">
    <div class="cell rule-label" style="justify-content:flex-start;padding-left:0.2rem">Yhteensä</div>`;
  game.players.forEach((p, i) => {
    const win = over && totals[i] === best && n > 1;
    html += `<div class="cell"><span class="total-value${win ? ' winner' : ''}">${totals[i]}</span></div>`;
  });
  html += `</div></div>`;

  card.innerHTML = html;
  $('undo-button').hidden = undoStack.length === 0;
  $('add-player-button').hidden = n >= 6;

  const bar = $('action-bar');
  if (!over && game.players[active].mode !== 'pen') {
    bar.hidden = false;
    $('assist-button').textContent = `🎲 ${game.players[active].name} — syötä nopat`;
    $('assist-button').dataset.player = active;
  } else {
    bar.hidden = true;
  }
}

function escapeHtml(text) {
  return text.replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  })[c]);
}

// ---------- manual score entry ----------

let manual = null; // {player, cat, typed}

function openManualEntry(playerIndex, cat) {
  manual = { player: playerIndex, cat, typed: '' };
  renderManualSheet();
  showSheet();
}

function renderManualSheet() {
  const p = game.players[manual.player];
  const values = validScores(manual.cat);
  const chips = values.length <= 10
    ? `<div class="chips">${values.map(v =>
        `<button data-action="chip" data-value="${v}">${v === 0 ? '—' : v}</button>`).join('')}</div>`
    : '';
  $('sheet').innerHTML = `
    <div class="sheet-title"><span class="who">${escapeHtml(p.name)}</span> ${FI[manual.cat]}</div>
    ${chips}
    <div class="entry-display">${manual.typed || '&nbsp;'}</div>
    <div class="numpad">
      ${[1,2,3,4,5,6,7,8,9].map(d => `<button data-action="digit" data-digit="${d}">${d}</button>`).join('')}
      <button data-action="digit" data-digit="0">0</button>
      <button data-action="backspace">⌫</button>
      <button data-action="cross">✗ = 0</button>
    </div>
    <div class="sheet-actions">
      <button class="secondary" data-action="cancel">Peruuta</button>
      <button data-action="confirm-manual">Merkitse</button>
    </div>`;
}

function manualAction(action, target) {
  if (action === 'digit') {
    if (manual.typed.length < 3) manual.typed += target.dataset.digit;
    renderManualSheet();
  } else if (action === 'backspace') {
    manual.typed = manual.typed.slice(0, -1);
    renderManualSheet();
  } else if (action === 'cross') {
    fillScore(manual.player, manual.cat, 0);
  } else if (action === 'chip') {
    fillScore(manual.player, manual.cat, Number(target.dataset.value));
  } else if (action === 'confirm-manual') {
    if (manual.typed === '') return;
    fillScore(manual.player, manual.cat, Number(manual.typed));
  }
}

// ---------- assisted turn ----------

async function startAssistTurn(playerIndex) {
  const p = game.players[playerIndex];
  const mask = maskOf(p.card);
  assist = { player: playerIndex, phase: 'loading' };
  $('sheet').innerHTML = `<div class="sheet-title"><span class="who">${escapeHtml(p.name)}</span></div>
    <p class="sheet-sub">Ladataan apuria…</p>`;
  showSheet();
  try {
    const t = await getTable();
    await ensureTurn(t, mask);
    if (!assist || assist.phase !== 'loading') return;
    assist = {
      player: playerIndex,
      peek: p.mode === 'peek',
      revealed: false,
      eval: evaluateTurn(t, mask, upperTotalOf(p.card)),
      rerollsLeft: 2,
      kept: [0, 0, 0, 0, 0, 0],
      typed: [],
      phase: 'dice',
    };
    renderAssistSheet();
  } catch (err) {
    if (!assist) return;
    $('sheet').innerHTML = `<div class="sheet-title">Apuri ei käytettävissä</div>
      <p class="sheet-sub">${escapeHtml(String(err.message || err))} —
      tarkista että ${TABLE_URL} on ladattavissa sivun vierestä.</p>
      <div class="sheet-actions"><button data-action="cancel">Sulje</button></div>`;
  }
}

const keptCount = () => assist.kept.reduce((a, b) => a + b, 0);

function rollCounts() {
  const counts = [...assist.kept];
  for (const f of assist.typed) counts[f]++;
  return counts;
}

function renderAssistSheet() {
  const p = game.players[assist.player];
  const expected = grandTotalOf(p.card) + assist.eval.preroll;
  const head = `<div class="sheet-title"><span class="who">${escapeHtml(p.name)}</span>
    heitto ${3 - assist.rerollsLeft}/3 <span class="ev" style="color:var(--faint);font-size:0.8rem">
    · odotettu lopputulos ${expected.toFixed(0)}</span></div>`;

  if (assist.phase === 'dice') {
    const need = 6 - keptCount();
    let diceHtml = '';
    for (let f = 0; f < 6; f++)
      for (let i = 0; i < assist.kept[f]; i++) diceHtml += dieSvg(f + 1, 42, 'big-die kept');
    for (const f of assist.typed) diceHtml += dieSvg(f + 1, 42, 'big-die');
    for (let i = assist.typed.length; i < need; i++)
      diceHtml += `<svg class="die-svg big-die pending" width="42" height="42" viewBox="0 0 16 16">
        <rect x="0.75" y="0.75" width="14.5" height="14.5" rx="3" fill="none"
        stroke="currentColor" stroke-width="1.3" stroke-dasharray="2.5 2"/></svg>`;
    $('sheet').innerHTML = `${head}
      <p class="sheet-sub">${keptCount() ? `pidossa ${diceText(assist.kept)} — ` : ''}syötä ${need} noppaa</p>
      <div class="dice-line">${diceHtml}</div>
      <div class="keypad">
        ${[1,2,3,4,5,6].map(f => `<button data-action="die" data-face="${f}">${f}</button>`).join('')}
      </div>
      <div class="keypad-wide">
        <button class="secondary" data-action="die-backspace">⌫</button>
        <button class="secondary" data-action="cancel">Peruuta</button>
      </div>`;
    return;
  }

  if (assist.phase === 'hint') {
    const counts = rollCounts();
    const reveal = !assist.peek || assist.revealed;

    // lay the roll out sorted; mark the first selection[f] dice of each face kept
    let diceHtml = '';
    const marked = [...assist.selection];
    for (let f = 0; f < 6; f++)
      for (let i = 0; i < counts[f]; i++) {
        const kept = marked[f] > i;
        diceHtml += `<span data-action="toggle-die" data-face="${f + 1}" data-kept="${kept ? 1 : 0}">
          ${dieSvg(f + 1, 42, `big-die${kept ? ' kept' : ''}`)}</span>`;
      }

    let hintHtml;
    if (reveal) {
      const fullId = diceId(counts);
      const options = assist.eval.keepOptions(fullId, assist.rerollsLeft, 3);
      const bestEv = options[0].ev;
      const selId = diceId(assist.selection);
      const selEv = assist.eval.keepValue(selId, assist.rerollsLeft);
      const selIsBest = selId === options[0].id;
      const expected = grandTotalOf(game.players[assist.player].card);
      const isStop = o => o.counts.reduce((a, b) => a + b, 0) === 6;

      const bestHtml = `<button class="hint-line hint-best hint-option"
          data-action="apply-option" data-counts="${options[0].counts.join(',')}">
          ${miniHandRow(counts, options[0].counts, 26)}
          <span class="hint-label">✨ ${isStop(options[0]) ? 'pysähdy tähän' : 'paras valinta'}</span>
          <span class="ev">EV ${(expected + bestEv).toFixed(1)}</span></button>`;
      const altHtml = options.slice(1).map(o => `<button class="hint-line hint-option"
          data-action="apply-option" data-counts="${o.counts.join(',')}">
          ${miniHandRow(counts, o.counts, 22)}
          ${isStop(o) ? '<span class="hint-label">pysähdy tähän</span>' : ''}
          <span class="ev">${(o.ev - bestEv).toFixed(2)}</span></button>`).join('');
      const selHtml = selIsBest ? '' : `<div class="hint-line">
          ${miniHandRow(counts, assist.selection, 22)}
          <span class="hint-label">valintasi</span>
          <span class="ev">${(selEv - bestEv).toFixed(2)}</span></div>`;
      hintHtml = `${bestHtml}${altHtml}${selHtml}`;
    } else {
      hintHtml = `<div class="hint-line hint-hidden">napauta pidettävät nopat itse</div>
        <button class="secondary peek-btn" data-action="reveal">👁 kurkista vinkki</button>`;
    }

    $('sheet').innerHTML = `${head}
      <div class="dice-line">${diceHtml}</div>
      ${hintHtml}
      <div class="sheet-actions">
        <button class="secondary" data-action="stop-here">Pysähdyn</button>
        <button data-action="reroll">Heitän loput</button>
      </div>`;
    return;
  }

  if (assist.phase === 'category') {
    const counts = rollCounts();
    const fullId = diceId(counts);
    const options = assist.eval.categoryOptions(fullId, CATEGORY_COUNT);
    const bestEv = options[0].ev;
    const bestCat = options[0].cat;
    const reveal = !assist.peek || assist.revealed;
    let diceHtml = '';
    for (let f = 0; f < 6; f++)
      for (let i = 0; i < counts[f]; i++) diceHtml += dieSvg(f + 1, 36, 'big-die');
    // ranked with EV deltas when revealed; plain card order while hidden
    const list = reveal ? options : [...options].sort((a, b) => a.cat - b.cat);
    const buttons = list.map(o => {
      const isBest = reveal && o.cat === bestCat;
      const delta = !reveal ? '' : (isBest ? '✨ paras' : (o.ev - bestEv).toFixed(2));
      return `<button data-action="fill" data-cat="${o.cat}" data-score="${o.score}"
        class="${isBest ? 'best' : ''}">
        <span class="opt-score">${o.score === 0 ? '—' : o.score}</span>
        <span class="opt-name">${FI[o.cat]}</span>
        <span class="opt-delta">${delta}</span>
      </button>`;
    }).join('');
    const peekBtn = reveal ? ''
      : `<button class="secondary peek-btn" data-action="reveal">👁 kurkista vinkki</button>`;
    $('sheet').innerHTML = `${head}
      <div class="dice-line">${diceHtml}</div>
      ${peekBtn}
      <div class="option-list">${buttons}</div>
      <div class="sheet-actions"><button class="secondary" data-action="cancel">Peruuta</button></div>`;
  }
}

function assistAction(action, target) {
  if (action === 'die') {
    if (assist.phase !== 'dice') return;
    if (assist.typed.length < 6 - keptCount()) {
      assist.typed.push(Number(target.dataset.face) - 1);
      if (assist.typed.length === 6 - keptCount()) {
        assist.revealed = false;
        if (assist.rerollsLeft === 0) {
          assist.phase = 'category';
        } else {
          assist.phase = 'hint';
          if (assist.peek) {
            // start from what the player already holds; don't preselect the optimum
            assist.selection = [...assist.kept];
          } else {
            const options = assist.eval.keepOptions(diceId(rollCounts()), assist.rerollsLeft, 1);
            assist.selection = [...options[0].counts];
          }
        }
      }
      renderAssistSheet();
    }
  } else if (action === 'die-backspace') {
    if (assist.typed.length) assist.typed.pop();
    else { hideSheet(); assist = null; return; }
    renderAssistSheet();
  } else if (action === 'toggle-die') {
    const f = Number(target.dataset.face) - 1;
    const kept = target.dataset.kept === '1';
    if (kept) assist.selection[f]--;
    else if (assist.selection[f] < rollCounts()[f]) assist.selection[f]++;
    renderAssistSheet();
  } else if (action === 'apply-option') {
    assist.selection = target.dataset.counts.split(',').map(Number);
    assistAction('reroll', target);
  } else if (action === 'reroll') {
    const total = assist.selection.reduce((a, b) => a + b, 0);
    if (total === 6) { assist.phase = 'category'; assist.revealed = false; renderAssistSheet(); return; }
    assist.kept = [...assist.selection];
    assist.typed = [];
    assist.rerollsLeft--;
    assist.phase = 'dice';
    renderAssistSheet();
  } else if (action === 'stop-here') {
    assist.phase = 'category';
    assist.revealed = false;
    renderAssistSheet();
  } else if (action === 'reveal') {
    assist.revealed = true;
    renderAssistSheet();
  } else if (action === 'fill') {
    fillScore(assist.player, Number(target.dataset.cat), Number(target.dataset.score));
  }
}

// ---------- shared ----------

function fillScore(playerIndex, cat, score) {
  pushUndo();
  game.players[playerIndex].card[cat] = score;
  save();
  hideSheet();
  manual = null;
  assist = null;
  render();
}

function showSheet() {
  $('sheet').hidden = false;
  $('sheet-backdrop').hidden = false;
}

function hideSheet() {
  $('sheet').hidden = true;
  $('sheet-backdrop').hidden = true;
}

// ---------- setup ----------

// keepScores: adding players mid-game — carry each existing card through the
// setup dataset so confirming preserves scores instead of starting fresh.
function openSetup({ keepScores = false } = {}) {
  let players;
  if (keepScores && game) {
    players = game.players.map(p => ({ name: p.name, mode: normalizeMode(p), card: p.card }));
    players.push({ name: '', mode: 'pen' });   // ready to fill in the newcomer
  } else {
    const previous = game ? game.players : (load()?.players ?? []);
    players = previous.length
      ? previous.map(p => ({ name: p.name, mode: normalizeMode(p) }))
      : [{ name: '', mode: 'pen' }, { name: '', mode: 'pen' }];
  }
  renderSetup(players);
  $('setup-title').textContent = keepScores ? 'Lisää pelaaja' : 'Pelaajat';
  $('start-game').textContent = keepScores ? 'Jatka' : 'Aloita peli';
  $('setup').hidden = false;
}

function renderSetup(players) {
  $('setup-players').innerHTML = players.map((p, i) => `
    <div class="setup-row">
      <input type="text" placeholder="Pelaaja ${i + 1}" value="${escapeHtml(p.name)}" data-index="${i}">
      <button class="assist-toggle mode-${p.mode}${p.mode === 'pen' ? '' : ' on'}"
        data-action="setup-mode" data-index="${i}">${MODE_LABEL[p.mode]}</button>
      ${players.length > 1 ? `<button class="remove" data-action="setup-remove" data-index="${i}">✕</button>` : ''}
    </div>`).join('');
  $('setup').dataset.players = JSON.stringify(players);
}

function setupPlayers() {
  const players = JSON.parse($('setup').dataset.players);
  document.querySelectorAll('#setup-players input').forEach(input => {
    players[Number(input.dataset.index)].name = input.value;
  });
  return players;
}

async function probeTable() {
  const status = $('table-status');
  try {
    const t = await getTable();
    await t.ensure([0]);
    status.textContent = `✨ apuri valmiina · optimipelin odotusarvo ${t.valueFromTotal(0, 0).toFixed(1)}` +
      (t.rangeSupported ? '' : ' (koko taulukko ladattu)');
  } catch {
    status.classList.add('error');
    status.textContent = `apuri ei saatavilla: ${TABLE_URL} puuttuu — pisteet voi silti kirjata`;
  }
}

// ---------- events ----------

document.addEventListener('click', event => {
  const target = event.target.closest('[data-action]');
  if (!target) return;
  const action = target.dataset.action;

  if (action === 'enter') {
    openManualEntry(Number(target.dataset.player), Number(target.dataset.cat));
  } else if (action === 'cycle-mode') {
    const p = game.players[Number(target.dataset.player)];
    p.mode = nextMode(p.mode);
    save();
    render();
  } else if (action === 'cancel') {
    hideSheet();
    manual = null;
    assist = null;
  } else if (manual) {
    manualAction(action, target);
  } else if (assist) {
    assistAction(action, target);
  } else if (action === 'setup-mode') {
    const players = setupPlayers();
    const p = players[Number(target.dataset.index)];
    p.mode = nextMode(p.mode);
    renderSetup(players);
  } else if (action === 'setup-remove') {
    const players = setupPlayers();
    players.splice(Number(target.dataset.index), 1);
    renderSetup(players);
  }
});

$('sheet-backdrop').addEventListener('click', () => {
  hideSheet();
  manual = null;
  assist = null;
});

$('assist-button').addEventListener('click', event => {
  startAssistTurn(Number(event.currentTarget.dataset.player));
});

$('undo-button').addEventListener('click', () => {
  if (!undoStack.length) return;
  game.players = JSON.parse(undoStack.pop());
  save();
  render();
});

$('new-game-button').addEventListener('click', () => openSetup());

$('add-player-button').addEventListener('click', () => openSetup({ keepScores: true }));

$('add-player').addEventListener('click', () => {
  const players = setupPlayers();
  if (players.length < 6) players.push({ name: '', mode: 'pen' });
  renderSetup(players);
});

$('start-game').addEventListener('click', () => {
  const rows = setupPlayers();
  // keepScores mode carries a card through the dataset; a fresh game does not
  const keeping = rows.some(p => Array.isArray(p.card));
  const players = rows.map((p, i) => ({
    name: p.name.trim() || `Pelaaja ${i + 1}`,
    mode: p.mode,
    card: Array.isArray(p.card) ? p.card : Array(CATEGORY_COUNT).fill(null),
  }));
  if (!players.length) return;
  if (keeping) pushUndo();            // adding mid-game stays undoable
  else undoStack = [];
  game = { players };
  save();
  $('setup').hidden = true;
  render();
});

// ---------- boot ----------

{
  const saved = load();
  if (saved && saved.players.some(p => filledCount(p.card) < CATEGORY_COUNT)) {
    game = { players: saved.players };
    undoStack = saved.undo;               // keep undo working across a reload
    render();
  } else {
    game = saved ? { players: saved.players } : null;
    if (game) render();
    openSetup();
  }
  probeTable();
}
