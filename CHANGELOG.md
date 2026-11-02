# Changelog

## 1.2.0 (2026-11-02)

### Elo estimator
- The move time is now set from this computer's speed as measured with two reference engines
  that ship with the program (Stockfish 11 and Blunder 8.0.0, built from pinned tags, with their
  source): they repeat searches recorded during calibration, after a 30-second warm-up. The
  internal perft benchmark used before misjudged engine speed by up to 2x, which made estimates
  too low (or too high) on some computers.
- The speed is checked again every minute during the test and after it: the reference engines
  repeat the same searches, and the engine under test repeats one fixed search of its own. A
  change of speed (throttling, other load, a slower core) gives a warning, or no number; a
  reading past the warning level is taken again first, so one passing slowdown doesn't.
- Engines run pinned to one core of the fastest class on Linux and Windows (a P-core on hybrid
  CPUs), the least busy one, with a lock per physical core so that two estimates at once use
  different cores; on Windows the engines and the program are opted out of power throttling. On
  Apple silicon the share of each engine's time on performance cores is measured and checked.
- No number outside the calibrated range (1578-3487 for model v2): an estimate past an end is a
  bound ("above the calibrated range: at least 3487", or "at least" the lower end of its
  interval when that reaches into the range). Stockfish 19 used to get about 4500. One rule, with
  the same interval, decides the result, the quick test's early stop and the running estimate.
- Model v2: 23 more calibration engines from 13 new families, most of them NNUE, run with the
  speed-matched protocol. Zahak, Shallow Blue and Halogen 11 join the fit (32 engines,
  9 families); Fatalii, Koivisto, Obsidian, Berserk 3.2.0 and the whole Weiss family are held
  out for validation. Of the 12 engines rated above 3600, 11 solve 94-98% of the positions, more
  than these positions can tell apart, so they only serve to check that such engines are reported
  above the range.
- Every run is checked (speed changes, time and CPU use, an engine that busy-waits for input,
  illegal moves); a run that fails a check gets no number (`chess-cli` exit code 4), borderline
  ones a warning. The 95% interval includes the uncertainty of the speed measurement (from a
  replay experiment with 25 calibration engines, 17 of them usable, checked on other machines).
  A move time that had to be clamped corrects the estimate. A stopped test gives no result.
- Results (JSON schema 2) carry a status, the bound, and diagnostics: speed per reference
  engine, CPU placement, every speed check, time and CPU use, performance-core share, and time,
  nodes and depth of every answer.

### Build
- `Resources` is copied next to the binaries on every build (also for `chess-cli`-only builds);
  the macOS bundle's nested reference engines are signed. The Linux reference engines are
  linked statically.
- CI: an Elo estimator job on Linux, macOS and Windows estimates Blunder 8.0.0 with the real
  speed measurement; an "Elo validation" workflow (on demand) builds held-out and NNUE engines
  on the three runners, replays recorded searches and estimates them; the arm64 macOS release
  build moved to the macOS 15 runner (macOS 14 is retired).

## 1.1.1 (2026-10-22)

### Fixed
- macOS: the downloaded app was reported as "damaged" and could not be opened. Its data
  files sat next to the executable, so the bundle had no valid signature. They now live in
  `Contents/Resources`, and the whole bundle is signed (ad hoc). The app is still not
  notarized: on first launch use **Open Anyway** in System Settings > Privacy & Security.

## 1.1.0 (2026-10-22)

A rebuild aimed at engine developers.

### Testing engines
- UCI compliance checker: 13 protocol checks with explanations and the engine log.
- Perft bisection: finds the position and the missing or extra moves behind a perft mismatch.
- UCI console (Ctrl+U): every running engine, live, with a command line.
- Search statistics per engine move (depth, seldepth, nodes, NPS, hash, time) with graphs.
- Position-based Elo estimator (GUI and `chess-cli elo-estimate`) with a model calibrated on 30
  engines (724 positions) and its calibration pipeline. Cross-validated error about 60 Elo from
  2500 CCRL Blitz up; much larger below, where a warning is shown (see docs/elo-estimator.md).

### Matches
- Games in parallel, colour-swapped game pairs, match / round-robin / gauntlet.
- Opening books: EPD (with opcodes), PGN (first N plies), FEN lists; sequential or seeded random.
- Adjudication: resign, draw, maximum length, Syzygy tablebases (Fathom).
- Elo, normalized Elo, LOS and pentanomial statistics; SPRT (GSPRT, fastchess conventions).
- Presets including "dev vs base", resumable runs, PGN with `[%eval]` / `[%clk]`, JSON export.
- The computer is kept awake during matches; games interrupted by system sleep are replayed.

### Command line and CI
- `chess-cli`: match, sprt, perft, perft-bisect, uci-check, analyse, elo-estimate, bench.
- A GitHub Action for engine repositories (`action.yml`).

### Chess and analysis
- Chess960: X-FEN / Shredder-FEN, castling rules, start positions, Chess960 matches.
- Variation tree: variations, comments and NAGs, with PGN import and export.
- Clickable engine lines, game review with move marks, accuracy and an eval graph.

### Interface
- The interface moved to Dear ImGui; native file dialogs; HiDPI.
- Piece sets (cburnett, merida, fantasy, celtic) and your own SVG sets; board themes.
- Move animation, check highlight, resizable board, optional move sounds, shortcuts overlay (F1).
