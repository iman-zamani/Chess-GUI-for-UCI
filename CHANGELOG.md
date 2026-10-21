# Changelog

## 2.0.0 (not released yet)

A rebuild aimed at engine developers.

### Testing engines
- UCI compliance checker: 13 protocol checks with explanations and the engine log.
- Perft bisection: finds the position and the missing or extra moves behind a perft mismatch.
- UCI console (Ctrl+U): every running engine, live, with a command line.
- Search statistics per engine move (depth, seldepth, nodes, NPS, hash, time) with graphs.
- Position-based Elo estimator (GUI and `chess-cli elo-estimate`) and its calibration pipeline;
  the calibrated model is not included yet (see docs/elo-estimator.md).

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
