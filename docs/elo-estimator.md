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
Fisher information. The reported 95% interval combines three things: that
standard error, the model's cross-validated error on engine families it was not
trained on (leave-one-family-out RMSE of the calibration engines estimated in
the same rating range), and the uncertainty of the speed measurement (below).

**The calibrated range.** A number is reported only between the second engine
family's lowest calibration engine and the second family's highest (model v2:
1578-3487), so that at least two families anchor every rating in the range.
Outside it the model can only extrapolate from one family or from none, and
cross-validation showed such extrapolation to be biased (engines 250-600 Elo
above the top anchor came out 230-270 too low; engines below the bottom anchor
far too high; engines above 3600 were overestimated by up to 1148, see below).
So an estimate past an end of the range is not reported as a number but as a
bound (`eloRangeStatus` in `src/elo_estimator.hpp`): "at least" the lower end of
its 95% interval, but never more than the top of the range, and "at most" the
upper end, never less than the bottom. An engine whose whole interval lies above
the range gets "above the calibrated range: at least 3487"; one estimated at 3646
+/- 275 gets "past the top of the calibrated range (3487): at least 3371". The
model's own number out there is an extrapolation, so it is never the bound (it
is kept in the JSON diagnostics as `eloUnclamped`). The same rule, with the same
interval, decides the result, the early stop of the quick test and the running
estimate shown during a test. (Before this rule, Stockfish 19 got "4673".)

**The ceiling of the scale.** The positions themselves limit how high the range
can go. Model v2 ran 12 more engines rated 3632-3774 (Stockfish 16, PlentyChess 8,
Obsidian 16, Stormphrax 8, Integral 8, Caissa 1.26, Clover 9.1, Koivisto 9.0,
Seer 2.8.0, Arasan 26.0, Ethereal 14, Halogen 16). 11 of them solve 94-98% of
the positions they ran, against 92% for Stockfish 11 (3563): at that level the
positions no longer separate engines. Cross-validated with them in the fit,
their own errors are -213 to +1148 Elo (8 of the 10 not held out are more than
380 too high; `data/elo/cv_with_above_scale_2026-11-02.json`), and the engines rated 3000-3600 come out biased low
(leave-one-family-out bias -69 Elo, against -11 without them; MAE over all
in-range engines 120 against 110). So they are not calibration engines
(`aboveScale` in `data/elo/engines.json`); they are only used to check that an
engine that strong is reported as above the range, or at least with the warning
that it is above the top of it (see Results). Raising the
ceiling needs harder positions and a deeper reference analysis than the
10M-node one used here.

**Quick mode** is computerised adaptive testing: the next position is the unused
one with the most Fisher information at the current estimate, taken from the
category (tactics, positional, endgame) furthest below its share; the test stops
when the measurement SE is at most 50 Elo (after at least 20 positions), when the
engine is clearly outside the calibrated range (its whole 95% interval), or after
300 positions. The interval includes the speed term as known at that point
(every speed check updates it); if the speed check after the test widens it so
much that a test which stopped "outside the range" is no longer clearly outside,
the test goes on with more positions. **Full mode** asks every position.

## Protocol, speed and checks

**Protocol** (identical for calibration and for your engine): Threads = 1,
Hash = 64 MB, any "Move Overhead" option set to 10 ms, `ucinewgame` + `isready`
before every position, `go movetime T`, and `stop` if no `bestmove` has arrived
50 ms after T. A position is asked again if the computer slept during the search.

**The move time.** The calibration engines had 500 ms per position on the
calibration machine; your engine gets 500 ms divided by how much faster your
computer runs chess engines, so that it gets the same amount of search. That
ratio is measured, not guessed:

* Two calibration engines ship with the program as *reference engines*:
  Stockfish 11 (GPL-3.0) and Blunder 8.0.0 (MIT), built from pinned tags
  (`tools/build_reference_engines.py`, `Resources/reference/`).
* The model stores 40 searches each of them made during calibration: the
  position, how much it searched ("go nodes N" for Stockfish 11, which stops at an
  exact node count; "go depth d" for Blunder) and the time it reported.
* Before the test, each reference engine repeats 24 of those searches on your
  computer, after a 30-second warm-up that brings the core to its steady clock
  (in a process of its own: the measurement starts from a fresh process, like
  every later check, so an engine that keeps history between searches searches
  the same way each time). Speed = recorded time / time here (median), geometric
  mean over the two engines.

