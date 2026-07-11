# TODO

## Web scorekeeper

- **Assist toggle discoverability.** Switching a player between pencil and
  assisted mode currently means tapping the player's name in the card header,
  which is easy to miss. Give it a visible control (e.g. a small ✨ button in
  the header cell), and consider a third per-player mode where hints are
  hidden behind a tap ("on-demand") so players can peek only when they want.
- **Automated JS-vs-C++ engine cross-check.** `web/engine.js` is a hand port
  of `src/policy.cpp`. The root value (383.3) and first-turn hints were
  checked by hand in the browser; add a scripted test (e.g. Node) that
  replays a few hundred sampled states through both engines and compares
  keep/category rankings and EVs.
- **Mid-turn state is not persisted.** Refreshing during an assisted turn
  loses the entered dice (the scorecard itself is safe in localStorage).
- **Undo granularity.** Undo reverts whole fills; it cannot step back within
  an assisted turn.
- **Offline/PWA mode.** Cache the app shell and store fetched table slices
  (or the whole table on demand) in the Cache API/IndexedDB so a phone works
  without connectivity at the summer cottage.
- **Post-game review.** For assisted players the data is already there to
  report EV lost per turn; for a trainer mode, let a manual player enter dice
  too and grade their choices afterwards.
- **i18n.** Category captions and UI strings are Finnish-only; add an
  English toggle.

## CLI advisor

- The `keep` override recomputes the full option list twice per command;
  harmless but sloppy.
- Consider a `sim` command that finishes the game automatically from the
  current position to show the expected distribution, and an `ev <category>`
  query.

## Solver / data

- A quantized table variant (e.g. centi-point fixed point, zstd-framed with
  a chunk index) could cut hosting size roughly in half if the 141 MiB
  static file ever becomes annoying.
- Solve the Swedish/Danish rule variant (84-point threshold, 100-point
  bonus, saved rerolls) for comparison; saved rerolls need a bigger state
  space (banked-reroll count per turn).
