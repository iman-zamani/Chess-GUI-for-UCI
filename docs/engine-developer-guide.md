# Engine developer guide

This guide walks through the tools for developing a UCI engine: getting your
engine running, checking the protocol, finding move-generator bugs, testing a
change with an SPRT, and the command-line equivalents for scripts and CI.

## 1. Add your engine

**Engines > Manage engines** (or the *Engines...* button on the home screen):
add the executable, optionally with command-line arguments, a working directory
(engines that load NNUE files or books by relative path need their own folder,
which is the default) and UCI options to set after the handshake. The list is
saved to `engines.json` in your configuration folder (macOS
`~/Library/Application Support/ChessGUIforUCI`, Linux
`~/.config/chess-gui-for-uci`, Windows `%APPDATA%\ChessGUIforUCI`).
Executables dropped into `Resources/engines/` are picked up automatically.

**Tools > UCI console** (Ctrl+U) shows every engine the GUI is running, with
the full two-way protocol log, and lets you type commands to any of them.

## 2. UCI compliance

**Engine testing tools > UCI compliance** runs 13 checks, each in a fresh engine
process where it matters, and reports PASS / WARN / FAIL with an explanation
and the engine log behind the result:

| Check | What it verifies |
|---|---|
| `handshake` | `uci` gets `id name`, `id author`, options and `uciok` |
| `isready-idle` | `isready` gets `readyok` when idle |
| `ucinewgame` | `ucinewgame` + `isready`, then a normal search |
| `quit` | the process exits within 1 s of `quit` |
| `isready-during-search` | `readyok` arrives while searching (`go infinite`) |
| `stop` | `go infinite` + `stop` gives a legal `bestmove` quickly |
| `go-modes` | movetime, depth, nodes, wtime/btime and movestogo searches return a legal move in time |
| `positions` | a 320-ply `position startpos moves ...` and unusual but legal FENs |
| `no-legal-moves` | checkmate / stalemate: no hang or crash; `bestmove (none)` or `0000` expected (other answers warn) |
| `mate-scores` | forced mates are reported as `score mate N` |
| `setoption` | every advertised option can be set, then `isready` |
| `ponder` | `go ponder` + `ponderhit`, and `go ponder` + `stop` |
| `time-management` | self-play games at 1s+0.01s and 10s+0.1s without losing on time (*Quick*: 1s+0.01s only); an overrun of up to 50 ms, which can be the GUI's own latency, only warns |

Command line: `chess-cli uci-check --engine cmd=./myengine [--quick] [--only handshake,stop]`
(exit code 1 when any check fails).

## 3. Move generation: perft and bisection

Your engine needs to answer `go perft N` with per-move counts (`e2e4: 20`) and a
total (`Nodes searched: 8902`), as Stockfish does. Common variants of the
format are understood.

**Move generator** tab: runs `go perft` on every position of a suite and
compares the totals with verified counts. The default suite is
`Resources/perftsuite.epd` (`<FEN> ;D1 n ;D2 n ...`); you can load your own.

**Perft bisection** finds the exact position where your generator is wrong:
it compares your engine's divide with the GUI's verified generator, descends
into the first move whose count differs, and repeats until it reaches a
position where the engine lists the wrong set of moves. A typical result:

> Move generator bug in `r3k2r/p1ppqpb1/1n2pnp1/1b1PN3/Pp2P3/2N2Q1p/1PPBBPPP/1R2K2R b Kkq a3 0 2`:
> missing legal move(s) b4a3. Reached by a1b1, a6b5, a2a4 from the start position.

Failed suite positions have a *Bisect* button. Command line:
```
chess-cli perft --engine cmd=./myengine --depth 4
chess-cli perft-bisect --engine cmd=./myengine --fen "<FEN>" --depth 5
```
`chess-cli perft --fen "<FEN>" --depth N --divide` (no engine) prints the
reference counts. Chess960 positions (X-FEN or Shredder-FEN) are supported; the
engine gets `UCI_Chess960` and castling in king-takes-rook notation.

## 4. Testing a change: SPRT

An SPRT (sequential probability ratio test) plays games until it can decide,
with error rates you choose, whether a change gains at least `elo1` (H1) or no
more than `elo0` (H0). It is how Stockfish's fishtest and OpenBench test
patches, and it needs far fewer games than a fixed-length match for the same
confidence.

1. Build the old version ("base") and the new one ("dev") and add both engines.
2. **Engine match / tournament**, then the **Dev vs base** preset: an SPRT on
   normalized Elo with `elo0 = 0`, `elo1 = 5`, `alpha = beta = 0.05`, at 10s+0.1s,
   with resign and draw adjudication. Pick dev as engine A and base as engine B.
3. Choose an opening book. Unbalanced books such as the UHO books from
   <https://github.com/official-stockfish/books> make engine games more decisive,
   which helps the test; every opening is played twice with colours swapped.
4. *Games in parallel*: keep *parallel games x 2 x engine threads* at or below
   your number of physical cores, or the engines take time from each other.
5. Start. The panel shows the LLR between its bounds (about -2.94 and 2.94 for
   alpha = beta = 0.05), the Elo and normalized Elo with 95% intervals, the LOS,
   and the game-pair (pentanomial) counts. The run stops by itself when H0 or
   H1 is accepted.

The statistics follow fastchess's conventions (pentanomial model over game
pairs, GSPRT with the exact maximum-likelihood formulas of Michel Van den
Bergh), and the unit tests compare them with values computed by fastchess's
code.

Long runs: with *resumable* on, the state is saved after every game; after a
crash or *Stop*, **Resume last run** continues. On macOS and Windows the
computer is kept from idle-sleeping while a match runs (closing a laptop lid
still sleeps it), and a game interrupted by a system sleep is replayed rather
than scored. Games are written to a PGN file as they finish,
with `[%eval]` and `[%clk]` comments.

Command line (exit code 0 for H1, 1 for H0, 3 if the game limit was reached):
```
chess-cli sprt --engine cmd=./dev name=dev --engine cmd=./base name=base \
  --tc 10+0.1 --book UHO_Lichess_4852_v1.epd --random --concurrency 4 \
  --elo0 0 --elo1 5 --pgn sprt.pgn --state sprt.state.json
```
Run the same command again to resume. Other options: `--mode roundrobin|gauntlet`,
`--resign CP:MOVES`, `--draw MOVE:CP:MOVES`, `--syzygy DIRS`, `--chess960`,
`--json`; see `chess-cli help`.

## 5. Estimating your engine's Elo without games

**Tools > Estimate engine Elo** (or `chess-cli elo-estimate`) runs your engine
on test positions and estimates its CCRL-Blitz-equivalent rating from which
positions it solves. It takes minutes instead of the thousands of games a real
rating needs, but it is an estimate from positions, not a rating from games:
time management, for example, is not measured. See
[elo-estimator.md](elo-estimator.md) for the method and its measured accuracy.

## 6. In CI

[github-action.md](github-action.md) shows how to run the UCI checks, perft
and the Elo estimate in your engine's GitHub Actions workflow.