Why not a synthetic benchmark: the internal perft benchmark used before misjudged
engine speed by 1.3x to 2x on an Apple M4, and its own reading on the same Mac
doubled between two days. Replayed searches of real engines repeat to within a
few percent. They are still a proxy for *your* engine's speed: in a replay
experiment on the M4 (25 calibration engines, 17 of them from 5 families with
repeatable searches; `tools/elo/speed_replay.py`, data in
`data/elo/speed_replay_m4_2026-11-02.json`, analysis `tools/elo/speed_proxy.py`),
other engines' speed ratios between the M4 and
the calibration machine scattered around the reference engines' with an RMS of
0.18 in log2 units, 0.27 with every family weighted equally (the Berserk family
runs relatively fast on the M4: +43%). Allowing for estimating that from only 5
families (x 1.31, the 95% point of Student's t with 5 degrees of freedom over
1.96) gives sigma = 0.35 log2, about +/-27% in speed. Times the measured change
of the estimate per doubling of search time (155 +/- 33 Elo, measured on five
engines from five families, `data/elo/speed_constants.json`), that is the
speed term in the interval: about +/-54 Elo at 1 sigma. With
only one reference engine working, sigma is 0.41. No held-out engine is in
these measurements. (The factor 1.31 is a judgement: an upper confidence bound on
an RMS from 5 families, from the chi-square distribution, would be about 2.4x at
95%; the checks below on other machines and engines are what supports 0.35.)

The proxy was then checked on engines and machines not used to set it, with the
recorded searches of `data/elo/replay_set_2026-11-02.json`
(`tools/elo/replay_set.py`; the model v2 run's times are scaled by the machine
speed it recorded with them, so all engines are on the reference engines'
footing), family-weighted RMS of the log2 deviations from the reference engines':

| Computer | Engines (families) | RMS | of which NNUE |
|---|---|---|---|
| The calibration machine itself (Xeon E5-2667 v4) | 6 (5) | 0.026 | Zahak 10.0, Halogen 11 |
| Apple M4, held-out Weiss, Berserk 3.2.0 and Stash | 4 (3) | 0.330 | |
| GitHub macOS runner (Apple M1, virtual), three runs | 6, 5, 6 (5, 4, 5) | 0.407, 0.468, 0.421 | Zahak 6.2 (first and third run) |
| GitHub Linux runner (x86-64), three runs on different CPUs | 6, 7, 6 (5, 6, 5) | 0.141, 0.231, 0.129 | Halogen 11, Zahak 6.2 (second run) |
| GitHub Windows runner (x86-64), two runs | 6 (5) | 0.147, 0.153 | Halogen 11 |

On the calibration machine the replays agree within 0.04, which checks the replay
set and the scaling of the v2 times. Between x86-64 computers the scatter (pooled
0.18) is well inside sigma; on Apple silicon it is larger (pooled with the first
M4 experiment 0.38), mostly because the Berserk family runs 1.5-1.8x faster there
relative to the reference engines. So the speed term depends on the computer's
architecture: x86-64 keeps sigma = 0.35 (above what was measured there, because
no Intel hybrid CPU was among these computers), ARM computers get
0.38 x 1.31 = 0.49, about +/-76 Elo at 1 sigma (set from the first two runner runs;
the third run, 0.421, pools to 0.385 x 1.31 = 0.50) (one reference engine: 0.41 on
x86-64, 0.58 on ARM, the Stockfish 11-only scatter measured the same way). The
numbers, with their runs, are in `data/elo/speed_constants.json`; the replay data
of the runners are in the "Elo validation" workflow's annotations.

**Speed checks during and after the test.** The move time is set once, so a
change of speed later would bias the estimate. Every 60 seconds during the test,
and after it:

* the reference engines replay some of the searches they replayed at the start
  (the same positions: their recorded times differ from position to position), and
* your engine repeats one fixed search of its own on one position ("go nodes N"
  with N the nodes it searched in half its move time, or "go depth d" for engines
  that don't stop at a node count), timed against the same search at the start.
  This follows your engine's own speed on whatever core it runs on. Engines whose
  fixed searches don't repeat within 10% at the start get no such probe (a
  warning says so); on Apple silicon the share of their search time on
  performance cores is measured instead (below), on Linux and Windows the pinning
  keeps them on one core.

While the reference engines replay, your engine is idle; its CPU use then is
measured too: an engine that busy-waits for its input takes the core from the
speed checks (and from itself), and gets no number with that reason.

A reading past the warning level (7%) is taken again at once, and the smaller of
the two changes is kept: a change that lasts shows up twice, while a passing one (a
burst of other load, one slow search) doesn't fail the run by itself. (On the
calibration machine, a quick run of Berserk 3.2.0 once failed on a single reading of
the engine's own search, -24%, while the reference engines on the same CPU read +3%
at the same check and the engine's next reading was +2%.) The largest change kept
fails or warns (table below), and the RMS of the changes is added to the speed term.

**CPU placement.** The reference engines and your engine run on the same core
(`src/cpu_placement.hpp`):

