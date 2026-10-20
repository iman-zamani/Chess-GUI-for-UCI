# Contributing

Thanks for helping. Bug reports, engine compatibility reports and pull requests
are all welcome.

## Reporting a problem with an engine

Most engine problems are easiest to diagnose from the protocol log:

1. Reproduce the problem, open **Tools > UCI console** (Ctrl+U) and copy the log
   of the engine, or run `chess-cli uci-check --engine cmd=/path/to/engine`.
2. Open an issue with the engine name and version, your OS, what you expected and
   the log.

## Building and testing

See [docs/building.md](docs/building.md). Before sending a pull request:

```
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Tests must not need the network or a real engine: use the mock engine in
`tests/mock_engine.cpp` (it has flags for most kinds of misbehaviour, see the top
of the file) and add flags to it when you need a new one.

## Code

- C++17. Match the style of the file you are editing: naming, braces, comment density.
- The core (`src/chess.*`, `src/uci_engine.*`, `src/tournament.*`, `src/testing.*`,
  `src/stats.*`, `src/elo_estimator.*`, ...) must not include SFML or ImGui: it is shared
  with the headless `chess-cli`. GUI code lives in `src/gui.*` and `src/ui/`.
- New dependencies: only with a GPL-2.0-compatible licence (MIT, BSD, zlib, Boost,
  OFL, public domain), pinned to an exact version or commit with a hash.
- Bundled assets (pieces, sounds, fonts) need a licence file next to them.
- One logical change per commit, with a subject line in the imperative
  ("Add SPRT engine to match runner").
- GUI changes: check the screen you touched (`chess-gui --script` can drive it and
  take screenshots; see `tests/gui_smoke.txt`).

## Licence

By contributing you agree that your work is distributed under the GNU General
Public License v2.0, the licence of this project.
