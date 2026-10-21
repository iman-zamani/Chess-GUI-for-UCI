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
(leave-one-family-out RMSE), so the interval is honest about how well the model
generalises, not just about measurement noise.

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

Not available yet. Status on 2026-10-21: the 4600 candidate positions and their
Stockfish 19 reference analysis are done, and the 32 calibration engines build from their
pinned tags and follow the protocol (checked on a few positions each). The engine runs
(about 6 hours on 4 cores), the cross-validation and the shipped model are still to come;
`tools/elo/pipeline.sh` produces them, and `tools/elo/report.py` writes this section from
its output so that every figure here comes straight from the files.

## Reproducing

[`tools/elo/pipeline.sh`](../tools/elo/pipeline.sh) rebuilds everything from
scratch (downloads, candidate positions, reference analysis, engine builds, engine
runs, cross-validation, final fit and selection); every step is resumable. The
Python tools need `tools/elo/requirements.txt`. The C++ estimator
(`src/elo_estimator.*`) is tested against the Python fit
(`tests/data/elo_fixture.json`, same θ within 1e-6).