* Linux and Windows: every engine process is pinned to one logical CPU of the
  fastest core class (on Intel hybrid CPUs, a P-core: Linux reads
  `cpu_capacity` or the maximum clock, Windows the EfficiencyClass), chosen as
  the least busy one over 0.2 s, counting the load on its hyperthread siblings,
  and not on the core of CPU 0, which takes most interrupts (unless it is the
  only core). A lock file per physical core (in the temp directory) keeps two
  estimates running at once on different cores, not on two hyperthreads of one
  core; when every such core is taken, the run shares one and says so. The choice
  is tested on synthetic topologies (an i7-12700's 8 P-cores with hyperthreads and
  4 E-cores, busy cores, held cores, a single core: `elo/choose-cpu`). On Windows
  the engine processes and this program (which times them) are also opted out of
  power throttling ("efficiency mode"), which otherwise slows down programs whose
  window is not in front; on machines with more than 64 logical CPUs the CPU is
  chosen in the program's own processor group, and an engine Windows starts in
  another group runs unpinned, with a warning. An engine restarted after a crash
  is pinned again, and checked.
* macOS can't pin threads. The scheduler keeps a single busy thread on a
  performance core. On Apple silicon the share of each engine's CPU time spent on
  performance cores is measured (`proc_pid_rusage`, `ri_user_ptime`; on an M4 a
  busy process read 0.999, and 0.000 when forced onto the efficiency cores): for
  the reference engines' measurement and for your engine's searches. Less than
  90% gives a warning, less than 75% no number. With ten other busy programs on
  an M4, the engine ran 63% of its search time on the efficiency cores and the
  reference engines 62% of theirs: no number. (Before this check that run got a
  number, and a right one, but only by luck.) (Forcing the efficiency cores,
  the macOS background band, was tested and rejected: engine speed fell by 40-66%
  during a run there.)

**Checks on every run.** The result has no number (status "unreliable", exit code
4 in `chess-cli`) when one of these fails, and a warning when it is borderline:

