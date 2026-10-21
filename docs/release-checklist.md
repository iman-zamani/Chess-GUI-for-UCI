# Release checklist (for the maintainer)

The code is ready for a 2.0.0 release; nothing has been tagged or published.

## Release
1. Check that `project(... VERSION 2.0.0 ...)` in `CMakeLists.txt` matches the tag you
   will create, and update the date in `CHANGELOG.md`.
2. Tag and push: `git tag v2.0.0 && git push origin v2.0.0`. The release workflow builds the
   Windows zip, the macOS DMGs (Intel and Apple Silicon), the Linux AppImage, .deb and .rpm,
   and the standalone `chess-cli-*` archives, and creates the GitHub release.
3. After the release exists, the GitHub Action (`uses: iman-zamani/Chess-GUI-for-UCI@v2.0.0`)
   can download `chess-cli`. Consider a moving `v2` tag for it.

## Repository settings (suggested)
- Description: "A chess GUI for developing UCI engines: SPRT and matches, perft bisection,
  UCI compliance checks, a position-based Elo estimate, and a CLI + GitHub Action."
- Topics: `chess`, `chess-gui`, `uci`, `chess-engine`, `engine-testing`, `sprt`, `perft`,
  `chess960`, `pgn`, `cpp17`, `sfml`, `imgui`.
- Social preview: a screenshot of the match screen with a running SPRT.

## Distribution (each needs an account or review on the other side)
- **macOS notarization**: the DMGs are unsigned, so Gatekeeper warns. Signing and notarizing
  needs an Apple Developer ID (`codesign`, `xcrun notarytool`); the workflow can do it with
  the certificate and an app-specific password stored as repository secrets.
- **Homebrew**: a tap (`homebrew-tap` repository) with a cask for the DMG and a formula for
  `chess-cli`; later, homebrew/cask once the project meets its notability rules.
- **winget**: a manifest in microsoft/winget-pkgs pointing at the Windows zip
  (`wingetcreate new <url>`).
- **Scoop**: a manifest in your own bucket (or a PR to the extras bucket).
- **AUR**: a `chess-gui-for-uci` PKGBUILD building from the release tarball (and/or `-bin`).
- **Flathub**: a Flatpak manifest; the app reads Resources next to the binary, so install
  them to `/app/bin/Resources` or add a resource path for Flatpak.
