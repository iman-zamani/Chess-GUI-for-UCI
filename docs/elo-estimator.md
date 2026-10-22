# The position-based Elo estimator

**What it does:** your engine answers a set of test positions at a fixed time per
move; from which positions it "solves", the estimator infers a rating on the
CCRL Blitz scale, with an uncertainty that includes the model's own measured
error. A quick run takes minutes, where a real rating needs thousands of games.

**What it is not:** a rating from games. It never sees your engine play a game,
so time management, opening choice, endgame conversion over many moves and
robustness (crashes, time losses) are not measured. Engines whose strength comes
from those will be misjudged. Treat the number as a quick indicator between real
test runs, not as a replacement for them.

## Method

**Item response theory (2PL).** Every test position *i* has a discrimination
*a<sub>i</sub>* and a difficulty *b<sub>i</sub>*; an engine has an ability θ.
The probability that the engine solves the position is
σ(a<sub>i</sub>(θ − b<sub>i</sub>)).

**Solved** means the engine's move loses at most *T* win-percentage points
against the best move. Every legal move of every position was evaluated in
advance by Stockfish 19 (MultiPV over all legal moves, capped at 40, 10M nodes,
one thread); moves outside the analysed set count as bad. Centipawns are turned
into winning chances with the Lichess model, 50 + 50·(2/(1 + e<sup>−0.00368208·cp</sup>) − 1).

**Fitting.** Two variants were compared by cross-validation:

* *anchored*: each calibration engine's θ is fixed to its CCRL rating,
  θ = (Elo − 2500)/400, and every position is a one-dimensional penalised logistic
  regression; θ maps back to Elo exactly;
* *joint*: a<sub>i</sub>, b<sub>i</sub> and every θ are fitted together (penalised
  joint maximum likelihood) and θ is mapped to Elo with a least-squares line.

**Estimating a new engine.** θ is the penalised maximum-likelihood estimate
(Newton iterations, prior N(0, 3²) on θ) and its standard error comes from the
Fisher information. The reported 95% interval combines that standard error with
the model's cross-validated error on engine families it was not trained on
(leave-one-family-out RMSE of the calibration engines estimated in the same rating
range), so the interval is honest about how well the model generalises, not just
about measurement noise. Where that error is large (below 2500, see Results) the
result carries a warning.

**Quick mode** is computerised adaptive testing: the next position is the unused
one with the most Fisher information at the current estimate, taken from the
category (tactics, positional, endgame) furthest below its share; the test stops
when the measurement SE is at most 50 Elo (after at least 20 positions) or after
300 positions. **Full mode** asks every position.

**Protocol** (identical for calibration and for your engine): Threads = 1,
Hash = 64 MB, any "Move Overhead" option set to 10 ms, `ucinewgame` + `isready`
before every position, `go movetime T`, and `stop` if no `bestmove` has arrived
50 ms after T. T is 500 ms scaled by this computer's speed relative to the
calibration machine, measured with the internal perft benchmark
(`chess-cli bench`, best of 3), clamped to 0.25x-4x. A position is asked again if
the computer slept during the search.

## Positions

The candidate pool has 4600 positions from two CC0 sources:

* the Lichess puzzle database (tactics and defence), stratified by puzzle rating so
  it spans easy to very hard;
* Lichess rated games from June 2015 with both players at 2200+ and at least 3 minutes
  base time: per game one quiet middlegame position (moves 12-40, more than 4 pieces
  besides kings and pawns) and, when the game got there, up to two endgame positions
  (at most 4 such pieces, moves 12-80, at least 10 plies apart). Quiet means not in
  check, not right after a capture, at least 3 legal moves.

All candidates are legal and distinct (by position, ignoring the move counters).
Every position keeps its source and ID (`Resources/elo/positions.epd`, opcodes `id`,
`c0` category, `c1` source, `c2` URL).

The final set is chosen after calibration, and the same rules are applied inside
every cross-validation fold: positions with more than 5 acceptable moves, and
positional or endgame positions already decided by 400 cp or more, are dropped;
of the rest, those with a positive, informative discrimination (a > 0.25) and a good
item fit are taken, balanced across categories (40% tactics and defence, 35%
positional, 25% endgame) and spread over the whole difficulty range, with extra
density between 2400 and 3400.

