# TODO

## Web scorekeeper

- **Mid-turn state is not persisted.** Refreshing during an assisted turn
  loses the entered dice. The scorecard and undo history are safe in
  localStorage, so this is deliberately out of scope for now.
- **Undo granularity.** Undo reverts whole fills (turn level) and now
  survives a reload; it still cannot step back within an assisted turn.
- **Offline/PWA mode.** Cache the app shell and store fetched table slices
  (or the whole table on demand) in the Cache API/IndexedDB so a phone works
  without connectivity at the summer cottage.
- **Post-game review.** For assisted players the data is already there to
  report EV lost per turn; for a trainer mode, let a manual player enter dice
  too and grade their choices afterwards.
- **Share as an image.** `jaa tulokset` shares a plain-text card, which only
  lines up in a monospace font; rendering the paper card to a canvas would
  travel better through chat apps.
- **i18n.** Category captions and UI strings are Finnish-only; add an
  English toggle.

## CLI advisor

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
