# Independent verification of the v1 solve

This documents an audit of the version 1 result
([`results-v1.md`](results-v1.md)) performed separately from the solver
implementation. The verifier lives in [`verify/verify_main.cpp`](../verify/verify_main.cpp)
and is intentionally **not** the solver: it does not link `src/solver.cpp` or
`src/upper_states.cpp`, and it reimplements the `.mytz` decoder, the
canonical upper-state mapping, the bonus accounting (exact running subtotal,
+50 exactly when it crosses 75), the keeper-expectation recursion, and full
game playouts from the rules in [`rules.md`](rules.md). Only the dice multiset
enumeration and the category scoring functions are shared with the solver;
both are unit-tested and small enough to audit by hand.

## Method 1: Bellman residual check

For randomly sampled `(scorecard mask, upper state)` pairs, the verifier
recomputes the stored value with one independent backward-induction step:
category continuations from the stored next-layer values, third-roll
valuation over all 462 unordered rolls, two keeper-expectation/keeper-max
passes, and the multinomial expectation over the opening roll. Because the
solver's table claims to be the fixed point of exactly this recursion, every
stored value must match the recomputation up to `float32` rounding of the
stored inputs.

Result over 100,000 sampled states (seed 12345):

| Metric | Value |
| --- | ---: |
| Mean absolute residual | 3.76e-06 |
| Maximum absolute residual | 2.05e-05 |
| Root recomputed via one Bellman step | 383.305401 |
| Root stored | 383.305389 |

The residuals are at the level of `float32` quantisation of the stored
values (~1e-7 relative on values near 400), so the table satisfies the
optimality recursion at every sampled state.

## Method 2: Monte Carlo playout with independent accounting

The verifier plays complete 20-turn games. Decisions (both keeper choices and
the category choice) are taken greedily against the lookup table, but the
score is kept by the verifier's own bookkeeping: it tracks the exact upper
subtotal — not the solver's coalesced state — and awards the 50-point bonus
itself when the subtotal crosses 75. If the state encoding, the
bonus-impossible/achieved coalescing, or the transition bonuses were wrong,
the empirical mean would not reproduce the stored initial expected value.

Result over 200,000 games (10 threads):

| Metric | Value |
| --- | ---: |
| Empirical mean score | 383.23 |
| Standard deviation | 56.19 |
| Standard error of mean | 0.126 |
| Stored initial expected value | 383.305 |
| Difference | −0.076 (−0.60 SE) |
| Games earning the 50-point bonus | 90.74% |
| Mean upper-section total | 78.46 |

The mean matches within Monte Carlo noise. The side statistics explain why
the optimal average is far above typical human results: with six dice the
75-point threshold needs only ~2.08 dice of each face on average, while
targeted keeping yields ~2.5, so optimal play banks the bonus in roughly nine
games out of ten.

## Context from prior work

No previously published exact optimal expected value for Maxi Yatzy was
found (searched July 2026: academic theses, arXiv, GitHub, and
Scandinavian-language sources). For calibration, the solved smaller games
are: Scandinavian 5-dice free-choice Yatzy 248.63 (two independent KTH
computations: Larsson & Sjöberg 2012, Sederblad & Törnebohm 2013) and
American Yahtzee 254.59 (Verhoeff 1999 and others).

Note on variants: the Swedish/Danish Tactic Maxi Yatzy uses an 84-point
threshold with a 100-point bonus and allows saving unused rerolls. This
project follows the Finnish Tactic rules (`saannot.pdf`): 75-point threshold,
50-point bonus, no saved rerolls. Results from other implementations (e.g.
Board Game Arena) are therefore not comparable.

## Running the verifier

```sh
make -j5
zstd -d -k artifacts/maxiyatzy-values-v1.mytz.zst -o /tmp/values.mytz
./build/maxiyatzy-verify /tmp/values.mytz bellman 100000
./build/maxiyatzy-verify /tmp/values.mytz mc 200000 10
```

Both modes first validate the embedded FNV-1a payload checksum.
