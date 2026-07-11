# Exact Maxi-Yatzy solver

An exact expected-score solver for the Finnish Tactic Maxi-Yatzy rules, using
six dice, twenty score categories, at most three rolls per turn, a 75-point
upper-section threshold, and a 50-point bonus.

The project currently contains the verified scoring model, unordered dice and
keeper enumeration, upper-state analysis, parallel backward solver, compact
lookup writer and verifier, and a solver-kernel benchmark. The first complete
result is recorded in [`docs/results-v1.md`](docs/results-v1.md).

The v1 table has been audited by an independent verifier
([`verify/`](verify/)) that re-derives sampled values by one-step backward
induction and reproduces the initial expected value by playing complete games
with its own score bookkeeping. Method and results are in
[`docs/verification-v1.md`](docs/verification-v1.md).

The planned fast-compute/compact-storage design is documented in
[`docs/architecture.md`](docs/architecture.md).

Two interactive front-ends use the solved table via a shared policy module
([`src/policy.cpp`](src/policy.cpp), mmap + lazy per-mask decode):

- `maxiyatzy-advise` — a text-mode advisor that walks one scorecard through a
  game, recommending keeps and categories and pricing any deviation:
  `./build/maxiyatzy-advise --table maxiyatzy-values.mytz`
- [`web/`](web/) — a static, mobile-friendly scorekeeper where selected
  players get optimal-play hints. It needs no server process: the table is
  read with HTTP range requests from any static host (see
  [`web/README.md`](web/README.md)).

## Rules encoded

- Small straight: `1-2-3-4-5`, with the sixth die ignored.
- Large straight: `2-3-4-5-6`, with the sixth die ignored.
- Full straight: exactly `1-2-3-4-5-6`.
- Pair categories use distinct face values and select the highest-scoring set.
- N-of-a-kind categories select the highest qualifying face.
- Full house selects a triple and a distinct pair; the sixth die is ignored.
- Super house is two distinct triples.
- Tower is four of one face and two of another.

Voluntary crossing is allowed: the player may put zero in any open category,
even if another category scores or Chance remains open. The complete variant is
specified in [`docs/rules.md`](docs/rules.md).

## Why an impossible bonus can be coalesced

The table stores *future additional score*, not score already earned. Past
lower-section points never affect future decisions, so they are omitted.

The upper subtotal matters only because it may cause the future 50-point bonus.
For a fixed set of already-filled upper boxes, let `future_maximum` be six times
the sum of the remaining face values. If

```text
current_upper_total + future_maximum < 75
```

then no sequence of future play can earn the bonus. Two such subtotals have
identical available categories, identical future rolls, identical immediate
scores, and both have zero possible bonus. Therefore their optimal future value
is identical and they may share one `bonus impossible` state.

This does **not** remove future upper-section choices. If three fives are rolled,
the solver still compares scoring 15 in Fives against every available lower
category or zero. The only discarded information is whether the already-earned
upper subtotal was, for example, 20 or 25 after both have become incapable of
reaching 75.

## Build

The dependency-free Makefile works with the Apple command-line tools:

```sh
make -j5
make test
./build/maxiyatzy-info
make bench
```

Alternatively, with CMake installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/maxiyatzy-info
./build/maxiyatzy-bench 200000
```

## Solve

```sh
./build/maxiyatzy-solve --threads 5 --output maxiyatzy-values.mytz
./build/maxiyatzy-inspect maxiyatzy-values.mytz
zstd -3 --rm maxiyatzy-values.mytz
```

The binary format is documented in
[`docs/file-format.md`](docs/file-format.md). Calculation uses `double`; the
portable table stores canonical values as XOR-delta `float32` and is designed
to be straightforward to decode from Node.js.
