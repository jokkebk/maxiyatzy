# Solver architecture

## Priorities

1. Exact and reproducible expected-score calculation in `double`.
2. Efficient use of all CPU cores.
3. Simple direct indexing in the hot loop.
4. Compact, portable lookup data for interactive clients.

The calculation may use substantially more memory than the final file. On the
target 36 GB machine, spending roughly 1 GB to avoid complicated indexes is a
good trade.

## In-memory value table

Use a dense table indexed by:

```text
(20-bit filled-category mask, canonical upper state)
```

There are 77 upper-state slots per mask:

- exact totals `0..74` while the bonus remains attainable;
- `75` for bonus already achieved;
- `76` for bonus impossible.

The dense table therefore occupies:

```text
2^20 * 77 * sizeof(double) = 616 MiB
```

Invalid combinations remain unused. A precomputed list identifies the 36.864
million canonical states that actually need calculation. This keeps direct
successor lookup while avoiding work on unreachable states.

Only one dense table is necessary. A state with mask `M` depends only on states
with mask `M | category_bit`, so masks with more filled categories are complete
before the current popcount layer begins. Current writes cannot overwrite a
successor.

## Parallel execution

Process filled-category counts from 20 down to 0. Within one popcount layer all
states are independent.

- Use a fixed worker pool rather than creating threads per layer.
- Give each worker contiguous chunks obtained from an atomic work counter.
- Make chunk size configurable; begin benchmarking around 128--1024 states.
- Each worker owns its 924-entry keeper expectation arrays and 462-entry roll
  arrays, avoiding locks and false sharing.
- The dense value table and all dice/scoring tables are shared read-only except
  for disjoint output ranges.
- Always allow `--threads`. On the target M3 Pro, five independent workers
  measured about 312,000 states/second in aggregate, while eleven measured only
  about 260,000. Default to the performance-core count when it can be detected
  on Apple Silicon; otherwise use hardware concurrency as a portable fallback
  and print enough timing data to tune it.

Layer barriers are required because layer `k` reads layer `k+1`. No other hot
loop synchronization should be needed.

## Within-turn calculation

For every boundary state:

1. Evaluate every legal scoring category for each of 462 complete rolls.
2. Run the 924-state keeper expectation transform.
3. Select the best keeper contained in each complete roll.
4. Repeat steps 2--3 for the second reroll.
5. Average the value before the mandatory initial roll and store it in the
   dense boundary table.

Scoring, keeper containment, dice additions, roll multiplicities, category
mask successors, and upper-state canonicalization are precomputed.

## Persistent lookup format

Do not dump the dense computation table. Serialize only canonical states:

- fixed header with magic, format version, rules flags, bonus threshold,
  category ordering, value encoding, and payload checksum;
- mask-offset table for direct lookup;
- canonical upper-state map for each of the 64 upper masks;
- values in mask order;
- independently compressed blocks, so clients need not inflate the entire
  file merely to inspect its metadata.

There are 36.864 million values. Raw sizes are:

| Encoding | Value payload |
| --- | ---: |
| `float64` | 281.2 MiB |
| `float32` | 140.6 MiB |
| 16-bit fixed point | 70.3 MiB |

The initial format uses `float32`, followed by compression. Values
must always be calculated in `double`; conversion happens only when writing.
Before accepting the compact file, run a validation pass that reloads the
stored values and compares chosen actions and expected regret against the
original double table. If float rounding changes meaningful decisions, add a
sparse high-precision correction stream rather than doubling every stored
value.

On the first real table, raw float32 compressed to 103--106 MiB. XOR-delta
coding adjacent float bit patterns within each mask is exactly reversible and
reduced the Zstandard result to about 94 MiB at level 3 and 91 MiB at level 19.

## Rule identity

Lookup files must encode all choices that affect the policy, including whether
voluntary zero scoring is permitted. A client must reject a file whose rules
identifier differs from the rules it presents.