## Calibration engines

The engines, their pinned source tags and commits, and their CCRL Blitz ratings
(copied from the list with its URL and date) are in
[`data/elo/engines.json`](../data/elo/engines.json). Every engine is built from
source; its `id name` is checked against the CCRL entry's version. Engines that
could not be matched to their CCRL entry, did not build, or cannot follow the
protocol are listed there with the reason they were left out. Engines are not
redistributed; only their answers are kept.

## Results

Model version 1, calibrated on 2026-10-22. The engine runs were done on a 4-core Intel
Xeon E5-2667 v4 (Linux, gcc builds, 3 engines at a time), `calibNps` = 9,043,110
(`chess-cli bench`). Everything from here to "Other runs" is written by
`tools/elo/report.py` from the pipeline's files.

Calibration: 29 engines from 7 families, CCRL Blitz 1271-3563. Model: anchored fit, solved = a move within 5 win-% points of the best, 724 positions.

### Cross-validation

Every fold refits the positions and reselects them without the engines it predicts. *Leave-one-engine-out* (LOEO) predicts each engine from the others; *leave-one-family-out* (LOFO) predicts every version of an engine family from the other families only, the honest test for an engine the model has never seen.

| T (win %) | model | LOEO MAE | LOEO RMSE | LOEO max | Spearman | LOFO MAE | LOFO RMSE | LOFO max |
|---|---|---|---|---|---|---|---|---|
| 2 | anchored | 100 | 155 | 587 | 0.979 | 178 | 254 | 827 |
| 2 | joint | 103 | 163 | 631 | 0.981 | 179 | 263 | 869 |
| 3 | anchored | 93 | 151 | 579 | 0.982 | 167 | 246 | 811 |
| 3 | joint | 102 | 163 | 627 | 0.983 | 170 | 259 | 850 |
| 5 | anchored **(shipped)** | 90 | 152 | 593 | 0.984 | 161 | 245 | 840 |
| 5 | joint | 107 | 168 | 636 | 0.982 | 168 | 251 | 849 |
| 7 | anchored | 96 | 152 | 566 | 0.987 | 166 | 242 | 841 |
| 7 | joint | 117 | 175 | 608 | 0.983 | 172 | 247 | 842 |
| 10 | anchored | 112 | 163 | 563 | 0.970 | 180 | 245 | 823 |
| 10 | joint | 129 | 190 | 599 | 0.967 | 193 | 265 | 864 |

By rating band (shipped configuration; bias > 0 means estimates too high):

| CCRL Blitz | engines | LOEO MAE | LOEO bias | LOFO MAE | LOFO bias |
|---|---|---|---|---|---|
| below 2000 | 5 | 239 | +239 | 508 | +508 |
| 2000-2500 | 6 | 57 | +11 | 177 | +177 |
| 2500 and above | 18 | 60 | -20 | 59 | -38 |

The 95% interval uses the LOFO RMSE of the engines *estimated* in the same range, since that is what is known when a new engine is tested:

| Estimate | engines | LOFO RMSE | model error in the 95% interval |
|---|---|---|---|
| below 2500 | 10 | 403 | +/- 790 |
| 2500 and above | 19 | 77 | +/- 151 |

Estimates below 2500 get a warning in the GUI and in `chess-cli`.

The configuration with the lowest LOEO MAE is shipped. Choosing it on the LOEO numbers makes them slightly optimistic; the LOFO numbers and the held-out engine below are the checks that were not used for any choice.

### Per engine (shipped configuration)