| Check | Warning | No number |
|---|---|---|
| Largest change of speed during the test (reference engines and your engine's own search) | > 7% | > 19% |
| Median time used / move time (calibration engines: 90-100%) | < 80% or > 110% | < 50% or > 125% (it ignores "go movetime" and needs a "stop") |
| Engine CPU time / wall time, counted over the searches (a search the engine crashed in isn't counted) | < 0.8 | < 0.6 (another program had its core: it got less search than measured), or > 1.35 (more than one thread, or a runtime's own threads; on Linux and Windows a pinned engine can't use more than one CPU, so there it can't get more search than measured either) |
| Engine CPU use while it waits (during the reference engines' checks) | | > 0.2 of a core (it busy-waits for its input) |
| Apple silicon: share of the CPU time on performance cores, your engine's searches and the reference engines' measurement | < 90% | < 75% |
| Illegal moves, no move, crashes (calibration engines: none) | any | > 5% of answers |
| Only one reference engine works | interval widened (sigma 0.41; on ARM 0.58) | |
| No reference engine works | interval widened (speed from the perft benchmark, sigma 0.6 log2) | |
| Move time outside 0.25x-4x or below 150 ms | the move time is clamped and the estimate corrected by 155 Elo per doubling of the difference; the correction's uncertainty (33 per doubling) is added to the speed term | |
| The speed could not be checked after the test | warning | |
| Two timed runs at once found no free core | warning | |
| The test was stopped | | no result at all |

Every result carries these diagnostics (`--json`, Export JSON in the GUI), so a
report from another computer can be checked: the speed per reference engine, the
placement, every speed check, the time and CPU use (searching and idle), the
performance-core share, the move time correction, and the time, nodes and depth
of every answer.

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
item fit are taken, aiming at 40% tactics and defence, 35% positional and 25%
endgame, spread over the whole difficulty range with extra density between 2400
and 3400. Not every category has enough such positions at every difficulty, and
shortfalls are filled with the most discriminating remaining positions, so the
shipped set (model v2, 714 positions) is 37% tactics and defence, 51% positional
and 12% endgame. Quick mode still asks the categories in the 40/35/25 proportions
as far as its positions allow.

## Calibration engines

The engines, their pinned source tags and commits, and their CCRL Blitz ratings
(copied from the list with its URL and date) are in
[`data/elo/engines.json`](../data/elo/engines.json). Every engine is built from
source; its `id name` is checked against the CCRL entry's version. Engines that
could not be matched to their CCRL entry, did not build, or cannot follow the
protocol are listed there with the reason they were left out. Engines are not
redistributed; only their answers are kept.

Model v1 had 30 engines from 7 families, half of them Stash versions and all with
hand-crafted evaluation. Model v2 adds 23 engines: newer Stockfish and Ethereal
versions and 13 new families, most of them NNUE (Berserk 13 was run as well but
is left out: its network can no longer be downloaded). They were run on the same
calibration machine with the speed-matched runner
(`run_engines.py --speed`: before every batch the worker measures its own speed
with the reference engines and scales the move time). They answered the 1040
candidate positions that at least one v1 engine failed and that pass the
position filters, so every position the model can select was answered by every
engine. Of the 23, Fatalii 0.2.0-0.5.0, Koivisto 9.0 and Obsidian 16.0 are held
out for validation and the 12 rated above 3600 are above the scale (see "The
ceiling of the scale"); Zahak (5 versions), Shallow Blue 2.0.0 and Halogen 11
join the fit.

Held out of everything (every fit, the cross-validation and the speed
constants), for validation with the shipped estimator: Berserk 3.2.0 (2910), the
whole Weiss family (1.0, 1.2, 1.4, 2.0: 2896-3319, the band where most engines
under development are), Fatalii 0.2.0-0.5.0 (1599-2208), and Koivisto 9.0 and
Obsidian 16.0 above the scale. The fit has 32 engines from 9 families.

## Results

Model version 2, calibrated on 2026-11-01. The calibration engines were run on the
calibration machine, a 4-core Intel Xeon E5-2667 v4 (Linux, gcc builds, 3 engines at
a time): the model v1 engines on 2026-10-22 (`calibNps` = 9,043,110), the v2 engines on
2026-11-01 with the speed-matched runner, whose speed readings (0.70-1.02 of the
recorded searches) set their move times. The tables from here to "Other computers"
are written by `tools/elo/report.py` from the pipeline's files. In the held-out table,
"Linux" is the calibration machine (one estimate at a time, pinned; other programs were
running on it) and "macOS" an Apple M4 (about 2.5x faster, so about 200 ms per
position). The macOS runs were made with the final code, the Linux runs with the code
before its last changes, none of which applies to them (the speed term on ARM computers,
tests and tools).

Calibration: 32 engines from 9 families, CCRL Blitz 1271-3563. Model: anchored fit, solved = a move within 5 win-% points of the best, 714 positions.

### Cross-validation

Every fold refits the positions and reselects them without the engines it predicts. *Leave-one-engine-out* (LOEO) predicts each engine from the others; *leave-one-family-out* (LOFO) predicts every version of an engine family from the other families only, the honest test for an engine the model has never seen.

| T (win %) | model | LOEO MAE | LOEO RMSE | LOEO max | Spearman | LOFO MAE | LOFO RMSE | LOFO max |
|---|---|---|---|---|---|---|---|---|
| 5 | anchored **(shipped)** | 97 | 141 | 400 | 0.985 | 118 | 159 | 456 |

By rating band (shipped configuration; bias > 0 means estimates too high):

| CCRL Blitz | engines | LOEO MAE | LOEO bias | LOFO MAE | LOFO bias |
|---|---|---|---|---|---|
| below 2000 | 8 | 174 | +168 | 249 | +249 |
| 2000-2500 | 6 | 55 | -14 | 55 | +46 |
| 2500 and above | 18 | 76 | -26 | 81 | -56 |

The 95% interval uses the LOFO RMSE of the engines *estimated* in the same range, since that is what is known when a new engine is tested:

| Estimate | engines | LOFO RMSE | model error in the 95% interval |
|---|---|---|---|
| below 2000 | 3 | 361 | +/- 708 |
| 2000-2500 | 13 | 139 | +/- 273 |
| 2500 and above | 16 | 102 | +/- 199 |

Estimates below 2000 get a warning in the GUI and in `chess-cli`.

Only the shipped configuration was cross-validated here (it was chosen on an earlier, larger comparison); the LOFO numbers and the held-out engines below were not used for any choice.

### Per engine (shipped configuration)

| Engine | CCRL Blitz | LOEO estimate | error | LOFO estimate | error |
|---|---|---|---|---|---|
| Stash 9.0.1 | 1271 | 1671 | +400 | 1727 | +456 |
| Shallow Blue 2.0.0 | 1578 | 1954 | +376 | 1954 | +376 |
| Stash 11.0.1 | 1687 | 1745 | +58 | 1891 | +204 |
| Bit-Genie 2 | 1793 | 2090 | +296 | 2095 | +302 |
| Zahak 0.3.0 | 1851 | 1963 | +112 | 2004 | +153 |
| Stash 12.0 | 1883 | 1860 | -23 | 2052 | +170 |
| Zahak 1.0.0 | 1940 | 2042 | +102 | 2091 | +151 |
| Stash 13.0.1 | 1968 | 1992 | +24 | 2147 | +179 |
| Stash 14.0 | 2059 | 1968 | -91 | 2131 | +72 |
| Bit-Genie 4 | 2166 | 2262 | +96 | 2291 | +125 |
| Stash 15.3 | 2174 | 2117 | -57 | 2147 | -27 |
| Stash 17.0 | 2297 | 2290 | -7 | 2339 | +42 |
| Bit-Genie 5 | 2361 | 2388 | +28 | 2412 | +51 |
| Stash 19.0 | 2474 | 2420 | -54 | 2487 | +13 |
| Berserk 2.0.0 | 2529 | 2493 | -36 | 2493 | -36 |
| Blunder 8.0.0 | 2654 | 2493 | -161 | 2493 | -161 |
| Stash 21.0 | 2714 | 2753 | +39 | 2753 | +39 |
| Zahak 6.2 | 2826 | 2538 | -288 | 2549 | -277 |
| Bit-Genie 8 | 2860 | 2743 | -117 | 2756 | -104 |
| Stash 25.0 | 2933 | 2925 | -8 | 2881 | -52 |
| Stash 27.0 | 3049 | 3018 | -31 | 2960 | -89 |
| Stash 29.0 | 3128 | 3133 | +5 | 3055 | -73 |
| Zahak 8.0 | 3160 | 3113 | -47 | 3061 | -99 |
| Stash 33.0 | 3273 | 3315 | +42 | 3298 | +25 |
| Zahak 10.0 | 3333 | 3216 | -118 | 3181 | -152 |
| Stash 35.0 | 3345 | 3456 | +111 | 3390 | +45 |
| Ethereal 12.00 | 3391 | 3290 | -101 | 3261 | -130 |
| Stash 37.0 | 3418 | 3434 | +16 | 3381 | -37 |
| Halogen 11 | 3426 | 3416 | -10 | 3416 | -10 |
| Ethereal 13.00 | 3487 | 3504 | +17 | 3473 | -14 |
| Stockfish 10 | 3509 | 3618 | +109 | 3565 | +56 |
| Stockfish 11 | 3563 | 3673 | +110 | 3620 | +57 |

### Held-out engines

Kept out of every fit and of the cross-validation, then estimated with the shipped model by `chess-cli elo-estimate` (speed measurement, CPU placement and checks included):

| Engine | CCRL Blitz | Computer | Mode | Result | 95% interval | Correct? |
|---|---|---|---|---|---|---|
| Fatalii 0.2.0 | 1599 | Linux | quick, 96 positions | 1621 (+22) | +/- 723 | yes |
| Fatalii 0.2.0 | 1599 | Linux | full, 714 positions | 1702 (+103) | +/- 718 | yes |
| Fatalii 0.3.0 | 1773 | Linux | quick, 94 positions | 1763 (-10) | +/- 722 | yes |
| Fatalii 0.3.0 | 1773 | Linux | full, 714 positions | 1853 (+80) | +/- 719 | yes |
| Fatalii 0.4.0 | 2004 | Linux | quick, 103 positions | 2061 (+57) | +/- 309 | yes |
| Fatalii 0.4.0 | 2004 | Linux | full, 714 positions | 2033 (+29) | +/- 301 | yes |
| Fatalii 0.5.0 | 2208 | Linux | quick, 104 positions | 2105 (-103) | +/- 309 | yes |
| Fatalii 0.5.0 | 2208 | Linux | full, 714 positions | 2114 (-94) | +/- 301 | yes |
| Weiss 1.0 | 2896 | Linux | quick, 216 positions | 2824 (-72) | +/- 246 | yes |
| Weiss 1.0 | 2896 | Linux | full, 714 positions | 2821 (-75) | +/- 242 | yes |
| Weiss 1.2 | 3056 | Linux | quick, 300 positions | 3043 (-13) | +/- 249 | yes |
| Weiss 1.2 | 3056 | Linux | full, 714 positions | 3031 (-25) | +/- 246 | yes |
| Weiss 1.4 | 3255 | Linux | quick, 300 positions | 3339 (+84) | +/- 257 | yes |
| Weiss 1.4 | 3255 | Linux | full, 714 positions | 3336 (+81) | +/- 254 | yes |
| Weiss 2.0 | 3319 | Linux | quick, 300 positions | 3385 (+66) | +/- 258 | yes |
| Weiss 2.0 | 3319 | Linux | full, 714 positions | 3383 (+64) | +/- 256 | yes |
| Berserk 3.2.0 | 2910 | Linux | quick, 285 positions | 2914 (+4) | +/- 246 | yes |
| Berserk 3.2.0 | 2910 | Linux | full, 714 positions | 2850 (-60) | +/- 243 | yes |
| Koivisto 9.0 | 3632 | Linux | quick, 21 positions | a bound: at least 3487 |  | yes |
| Obsidian 16.0 | 3747 | Linux | quick, 20 positions | a bound: at least 3487 |  | yes |
| Ethereal 14.00 | 3647 | Linux | quick, 300 positions | a bound: at least 3368 |  | yes |
| Stockfish 16 | 3761 | Linux | quick, 20 positions | a bound: at least 3487 |  | yes |
| PlentyChess 8.0.0 | 3774 | Linux | quick, 20 positions | a bound: at least 3487 |  | yes |
| Seer 2.8.0 | 3640 | Linux | quick, 31 positions | a bound: at least 3487 |  | yes |
| Weiss 1.0 | 2896 | macOS | quick, 187 positions | 2749 (-147) | +/- 269 | yes |
| Weiss 1.0 | 2896 | macOS | full, 714 positions | 2758 (-138) | +/- 264 | yes |
| Weiss 1.2 | 3056 | macOS | quick, 300 positions | 3024 (-32) | +/- 270 | yes |
| Weiss 1.2 | 3056 | macOS | full, 714 positions | 3039 (-17) | +/- 268 | yes |
| Weiss 1.4 | 3255 | macOS | quick, 300 positions | 3270 (+15) | +/- 276 | yes |
| Weiss 1.4 | 3255 | macOS | full, 714 positions | 3251 (-4) | +/- 273 | yes |
| Weiss 2.0 | 3319 | macOS | quick, 300 positions | 3309 (-10) | +/- 277 | yes |
| Weiss 2.0 | 3319 | macOS | full, 714 positions | 3310 (-9) | +/- 275 | yes |
| Berserk 3.2.0 | 2910 | macOS | quick, 300 positions | 2992 (+82) | +/- 269 | yes |
| Berserk 3.2.0 | 2910 | macOS | full, 714 positions | 2917 (+7) | +/- 266 | yes |

All 34 runs were correct: 28 numbers whose 95% interval contains the CCRL Blitz rating
(mean absolute error 54 Elo) and 6 bounds on the right side of the rating. The errors
were: Fatalii -103 to +103 (8 runs), Weiss -75 to +84 on Linux and -147 to +15 on the Mac
(8 runs each; Weiss 1.0 came out about 140 low on the Mac in both modes), Berserk 3.2.0
-60 to +82 (4 runs). Ethereal 14.00 (3647) was estimated past the top of the range, so it
got the bound "at least 3368". The wide intervals of Fatalii 0.2.0 and 0.3.0 are the
model error below 2000 (cross-validated), not the measurement. On the Mac every run spent
99.9-100% of the engine's search time on performance cores.

The held-out engines rated above 3600 (Koivisto, Obsidian, Ethereal 14, Stockfish 16,
PlentyChess, Seer) are among the 12 whose solve rates showed that the positions can't tell
such engines apart (see "The ceiling of the scale"), so their bounds confirm the ceiling
rather than test it independently. They were in no fit.

### Other computers (CI runners)

The "Elo validation" workflow built the engines below from their pinned tags on GitHub's
runners (Linux and Windows: x86-64 virtual machines; macOS: an Apple M1 virtual machine)
and estimated them with the shipped code and model, quick mode (the same estimator code
as the final build; `tools/elo/ci_validation.py`). Zahak 6.2 and Halogen 11 are NNUE
calibration engines, in the fit (so in-sample), added because no held-out engine in the
range is NNUE. Zahak 8.0 and 10.0 did not build there; Halogen 11 doesn't build on macOS.

| Engine | CCRL Blitz | Computer | Positions | ms/position | Result | Correct? |
|---|---|---|---|---|---|---|
| Berserk 3.2.0 | 2910 | Linux runner | quick, 273 | 399 | 2906 +/- 246 (-4) | yes |
| Berserk 3.2.0 | 2910 | Windows runner | quick, 289 | 333 | 2920 +/- 251 (+10) | yes |
| Berserk 3.2.0 | 2910 | macOS runner (M1) | quick, 300 | 294 | 2998 +/- 270 (+88) | yes |
| Fatalii 0.4.0 | 2004 | Linux runner | quick, 103 | 401 | 2034 +/- 309 (+30) | yes |
| Fatalii 0.4.0 | 2004 | Windows runner | quick, 103 | 332 | 2034 +/- 313 (+30) | yes |
| Fatalii 0.4.0 | 2004 | macOS runner (M1) | quick, 103 | 289 | 2034 +/- 327 (+30) | yes |
| Fatalii 0.5.0 | 2208 | Linux runner | quick, 104 | 399 | 2095 +/- 309 (-113) | yes |
| Fatalii 0.5.0 | 2208 | Windows runner | quick, 103 | 334 | 2081 +/- 312 (-127) | yes |
| Fatalii 0.5.0 | 2208 | macOS runner (M1) | quick, 104 | 310 | no number: the computer's speed changed by 21% during the test | - |
| Weiss 1.4 | 3255 | Linux runner | quick, 300 | 399 | 3350 +/- 257 (+95) | yes |
| Weiss 1.4 | 3255 | Windows runner | quick, 300 | 332 | 3323 +/- 260 (+68) | yes |
| Weiss 1.4 | 3255 | macOS runner (M1) | quick, 300 | 300 | 3307 +/- 277 (+52) | yes |
| Weiss 2.0 | 3319 | Linux runner | quick, 300 | 395 | 3415 +/- 260 (+96) | yes |
| Weiss 2.0 | 3319 | Windows runner | quick, 300 | 325 | 3368 +/- 262 (+49) | yes |
| Weiss 2.0 | 3319 | macOS runner (M1) | quick, 300 | 293 | 3310 +/- 277 (-9) | yes |
| Halogen 11 (in the fit, NNUE) | 3426 | Linux runner | quick, 300 | 398 | at least 3261 | yes |
| Halogen 11 (in the fit, NNUE) | 3426 | Windows runner | quick, 300 | 338 | 3458 +/- 265 (+32) | yes |
| Zahak 6.2 (in the fit, NNUE) | 2826 | Linux runner | quick, 156 | 395 | 2580 +/- 247 (-246) | yes |
| Zahak 6.2 (in the fit, NNUE) | 2826 | Windows runner | quick, 156 | 336 | 2578 +/- 250 (-248) | yes |
| Zahak 6.2 (in the fit, NNUE) | 2826 | macOS runner (M1) | quick, 162 | 294 | 2617 +/- 268 (-209) | yes |

Runs: https://github.com/iman-zamani/Chess-GUI-for-UCI/actions/runs/41395062637

19 of 20 runs gave a correct number or bound; one (Fatalii 0.5.0 on the macOS runner)
gave no number because its speed changed by 21% during the test. In an earlier run of the
same workflow the macOS virtual machine did that to 2 of 6 engines: the checks are what
keeps such a computer from giving a wrong number. The errors of the held-out engines were
-127 to +96 (14 runs); Zahak 6.2 came out 209-248 low on all three runners (in the
cross-validation, where it is out of the fit, it is -288 with the engine left out and -277
with its family left out: see "Per engine").
### Other runs

On the Mac, with the shipped model, not used for any choice:

| Engine | Situation | Result |
|---|---|---|
| Stockfish 19 | above the scale | above the calibrated range: at least 3487 (stopped after 20 positions) |
| Stash 9.0.1 (CCRL Blitz 1271, calibration engine) | below the range | past the bottom of the calibrated range (1578): at most 1624 |
| Blunder 8.0.0 (2654, calibration engine) | quick | 2508 +/- 268 |
| Berserk 3.2.0 (2910) | quick, every core busy (10 x `yes`) | no number: "the engine ran 63% of its search time on efficiency cores", and the reference engines 62% of theirs (exit code 4) |
| Berserk 4.1.0 | answers in about 2 ms whatever the move time | no number: "used a median 0% of its move time" (exit code 4) |
| Blunder 7.1.0 | doesn't stop at the move time | no number: "searched a median 129% of its move time" (exit code 4) |
| Weiss 0.8 | illegal moves (a `position fen` bug) | below the calibrated range: at most 1578, with the warning "8 answers were illegal moves" (8 of 247, 3.2%, under the 5% that gives no number) |

Before model v2, with model v1 and the perft benchmark, Stockfish 19 got 4368 +/- 369
(quick) and 4673 +/- 471 (full) on the same Mac.

### Limits

* **The ceiling is 3487.** Past it the result is a bound, not a number: the
  positions can't separate engines rated much above 3500 (see "The ceiling of the
  scale"). Measuring there needs harder positions and a deeper reference analysis.
* **Below 2000 the model is much less accurate.** 8 calibration engines are below
  2000, from 4 families; with a family left out, the estimate for it is too high by
  about 250 Elo on average. The interval there is about +/- 700 and a warning is
  shown.
* From 2500 to 3487 the error on unseen families (MAE 81) is close to the error on
  unseen versions of known families (MAE 76). It was measured on 18 engines from 8
  families, so an engine with a very unusual style can still be misjudged.
* **Calibration engines tested again** are estimated from positions chosen partly
  on their own answers, which inflates their estimates near the top: Stockfish 11
  comes out 325 Elo high in-sample (110 high in cross-validation, where the
  positions are chosen without it), Stockfish 10 287, Ethereal 13 145, Stash 37
  154. A new engine is in the cross-validation situation, not this one; but don't
  use a calibration engine (or the bundled Stockfish 11) as a check of the scale.
* **The speed measurement is a proxy:** engines' speed ratios between two computers
  differ from the reference engines' (in the interval: about +/-54 Elo at 1 sigma
  on x86-64, +/-76 on ARM). It was measured on five computers besides the
  calibration machine, with 9 engine families, two of them NNUE (Zahak, Halogen);
  a CPU with other vector units (AVX-512, VNNI), or an engine family that runs
  unusually fast or slow on it (Berserk on Apple silicon: up to +0.87 log2, 2.5
  sigma), can be further off.
* **NNUE engines may come out low.** The only NNUE families in the range are
  Zahak and Halogen, both in the fit, and no held-out NNUE engine in the range was
  available. With its family left out, Zahak came out -277 (6.2), -99 (8.0) and
  -152 (10.0); Zahak 6.2 came out 209-248 low on the CI runners; Halogen 11 -10.
  The family-left-out bias of the whole 2500+ band is -56. An NNUE engine may be
  underestimated by that much or more, within the interval or not.
* **On fast computers engines may use less of the short move time** (fixed costs
  per search): Halogen 11 used 62-76% on the CI runners, Zahak 6.2 82-91%,
  Berserk 3.2.0 85% on the M4 (94% on the calibration machine). The time-use check warns below 80%; the estimate is not
  corrected for it (25% less search is about 65 Elo).
* **A binary for the wrong architecture** (the x86-64 build under Rosetta on a
  Mac, or the x64 Windows build on an ARM laptop) runs the reference engines
  emulated: the speed measurement is then off. Use the build for your computer.
* **Intel hybrid CPUs (the reporter's i7-12700) have not been run.** The CPU choice
  on them is tested on a synthetic i7-12700 topology and the Windows code runs in
  CI on Windows runners, which are not hybrid. If Windows reports no
  EfficiencyClass difference, all cores count as one class; the speed checks
  during the test then still catch an engine that lands on an E-core.
* **Virtual machines and WSL2** often expose neither `cpu_capacity` nor the
  maximum clock: all vCPUs count as one class, and pinning to a vCPU doesn't fix
  the physical core under it. The speed checks are what protects such runs.
* On the calibration machine itself the reference engines read 0.96-1.08x the
  recorded speed in the final validation runs (one estimate at a time, with the
  machine's other programs running; the recordings were made with three engines
  running at once), so engines there got 470-520 ms per position instead of 500:
  the move time follows the machine's speed at the time, which is what it is for.
* A speed reading past 7% is taken again and the smaller change kept, which makes
  the drift term slightly smaller than the readings; the reference engines'
  disagreement (half its log2) is added to the speed term on top of sigma, which
  already includes typical disagreement: both are small (a few Elo).
* macOS release builds: the bundled reference engines are signed with the app;
  starting them from a quarantined download was not tested. If they can't start,
  the estimate falls back to the perft benchmark with a wider interval and says so.
* Stopping a test takes up to about 10 s while a reference engine's search runs.
* It measures move choice at about 0.5 s per move on the calibration machine,
  single-threaded. Time management, pondering, multi-threaded search and
  robustness in games are not measured.

## Reproducing

[`tools/elo/pipeline.sh`](../tools/elo/pipeline.sh) rebuilds everything from
scratch (downloads, candidate positions, reference analysis, engine builds, engine
runs, cross-validation, final fit and selection); every step is resumable. It runs
every engine on every candidate in one go on one machine. Model v2 was made in two
steps on the same machine: the model v1 run, then the new engines on the 1040
candidates any v1 engine failed, with `run_engines.py --speed` (the reference
engines' recorded searches from the v1 run) so that both runs had the same amount of
search. Engines marked `heldOut` or `aboveScale` in `data/elo/engines.json` are kept
out of every fit by the tools themselves. The
Python tools need `tools/elo/requirements.txt`. The C++ estimator
(`src/elo_estimator.*`) is tested against the Python fit
(`tests/data/elo_fixture.json`, same θ within 1e-6).

On another computer: `tools/elo/replay_set.py` picks recorded searches of calibration
engines (`data/elo/replay_set_2026-11-02.json`), `tools/elo/speed_replay.py --set`
repeats them and `tools/elo/speed_proxy.py` compares the engines' speed ratios with
the reference engines'. The "Elo validation" workflow
(`.github/workflows/elo-validation.yml`, `tools/elo/ci_validation.py`) does that on
GitHub's Linux, Windows and macOS runners with engines built there from their pinned
tags, and estimates them; its results are in the run's annotations. The
cross-validation with the engines above 3600 in the fit is
`data/elo/cv_with_above_scale_2026-11-02.json`.
