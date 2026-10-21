# Proposed issues

Texts for a few labelled issues, for the maintainer to create on GitHub.

---

### Show the opening name in the game screen
Labels: `good first issue`, `enhancement`

Use the ECO / opening name tags when a PGN has them, and show them under the
player names. Bonus: look the name up from the moves with a small bundled table
(the Lichess `chess-openings` data is CC0).

Where: `src/ui/game_screen.cpp` (`drawPlayers`), `src/game_tree.hpp` (`tags`).

---

### Copy the PV of an analysis line as SAN or UCI
Labels: `good first issue`, `enhancement`

A right-click menu on an engine line in the analysis panel with "Copy as SAN" and
"Copy as UCI moves", handy for pasting into bug reports.

Where: `GameScreen::drawAnalysis` in `src/ui/game_screen.cpp`.

---

### chess-cli: `bench` against an engine
Labels: `good first issue`, `cli`

`chess-cli bench --engine E` could send `bench` (Stockfish-style) or run a fixed
set of `go nodes` searches and print the engine's NPS, to compare builds.

Where: `src/cli.cpp`.

---

### Remember the window position
Labels: `good first issue`

The window size is stored in `settings.json`; the position is not. Save it on
exit and restore it when it is still on a connected screen.

Where: `src/gui.cpp`, `src/settings.hpp`.

---

### Match screen: crosstable for round-robins
Labels: `enhancement`, `match`

For three or more engines, a crosstable (engine x engine scores) next to the
standings, and in the JSON export.

Where: `src/tournament.cpp` (`resultsJson`), `src/ui/match_screen.cpp`.

---

### Opening book: Polyglot `.bin` support
Labels: `enhancement`, `match`

Read Polyglot books (random weighted moves up to N plies) as a third book format
next to EPD and PGN. The key format is documented and needs no third-party code.

Where: `src/openings.{hpp,cpp}`, with tests in `tests/test_openings.cpp`.
