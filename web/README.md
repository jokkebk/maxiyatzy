# Web scorekeeper with optimal-play hints

A static, mobile-friendly Maxi Yatzy scorecard for up to six players. Each
player has a play mode, cycled with the button in their column header (or in
the setup screen):

- **kynä** (pen) — scores are just written into the card.
- **✨ apuri** (assist) — the turn is entered as dice and the app shows the
  optimal keeps and category choices with expected-value deltas, computed
  from the solved lookup table.
- **👁 kurkista** (peek) — the same dice-entry flow, but the recommendation
  stays hidden behind a *kurkista vinkki* button so a player can decide first
  and reveal the optimal move only when they want to check themselves.

Game state persists in `localStorage`, including the undo history, so an
accidental reload between turns loses nothing (the transient dice of an
in-progress assisted turn are not persisted).

## Deployment

Copy these files plus the **decompressed** value table to any static host:

```sh
zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o web/maxiyatzy-values.mytz
# upload web/* to a directory on the server
```

There is no server-side code. The app reads the 141 MiB table with HTTP
range requests — one turn costs about twenty ~300-byte ranges — so the host
must answer `206 Partial Content` (nginx, Apache, S3, Caddy and most CDNs do
out of the box; same-origin means no CORS setup). If the host ignores
`Range` and answers `200`, the app falls back to downloading the whole table
once per session, which works but is heavy.

Scorekeeping works even when the table is missing or unreachable; only the
hints are disabled.

## Local testing

Run from the **repo root** (not from `web/`), so the `web` path below is the
served directory:

```sh
npx http-server web -c-1      # supports ranges, disables caching
bunx http-server web -c-1     # same, via bun
```

From inside `web/` use `bunx http-server . -c-1` instead.

Note that `python3 -m http.server` does **not** support range requests — the
app still works via the full-download fallback, but expect a 141 MiB load.

### Engine cross-check

`engine.js` is a hand port of `src/policy.cpp`, so a scripted test replays a
few hundred sampled states through both engines and diffs the pre-roll value
plus the keep and category rankings and EVs:

```sh
zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o maxiyatzy-values.mytz
make build/maxiyatzy-crosscheck
node web/test/crosscheck.mjs            # or: npm --prefix web run crosscheck
```

It exits non-zero on any divergence beyond float32 rounding (`MYTZ_TABLE` and
`CROSSCHECK_BIN` override the table/binary locations). The Node driver loads
the real `table.js`/`engine.js`, feeding `table.js` the whole file through a
`fetch` shim that trips its full-download fallback.

## Files

- `index.html`, `style.css`, `app.js` — UI: paper-scorecard rendering, manual
  score entry, the assisted-turn flow, setup and persistence.
- `engine.js` — JavaScript port of the dice/scoring/turn-evaluation logic in
  `src/policy.cpp`.
- `table.js` — `.mytz` header parsing and range-request value loader with
  per-mask caching.
- `test/crosscheck.mjs` — Node cross-check of `engine.js` against the C++
  `maxiyatzy-crosscheck` helper. `package.json` only marks the `.js` files as
  ES modules for Node; static hosts ignore it.