| Engine | CCRL Blitz | LOEO estimate | error | LOFO estimate | error |
|---|---|---|---|---|---|
| Stash 9.0.1 | 1271 | 1864 | +593 | 2111 | +840 |
| Stash 11.0.1 | 1687 | 1801 | +114 | 2172 | +485 |
| Bit-Genie 2 | 1793 | 2201 | +408 | 2245 | +452 |
| Stash 12.0 | 1883 | 1896 | +12 | 2265 | +382 |
| Stash 13.0.1 | 1968 | 2034 | +66 | 2350 | +382 |
| Stash 14.0 | 2059 | 1991 | -68 | 2317 | +258 |
| Bit-Genie 4 | 2166 | 2306 | +140 | 2390 | +224 |
| Stash 15.3 | 2174 | 2150 | -24 | 2342 | +168 |
| Stash 17.0 | 2297 | 2301 | +4 | 2458 | +161 |
| Bit-Genie 5 | 2361 | 2421 | +60 | 2493 | +132 |
| Stash 19.0 | 2474 | 2427 | -47 | 2594 | +120 |
| Berserk 2.0.0 | 2529 | 2533 | +4 | 2533 | +4 |
| Blunder 8.0.0 | 2654 | 2507 | -147 | 2507 | -147 |
| Stash 21.0 | 2714 | 2746 | +32 | 2762 | +48 |
| Bit-Genie 8 | 2860 | 2757 | -103 | 2779 | -81 |
| Weiss 1.0 | 2896 | 2799 | -97 | 2790 | -106 |
| Stash 25.0 | 2933 | 2933 | -0 | 2913 | -20 |
| Stash 27.0 | 3049 | 3003 | -46 | 2986 | -63 |
| Weiss 1.2 | 3056 | 2973 | -83 | 2975 | -81 |
| Stash 29.0 | 3128 | 3106 | -22 | 3056 | -72 |
| Weiss 1.4 | 3255 | 3356 | +101 | 3322 | +67 |
| Stash 33.0 | 3273 | 3282 | +10 | 3258 | -16 |
| Weiss 2.0 | 3319 | 3382 | +63 | 3363 | +44 |
| Stash 35.0 | 3345 | 3412 | +67 | 3355 | +10 |
| Ethereal 12.00 | 3391 | 3255 | -136 | 3229 | -162 |
| Stash 37.0 | 3418 | 3394 | -24 | 3388 | -30 |
| Ethereal 13.00 | 3487 | 3428 | -58 | 3411 | -76 |
| Stockfish 10 | 3509 | 3589 | +80 | 3526 | +17 |
| Stockfish 11 | 3563 | 3569 | +6 | 3539 | -24 |

### Held-out engine

Kept out of every fit and of the cross-validation, then estimated with the shipped model:

| Engine | CCRL Blitz | Mode | Estimate | 95% interval | Inside? |
|---|---|---|---|---|---|
| Berserk 3.2.0 | 2910 | full, 724 positions | 2877 | +/- 173 | yes |
| Berserk 3.2.0 | 2910 | quick, 168 positions | 2886 | +/- 180 | yes |

### Other runs of the held-out engine

The same engine on the same Mac (hardware factor about 0.6, so about 300 ms per
position), not used for any choice:

| Run | Estimate | 95% interval |
|---|---|---|
| `chess-cli`, full, first run | 2851 | +/- 487 |
| `chess-cli`, quick (177 positions), first run | 2906 | +/- 490 |
| GUI, quick (172 positions) | 2896 | +/- 180 |

The first two runs used the same positions and parameters. The only difference was the
interval: it then used the overall LOFO RMSE (245 Elo) for every estimate, and was
replaced by the error for the estimate's own rating range. Repeated runs differ by a few
tens of Elo because engine searches under a time limit are not deterministic.

### Limits

* **Below 2500 the model is much less accurate.** Only 5 calibration engines are below
  2000, and 4 of them are Stash versions. When a family is left out, nothing similar is
  left at the low end, and the estimate is too high, often by several hundred Elo. The
  interval there is about +/- 800, and a warning is shown.
* From 2500 up, the error on unseen families (MAE 59) is close to the error on unseen
  versions of known families (MAE 60). It was measured on 18 engines from 6 families, so
  an engine with a very unusual style can still be misjudged.
* It measures move choice at about 0.5 s per move, single-threaded. Time management,
  pondering, multi-threaded search and robustness in games are not measured.

## Reproducing

[`tools/elo/pipeline.sh`](../tools/elo/pipeline.sh) rebuilds everything from
scratch (downloads, candidate positions, reference analysis, engine builds, engine
runs, cross-validation, final fit and selection); every step is resumable. The
Python tools need `tools/elo/requirements.txt`. The C++ estimator
(`src/elo_estimator.*`) is tested against the Python fit
(`tests/data/elo_fixture.json`, same θ within 1e-6).
