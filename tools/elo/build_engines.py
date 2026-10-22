#!/usr/bin/env python3
"""Build the calibration engines listed in data/elo/engines.json.

Every engine is cloned at its pinned tag into WORKDIR/<id>, built with the
entry's shell command and checked: the binary must answer `uci` with an
`id name` that contains the family name and the version (case-insensitive),
so the build is the version CCRL rated. The commit the tag points at and the
id name are recorded (with --update they are written back to engines.json; a
pinned commit that no longer matches the tag fails the build). Engines are never redistributed;
only their answers to the test positions are kept.

An entry's "build", "binary" and "patches" are the default recipe (written for
macOS on Apple Silicon); "builds", "binaries" and "patchesFor" can override them
per platform, keyed by "<system>-<machine>" (e.g. "Linux-x86_64").

Usage: build_engines.py --workdir DIR [--only ID ...] [--jobs N] [--update]
"""
import argparse, json, os, platform, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Portability patches an entry may name in "patches" (applied before building;
# they never change what the engine computes).
PATCHES = {
    # x86 intrinsic headers included unconditionally, while the code that uses
    # them sits behind #ifdefs: include them only on x86
    "guard-x86-headers": r"""grep -rlE '^#\s*include\s*<(immintrin|x86intrin|nmmintrin|xmmintrin|smmintrin)\.h>' . """
                         r"""| xargs perl -pi -e 's/^(#\s*include\s*<(immintrin|x86intrin|nmmintrin|xmmintrin|smmintrin)\.h>.*)$/#if defined(__x86_64__) || defined(_M_X64)\n$1\n#endif/'""",
    # Bit-Genie 2's bench command calls std::max(long long, long), which only compiles where
    # int64_t is long long; spell out the type (the benchmark only, not the search)
    "bitgenie-bench-max": r"""sed -i.orig 's/std::max(1ll, elapsed)/std::max<long long>(1ll, elapsed)/' Bit-Genie/src/benchmark.cpp""",
}
ENGINES = os.path.join(HERE, "..", "..", "data", "elo", "engines.json")


def run(cmd, cwd, log, env=None):
    with open(log, "a") as f:
        f.write("$ %s\n" % cmd)
        f.flush()
        return subprocess.call(cmd, shell=True, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env)


def uci_name(binary):
    """Sends `uci`, waits for `uciok`, then `quit` (some engines drop input that
    arrives before they start reading). Returns (id name, error)."""
    import threading
    try:
        p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             text=True, cwd=os.path.dirname(binary))
    except Exception as e:
        return None, str(e)
    timer = threading.Timer(20, p.kill)
    timer.start()
    name = None
    try:
        p.stdin.write("uci\n"); p.stdin.flush()
        for line in p.stdout:
            if line.startswith("id name "):
                name = line[8:].strip()
            if line.strip() == "uciok":
                break
        p.stdin.write("quit\n"); p.stdin.flush()
        p.wait(timeout=5)
    except Exception:
        p.kill()
    finally:
        timer.cancel()
    return (name, None) if name else (None, "no 'id name' line before uciok")


def norm(s):
    """Lower case, single spaces."""
    return re.sub(r"\s+", " ", s.strip().lower())


def platform_key():
    """e.g. "Linux-x86_64" or "Darwin-arm64": an entry's "builds" may give a recipe for it."""
    return "%s-%s" % (platform.system(), platform.machine())


def recipe(e, field):
    """The entry's field for this platform (builds / binaries / patchesFor), else the default."""
    per = e.get({"build": "builds", "binary": "binaries", "patches": "patchesFor"}[field], {})
    return per.get(platform_key(), e.get(field, [] if field == "patches" else None))


def build(e, workdir, jobs, log):
    d = os.path.join(workdir, e["id"])
    if not os.path.isdir(os.path.join(d, ".git")):
        # a partial clone, then the commit the tag points at (some tags point at
        # another tag object, which `clone --branch` does not resolve reliably)
        if run("git clone -q --filter=blob:none --no-checkout %s %s" % (e["repo"], d), workdir, log) or \
           run("git -c advice.detachedHead=false checkout -q '%s^{commit}'" % e["tag"], d, log):
            return {"ok": False, "error": "clone/checkout failed (see %s)" % log}
    want = subprocess.check_output(["git", "rev-parse", e["tag"] + "^{commit}"], cwd=d, text=True).strip()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=d, text=True).strip()
    if commit != want:
        return {"ok": False, "commit": commit, "error": "checked out %s, but %s is %s" % (commit, e["tag"], want)}
    binary = os.path.join(d, recipe(e, "binary"))
    if not os.path.exists(binary):
        for p in recipe(e, "patches"):
            if run(PATCHES[p], d, log):
                return {"ok": False, "commit": commit, "error": "patch %s failed" % p}
        env = dict(os.environ)
        if run("nice -n 19 sh -c '%s'" % recipe(e, "build").replace("{jobs}", str(jobs)).replace("'", "'\\''"), d, log, env):
            return {"ok": False, "commit": commit, "error": "build failed (see %s)" % log}
    if not os.path.exists(binary):
        return {"ok": False, "commit": commit, "error": "no binary at " + recipe(e, "binary")}
    name, err = uci_name(binary)
    if name is None:
        return {"ok": False, "commit": commit, "error": "uci: " + err}
    ok = norm(e.get("uciFamily", e["family"])) in norm(name) and norm(e["version"]) in norm(name)
    return {"ok": ok, "commit": commit, "idName": name, "binary": binary,
            "error": None if ok else "id name %r does not name %s %s" % (name, e["family"], e["version"])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--update", action="store_true", help="write the built commits into engines.json")
    a = ap.parse_args()
    a.workdir = os.path.abspath(a.workdir)
    os.makedirs(a.workdir, exist_ok=True)
    data = json.load(open(ENGINES))
    status_file = os.path.join(a.workdir, "build_status.json")
    status = json.load(open(status_file)) if os.path.exists(status_file) else {}
    for e in data["engines"]:
        if a.only and e["id"] not in a.only:
            continue
        log = os.path.join(a.workdir, e["id"] + ".build.log")
        r = build(e, a.workdir, a.jobs, log)
        if r.get("commit") and e.get("commit") and e["commit"] != r["commit"]:
            r["ok"] = False
            r["error"] = "tag %s now points at %s, engines.json pins %s" % (e["tag"], r["commit"], e["commit"])
        status[e["id"]] = r
        print("%-22s %s %s" % (e["id"], "OK  " if r["ok"] else "FAIL", r.get("idName") or r.get("error")), flush=True)
        if a.update and r["ok"]:
            e.setdefault("commit", r["commit"])
            e["idName"] = r["idName"]
        json.dump(status, open(status_file, "w"), indent=1)
    if a.update:
        with open(ENGINES, "w") as f:
            json.dump(data, f, indent=1)
            f.write("\n")
    return 0 if all(status[e["id"]]["ok"] for e in data["engines"] if e["id"] in status) else 1


if __name__ == "__main__":
    sys.exit(main())
