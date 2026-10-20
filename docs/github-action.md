# GitHub Action: check your engine in CI

The repository root contains a composite GitHub Action that runs `chess-cli`
against an engine your workflow has just built:

* `uci-check`: UCI protocol compliance: handshake, `isready` (idle and during a
  search), `ucinewgame`, `quit`, `stop`, the `go` modes, positions, no legal
  moves, mate scores, `setoption`, ponder and time management. Fails the job on
  any FAIL.
* `perft`: your engine's `go perft` against the verified move-generator
  reference. Fails the job on any mismatch; run `chess-cli perft-bisect`
  locally to find the bug.
* `elo-estimate` (optional): a position-based Elo estimate, written to the job
  summary and to the `elo` output. It is an estimate from test positions, not
  a rating from games.

**Requirement:** the action downloads `chess-cli` from a GitHub release of
Chess-GUI-for-UCI, so it works once a release with `chess-cli` archives has
been published (`chess-cli-linux-x86_64.tar.gz`, `chess-cli-macos-arm64.tar.gz`,
`chess-cli-macos-x86_64.tar.gz`, `chess-cli-windows-x64.zip`).

## Example

```yaml
# .github/workflows/engine-checks.yml in your engine's repository
name: Engine checks
on: [push, pull_request]

jobs:
  checks:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v6
      - name: Build the engine
        run: make -j            # whatever builds your engine
      - name: UCI and perft checks
        uses: iman-zamani/Chess-GUI-for-UCI@v1   # a release tag
        with:
          engine: ./my-engine
          checks: uci-check,perft
```

## Inputs

| Input | Default | Meaning |
|---|---|---|
| `engine` | (required) | Path to the engine executable. |
| `engine-args` | `""` | Command-line arguments for the engine, as one string. |
| `checks` | `uci-check,perft` | Any of `uci-check`, `perft`, `elo-estimate`, comma-separated. |
| `uci-check-args` | `--quick` | Extra arguments for `chess-cli uci-check` (e.g. `--only handshake,stop`). |
| `perft-args` | `--depth 4` | Extra arguments for `chess-cli perft` (e.g. `--suite my.epd`). |
| `elo-estimate-args` | `--quick` | Extra arguments for `chess-cli elo-estimate`. |
| `version` | `latest` | The Chess-GUI-for-UCI release to take `chess-cli` from. |
| `chess-cli` | `""` | Use this `chess-cli` executable instead of downloading one. |

Output: `elo` (only when `elo-estimate` ran).

Runners: Linux x86-64, macOS (Apple Silicon and Intel) and Windows x64.
The action itself is tested in this repository's CI against a mock engine.
