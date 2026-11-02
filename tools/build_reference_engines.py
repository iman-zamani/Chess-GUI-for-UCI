#!/usr/bin/env python3
"""Builds the Elo estimator's reference engines into Resources/reference/.

The estimator measures a computer's speed by replaying searches that these
engines made during calibration (src/elo_speed.hpp). They are built from pinned
source tags, with the same kind of optimised build as on the calibration
machine (gcc/clang -O3 builds, the Go compiler for Blunder), and shipped as
separate programs next to their licences, their complete source code
(<engine>-source.tar.gz, as GPL-3.0 asks of a binary distribution) and the
exact build commands (SOURCES.txt):

  Stockfish 11   GPL-3.0  https://github.com/official-stockfish/Stockfish  tag sf_11
  Blunder 8.0.0  MIT      https://github.com/algerbrex/blunder             tag v8.0.0

Needs git, make and a C++ compiler (MinGW g++ on Windows), and Go (the calibration build used 1.26.10).
Usage: build_reference_engines.py [--out Resources/reference] [--work DIR] [--jobs N]
"""
import argparse, os, platform, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ENGINES = [
    {"id": "stockfish-11", "repo": "https://github.com/official-stockfish/Stockfish", "tag": "sf_11",
     "commit": "c3483fa9a7d7c0ffa9fcc32b467ca844cfb63790", "licence": "Copying.txt", "licenceName": "GPL-3.0"},
    {"id": "blunder-8.0.0", "repo": "https://github.com/algerbrex/blunder", "tag": "v8.0.0",
     "commit": "527906ccccdb0c603dc62e9ea1a885c0c7c333f3", "licence": "LICENSE", "licenceName": "MIT"},
]


def platform_key():
    s = platform.system()
    m = platform.machine().lower()
    arch = "arm64" if m in ("arm64", "aarch64") else "x86_64"
    return {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(s, s.lower()) + "-" + arch


def stockfish_build(src, key, jobs):
    """make command for Stockfish 11 on this platform (the calibration build was x86-64-modern, gcc)."""
    if key == "macos-arm64":
        # the recipe of the calibration engine builds on Apple silicon (data/elo/engines.json)
        return ["make", "-C", src, "all", "ARCH=general-64", "COMP=clang", "arch=arm64", "EXTRACXXFLAGS=-DUSE_POPCNT",
                "-j%d" % jobs]
    if key == "macos-x86_64":
        return ["make", "-C", src, "build", "ARCH=x86-64-modern", "COMP=clang", "-j%d" % jobs]
    # Linux: linked statically, so it starts on distributions older than the build machine
    if key == "linux-x86_64":
        return ["make", "-C", src, "build", "ARCH=x86-64-modern", "COMP=gcc", "EXTRALDFLAGS=-static", "-j%d" % jobs]
    if key == "linux-arm64":
        return ["make", "-C", src, "build", "ARCH=general-64", "COMP=gcc", "EXTRACXXFLAGS=-DUSE_POPCNT",
                "EXTRALDFLAGS=-static", "-j%d" % jobs]
    if key == "windows-x86_64":                  # COMP=mingw links statically
        mk = shutil.which("mingw32-make") or "make"
        return [mk, "-C", src, "build", "ARCH=x86-64-modern", "COMP=mingw", "-j%d" % jobs]
    sys.exit("no Stockfish 11 build for " + key)


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(cmd), file=sys.stderr, flush=True)
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "Resources", "reference"))
    ap.add_argument("--work", default=None, help="checkout directory (default: a temporary one)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()
    key = platform_key()
    exe = ".exe" if key.startswith("windows") else ""
    work = a.work or tempfile.mkdtemp(prefix="chessgui-ref-")
    os.makedirs(a.out, exist_ok=True)
    notes = ["Reference engines for the Elo estimator's speed measurement (src/elo_speed.hpp).",
             "Separate programs, run over UCI; built from these sources for " + key + ":", ""]
    for e in ENGINES:
        d = os.path.join(work, e["id"])
        if not os.path.isdir(d):
            run(["git", "clone", "--quiet", "--depth", "1", "--branch", e["tag"], e["repo"], d])
        commit = subprocess.run(["git", "-C", d, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        if e["commit"] and commit != e["commit"]:
            sys.exit("%s: tag %s is commit %s, expected %s" % (e["id"], e["tag"], commit, e["commit"]))
        target = os.path.join(a.out, e["id"] + exe)
        if e["id"].startswith("stockfish"):
            cmd = stockfish_build(os.path.join(d, "src"), key, a.jobs)
            run(cmd)
            shutil.copy2(os.path.join(d, "src", "stockfish" + exe), target)
        else:
            cmd = ["go", "build", "-trimpath", "-o", target, "./blunder"]
            run(cmd, cwd=d, env=dict(os.environ, CGO_ENABLED="0"))
        shutil.copy2(os.path.join(d, e["licence"]), os.path.join(a.out, "LICENSE-" + e["id"] + ".txt"))
        run(["git", "-C", d, "archive", "--format=tar.gz", "--prefix=%s/" % e["id"],
             "-o", os.path.abspath(os.path.join(a.out, e["id"] + "-source.tar.gz")), "HEAD"])
        notes.append("%s (%s): %s, tag %s, commit %s" % (e["id"], e["licenceName"], e["repo"], e["tag"], commit))
        shown = [x.replace(work, "<checkout>").replace(os.path.abspath(a.out), "<out>") for x in cmd if not x.startswith("-j")]
        notes.append("  source: %s-source.tar.gz; built with: %s" % (e["id"], " ".join(shown)))
        print("built " + target, file=sys.stderr)
    notes += ["", "The complete source code of each engine is in <engine>-source.tar.gz (the repository at the",
              "commit above); its licence is in LICENSE-<engine>.txt."]
    open(os.path.join(a.out, "SOURCES.txt"), "w").write("\n".join(notes) + "\n")


if __name__ == "__main__":
    main()
