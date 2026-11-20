# Elo from games

**Tools > Elo from games** (or `chess-cli elo-games`) rates an engine from real games
against Stockfish 11 set to fixed strength levels. It is much slower than the
[estimate from positions](elo-estimator.md), but it measures everything that counts in a game,
time management included, at the time control you choose. Every engine rates higher with
more time against these levels (their strength hardly changes with time; see below), so
compare an engine with itself or with another engine at the same time control rather than
across time controls.

## How it works

1. **The opponent.** Stockfish 11 ships with the program (`Resources/reference`, built from
   the `sf_11` tag; its source is included). With `UCI_LimitStrength` on, it plays at the
   strength set by `UCI_Elo` (1350-2850). Its source describes the scale: "anchoring to CCRL
   Elo (goldfish 1.13 = 2000) and a fit through Ordo derived Elo for match (TC 60+0.6) results"
   ([src/search.cpp at sf_11](https://github.com/official-stockfish/Stockfish/blob/sf_11/src/search.cpp)).
   The levels were fitted on matches between Stockfish's own levels, so their spacing is
   self-play spacing; the fit check below tells you whether it suits your engine. Any other
   engine with `UCI_LimitStrength` and `UCI_Elo` can be the opponent instead; the result is
   then on that engine's scale.
2. **The games.** Your engine plays a gauntlet against Stockfish at several levels, in
   colour-swapped pairs from an opening book (by default the 50 well-known openings in
   `Resources/books/openings.pgn`), several games at a time. This is the same match runner
   as the Match screen and `chess-cli match`: clocks, time losses, crashes and a computer that
   sleeps are handled the same way.
3. **The levels.** *Automatic* (the default) plays about 30% of the games (at least a pair per
   level) at five levels spread over the whole range, fits a first estimate, and plays the
   rest at three levels around it, 200 Elo apart, where a game says the most. When the first
   estimate is past the end of the range, the rest are played at the end level. You can also
   give the levels yourself; the games are then shared equally (the odd pairs go to the middle
   levels).
4. **The fit.** The rating is the maximum-likelihood fit of the logistic Elo model (expected
   score `1 / (1 + 10^((level - rating) / 400))`, a draw counting half) to the scores at all
   levels together. The 95% interval comes from the model's information, scaled by how much
   the pair scores really vary around the fit (draws make games less variable than the model
   assumes; a level the fit doesn't match makes them more). That variation is estimated
   from the pairs themselves, so the multiplier is Student's t on the number of pairs rather
   than 1.96, which widens the interval of a short run. Past the levels the likelihood is
   lopsided, so there the interval is the likelihood-ratio one, shown as "from ... to".
   In simulated runs of the automatic plan (3000 runs each, no draws to many draws) the
   interval held the true rating 93-97% of the time for ratings 1500-2700 (20 to 1000 games),
   with a bias of a few Elo at most (up to about 4 near the ends of the range in runs of 20-50
   games), and the interval or bound 93-97% of the time for ratings
   from 900 to 3300, past both ends of the range (the lowest, 92.6%, for 50-game runs of
   engines 450 Elo past an end). `tests/test_ladder.cpp` checks the coverage. Simulations
   assume the levels follow the logistic model exactly, which real levels only do
   approximately.

## What the result means

- **A rating on Stockfish 11's UCI_Elo scale, at this time control, on this computer.**
  Each level picks its move from the search at one depth (1 + its skill level: depth 1 at
  1350, 8 at 2100, 14 at 2500, 19 at 2850), so its strength hardly depends on the time
  control or the computer, as long as the search gets that deep. Your engine's strength
  does, so a faster computer or a longer time control gives it a higher number. That is
  the point of choosing the time control.
- **Depth shortfall.** At bullet the stronger levels often run out of time before that
  depth and then play weaker than their Elo, which makes your engine's rating too high. The
  run counts, from Stockfish's own reports, the moves where this happened: the **Short**
  column of the table (and the JSON). At time controls faster than 60+0.6 it warns when that
  was more than 5% of a level's moves, out of at least 100. After a resume, the column counts
  only the moves played since. In runs on an Apple M-series laptop: at 5+0.05, level
  2500 fell short on 46% of its moves and level 2100 on 1%; at 60+0.6, level 2500 on none and
  level 2850 on 40%. 60+0.6 is the time control the levels were fitted at, so a shortfall
  there was part of the calibration games too and is in the levels' Elo, but how often it
  happened on the calibration machine isn't known. So for the strongest levels the number is
  only as good as the match between this computer and time control and the calibration's: a
  slower computer at 60+0.6 makes them weaker than their Elo (your rating comes out high),
  and a much slower time control lets them reach their depth more often than in the
  calibration (your rating comes out low). Levels with no shortfall (Short 0%, all levels up
  to about 2500 at 60+0.6 here) don't have this problem.
- **The interval is statistical only.** How far each level is from its nominal Elo is not
  known and is not included.
- **Bounds.** An engine that wins every game gets "above X": X is the rating at which a
  clean sweep would happen only 5% of the time. The same holds for "below X". A fit more than
  300 Elo past the strongest or weakest level played is reported as a bound too: the near end
  of its 95% interval. Stockfish 11's range is 1350-2850, so engines much stronger than 2850
  or much weaker than 1350 get a bound, not a number. (Because the data decide between a
  number and a bound, a bound reported just past that threshold is right less often than
  95% on its own; the number-or-bound answer as a whole holds the true rating at the rates
  given above.) A number more than 100 Elo past the
  levels played is marked as extrapolated and shown with its interval ("3010 (2890 to
  3190)").
- **Fit check.** With three or more levels, a chi-square test asks whether one rating explains
  the scores at every level. A warning means your engine does better against some levels than
  the others predict, for example much better against weak play than against strong play.
- **Performance per level.** The table shows the rating each level's games alone give (from
  10 games against it, with its own interval). When the fit check passes, they agree with the
  fitted rating within their intervals.

## How many games

The interval narrows with the square root of the number of games. Mean 95% half-widths in
the simulations above, from many draws to none: ±170-205 Elo after 20 games, ±92-119 after
50, ±63-84 after 100, ±36-48 after 300 and ±19-26 after 1000. A 60+0.6 game takes about three minutes when
both clocks are used up over 50 moves each; most games take less, long games more. The GUI
shows that figure for the whole run before it starts.

Every parallel game keeps a Stockfish process for each level of the stage running (16 MB
of hash each by default); a reference engine you give with a large hash uses that much per
process.

## Command line

```
chess-cli elo-games --engine cmd=./myengine --games 300 --concurrency 8 --state run.json
chess-cli elo-games --engine cmd=./myengine --tc 10+0.1 --levels 1800,2000,2200 --games 120 --pgn games.pgn --json
chess-cli elo-games --engine cmd=./myengine --reference cmd=./stockfish-17 name=SF17
```

Options: `--reference E` (default the bundled Stockfish 11), `--tc BASE[+INC]` (default 60+0.6),
`--games N` (200), `--levels auto|L,L...`, `--concurrency N` (half the cores), `--book FILE`,
`--book-plies N`, `--seed S`, `--margin MS`, `--pgn FILE`, `--state FILE`, `--json`, `--verbose`.
With `--state`, the same command resumes a stopped run (the seed is then 1 unless given, so the
openings are the same). A run that finishes removes its state files, so the same command then
plays a new run. If the engine or the reference engine was rebuilt (its file's size or time
changed), or other settings are given, the old games are not used and a warning says so. The
check looks at the file given as the engine (`cmd=`): for an engine started through a script
or another program, or found on the PATH, it can't see a rebuild, so start a new run (without
the state file) after rebuilding such an engine. The JSON holds every setting, the seed included. Exit codes: 0 a result
(a rating or a bound), 2 bad options or an engine that could not be started, 5 the run stopped
on an error (the games so far are printed), 130 interrupted (the partial result is printed).

In the GUI, **Resume the stopped run** continues a run that was stopped or cut short, with
the same settings.

## Compared with the estimate from positions

| | Estimate from positions | Elo from games |
|---|---|---|
| Time | minutes | an hour or more |
| Measures | move choice at a fixed time per move | everything, time management included |
| Time control | fixed by the model | yours |
| Scale | CCRL Blitz (single CPU), calibrated on 32 engines | Stockfish 11's UCI_Elo (anchored to CCRL) |
| Error | includes the model's cross-validated error | statistical only |
