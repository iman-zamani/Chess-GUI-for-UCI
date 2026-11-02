# Reference engines

The Elo estimator measures how fast this computer runs chess engines by
replaying searches that two calibration engines made on the calibration
machine (see `src/elo_speed.hpp` and `docs/elo-estimator.md`). The engines are
separate programs, run over UCI, and are built from pinned source tags by

    python3 tools/build_reference_engines.py

which puts them here, with their licences, their complete source code
(`<engine>-source.tar.gz`) and the build commands (`SOURCES.txt`):

| File | Engine | Licence | Source |
|---|---|---|---|
| `stockfish-11` | Stockfish 11 | GPL-3.0 | https://github.com/official-stockfish/Stockfish, tag `sf_11` |
| `blunder-8.0.0` | Blunder 8.0.0 | MIT | https://github.com/algerbrex/blunder, tag `v8.0.0` |

Release packages include them, with their source archives. Stockfish 11 is
GPL-3.0 and this program GPL-2.0: they are separate programs that talk over UCI
(aggregation), and Stockfish 11 is distributed with its complete source under
its own licence. Without them the estimator falls back to its internal
benchmark, says so, and reports a wider interval.
