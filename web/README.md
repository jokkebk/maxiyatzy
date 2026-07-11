# Web scorekeeper with optimal-play hints

A static, mobile-friendly Maxi Yatzy scorecard for up to six players. Any
player can be marked AI-assisted: their turns are entered as dice and the app
shows the optimal keeps and category choices with expected-value deltas,
computed from the solved lookup table. Unassisted players just get their
scores written into the card. Game state persists in `localStorage`.

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

```sh
npx http-server web -c-1      # supports ranges, disables caching
```

Note that `python3 -m http.server` does **not** support range requests — the
app still works via the full-download fallback, but expect a 141 MiB load.

## Files

- `index.html`, `style.css`, `app.js` — UI: paper-scorecard rendering, manual
  score entry, the assisted-turn flow, setup and persistence.
- `engine.js` — JavaScript port of the dice/scoring/turn-evaluation logic in
  `src/policy.cpp`.
- `table.js` — `.mytz` header parsing and range-request value loader with
  per-mask caching.
