#!/usr/bin/env python3
"""Validation of the Elo estimator on another computer (the CI runners: Linux and Windows on
x86-64, macOS on Apple silicon), in three steps:

  build     builds held-out and NNUE calibration engines from their pinned tags (the commit
            is checked against data/elo/engines.json); an engine that doesn't build here is
            skipped and reported
  replay    speed_replay.py --set: the engines repeat searches recorded on the calibration
            machine (data/elo/replay_set_2026-11-02.json); speed_proxy.py: how far each
            engine's speed ratio is from the reference engines' (the speed term's sigma)
  estimate  chess-cli elo-estimate --quick on each engine, with the shipped model and the
            real speed measurement; correct = the 95% interval contains the CCRL Blitz
            rating (a bound: the rating is on its side)

Each step prints GitHub annotations (::notice) with its results, and writes WORK/*.json.
Usage: ci_validation.py build|replay|estimate --work DIR [--cli build/chess-cli] [--ref DIR]
"""
import argparse, json, os, platform, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WIN = platform.system() == "Windows"
EXE = ".exe" if WIN else ""
OS = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}[platform.system()]
MAKE = "mingw32-make" if WIN and shutil.which("mingw32-make") else "make"
# Windows: Git's bash (not WSL's), for the build commands
BASH = next((p for p in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe")
             if os.path.isfile(p)), "bash") if WIN else None

# id -> (build command per OS ("*" for all), binary). Held-out engines (Weiss, Fatalii,
# Berserk 3.2.0) and NNUE calibration engines (Zahak 6.2-10.0, Halogen 11), plus Stash as
# a plain C engine.
GUARD = (r"""grep -rlE '^#\s*include\s*<(immintrin|x86intrin|nmmintrin|xmmintrin|smmintrin)\.h>' . """
         r"""| xargs perl -pi -e 's/^(#\s*include\s*<(immintrin|x86intrin|nmmintrin|xmmintrin|smmintrin)\.h>.*)$/#if defined(__x86_64__) || defined(_M_X64)\n$1\n#endif/' ; """)
CC = "clang" if OS == "macos" else "gcc"
ZAHAK = ("%s build || ( (test ! -f netgen/nn.go || go run -ldflags \"-X 'main.netPath=default.nn'\" netgen/nn.go) "
         "&& go build -gcflags -B -o bin/zahak%s ./zahak )" % (MAKE, EXE))
RECIPES = {
    "fatalii-0.5.0": ({"*": "cargo build --release --bin fatalii"}, "target/release/fatalii"),
    "fatalii-0.4.0": ({"*": "cargo build --release --bin fatalii"}, "target/release/fatalii"),
    # as on the calibration machine: `make build` (it turns the network into Go code, then
    # builds without bounds checks); else the same two steps by hand
    "zahak-10.0": ({"*": ZAHAK}, "bin/zahak"),
    "zahak-8.0": ({"*": ZAHAK}, "bin/zahak"),
    "zahak-6.2": ({"*": ZAHAK}, "bin/zahak"),
    "weiss-1.4": ({"*": "%s -C src basic CC=%s WARN=-Wall" % (MAKE, CC)}, "src/weiss"),
    "weiss-2.0": ({"*": "%s -C src basic CC=%s WARN=-Wall" % (MAKE, CC)}, "src/weiss"),
    "weiss-1.0": ({"*": "%s -C src CC=%s WARN=-Wall" % (MAKE, CC)}, "src/weiss"),
    "berserk-3.2.0": ({"macos": GUARD + "make -C src CC=clang EXE=berserk POPCOUNT=-DPOPCOUNT",
                       "*": "cd src && gcc -std=gnu11 -O3 -march=native -fcommon -DNDEBUG *.c -o berserk%s -lm -pthread" % EXE},
                      "src/berserk"),
    "halogen-11": ({"*": "%s -C src EXE=halogen" % MAKE}, "src/halogen"),
    "stash-21.0": ({"macos": GUARD + "make -C src CC=clang", "*": "%s -C src CC=gcc" % MAKE}, "src/stash-bot"),
}
ESTIMATE = ["fatalii-0.5.0", "fatalii-0.4.0", "weiss-1.4", "weiss-2.0", "berserk-3.2.0",
            "zahak-10.0", "zahak-6.2", "halogen-11"]


def engines_json():
    return {e["id"]: e for e in json.load(open(os.path.join(ROOT, "data", "elo", "engines.json")))["engines"]}


def notice(title, msg):
    print("::notice title=%s::%s" % (title, msg.replace("\n", " ")), flush=True)


def build(a):
    meta = engines_json()
    src = os.path.join(a.work, "src")
    os.makedirs(src, exist_ok=True)
    status, report = {}, []
    for eid, (cmds, binary) in RECIPES.items():
        e = meta[eid]
        d = os.path.join(src, eid)
        log = os.path.join(a.work, eid + ".build.log")
        t0 = time.time()
        with open(log, "w") as f:
            ok = subprocess.call(["git", "clone", "-q", "--depth", "1", "--branch", e["tag"], e["repo"], d],
                                 stdout=f, stderr=subprocess.STDOUT) == 0
            commit = subprocess.run(["git", "-C", d, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip() if ok else ""
            if ok and commit != e["commit"]:
                f.write("tag %s is %s, expected %s\n" % (e["tag"], commit, e["commit"]))
                ok = False
            if ok:
                cmd = cmds.get(OS, cmds["*"])
                f.write("$ %s\n" % cmd); f.flush()
                ok = subprocess.call([BASH, "-c", cmd] if WIN else cmd, shell=not WIN, cwd=d, stdout=f,
                                     stderr=subprocess.STDOUT) == 0
        path = next((p for p in (os.path.join(d, binary + EXE), os.path.join(d, binary)) if os.path.isfile(p)), None)
        if ok and path:
            status[eid] = {"binary": os.path.abspath(path), "commit": commit}
            report.append("%s ok (%ds)" % (eid, time.time() - t0))
        else:
            report.append("%s FAILED" % eid)
            print("---- %s build log (tail)" % eid)
            print("".join(open(log, errors="replace").readlines()[-25:]))
    json.dump(status, open(os.path.join(a.work, "status.json"), "w"), indent=1)
    notice("Engine builds (%s)" % OS, "; ".join(report))


def replay(a):
    status = json.load(open(os.path.join(a.work, "status.json")))
    for r in ("stockfish-11", "blunder-8.0.0"):
        status[r] = {"binary": os.path.abspath(os.path.join(a.ref, r + EXE))}
    st = os.path.join(a.work, "status_replay.json")
    json.dump(status, open(st, "w"), indent=1)
    out = os.path.join(a.work, "replay.json")
    order = ["stockfish-11", "blunder-8.0.0"] + [e for e in RECIPES if e in status]
    rc = subprocess.call([sys.executable, os.path.join(HERE, "speed_replay.py"), out, "--status", st,
                          "--set", os.path.join(ROOT, "data", "elo", "replay_set_2026-11-02.json"),
                          "--repeats", "2", "--engines"] + order)
    if not os.path.exists(out):
        notice("Speed replay (%s)" % OS, "speed_replay.py wrote nothing (exit %d)" % rc)
        return
    sys.path.insert(0, HERE)
    import speed_proxy
    data = json.load(open(out))
    if data.get("errors"):
        notice("Speed replay (%s): engines that failed" % OS, "; ".join("%s (repeat %d): %s" % (x["engine"], x["repeat"], x["error"][:200])
                                                                   for x in data["errors"]))
    res = speed_proxy.analyse(data)
    sp = res["speed"]
    notice("Speed replay (%s): ratios" % OS, "; ".join("%s %.3f (%s %d/%d)" % (e, sp[e], res["method"][e], res["kept"][e], res["total"][e])
                                                      for e in sorted(sp, key=lambda x: sp[x])))
    notice("Speed replay (%s): log2 from the references" % OS, "; ".join("%s %+.3f" % (e, v) for e, v in sorted(res["dev"].items())))
    for name, rms, n, rmsf, nf, mx, worst in res["summary"]:
        notice("Speed replay (%s): %s" % (OS, name), "RMS %.3f over %d engines, family-weighted %.3f over %d families, "
               "max %.3f (%s)" % (rms, n, rmsf, nf, mx, worst))
    unused = [e for e in res["total"] if e not in sp]
    if unused:
        notice("Speed replay (%s): not repeatable" % OS, ", ".join(unused))


def estimate(a):
    meta = engines_json()
    status = json.load(open(os.path.join(a.work, "status.json")))
    results = []
    for eid in ESTIMATE:
        if eid not in status:
            notice("Elo validation (%s) %s" % (OS, eid), "not built here")
            continue
        out = os.path.join(a.work, eid + "_q1.json")
        t0 = time.time()
        with open(out, "w") as f, open(os.path.join(a.work, eid + "_q1.log"), "w") as lg:
            rc = subprocess.call([a.cli, "elo-estimate", "--engine", "cmd=" + status[eid]["binary"], "--quick", "--json"],
                                 stdout=f, stderr=lg)
        try:
            j = json.load(open(out))
        except Exception as ex:
            notice("Elo validation (%s) %s" % (OS, eid), "no JSON (exit %d): %s" % (rc, ex))
            continue
        d = j["diagnostics"]
        ccrl = meta[eid]["ccrl"]["rating"]
        if j["status"] == "ok":
            correct = abs(j["elo"] - ccrl) <= j["ci95"]
            res = "%d +/- %d (error %+d)" % (j["elo"], j["ci95"], j["elo"] - ccrl)
        elif j["status"] == "above_range":
            correct, res = ccrl >= j["bound"], "at least %d" % j["bound"]
        elif j["status"] == "below_range":
            correct, res = ccrl <= j["bound"], "at most %d" % j["bound"]
        else:
            correct, res = None, "no number: " + (d["failures"][0] if d["failures"] else "?")
        kind = "held out" if meta[eid].get("heldOut") else "calibration engine"
        speeds = ", ".join("%s %.2fx" % (x["id"], x["speed"]) for x in d["speedStart"]["engines"] if x.get("speed"))
        drift = max([abs(c[k]) for c in d["speedChecks"] for k in ("reference", "own") if c.get(k) is not None] or [0])
        msg = ("%s (%s, CCRL %d): %s; correct: %s; %d positions, %d ms/position, speed %s; largest speed change %.3f log2; "
               "probe %s; time use %.2f; cpu %.2f; idle cpu %.2f; placement: %s; warnings: %s; %ds"
               % (eid, kind, ccrl, res, {True: "yes", False: "NO", None: "-"}[correct], j["positions"], j["movetimeMs"],
                  speeds, drift, d.get("ownProbe") or "-", d["timeUse"], d["cpuRatio"], d.get("idleCpu", -1),
                  d["placement"], len(d["warnings"]), time.time() - t0))
        notice("Elo validation (%s) %s" % (OS, eid), msg)
        results.append({"engine": eid, "ccrl": ccrl, "kind": kind, "status": j["status"], "elo": j["elo"], "ci95": j["ci95"],
                        "bound": j["bound"], "correct": correct, "os": OS})
    json.dump(results, open(os.path.join(a.work, "estimates.json"), "w"), indent=1)
    print(json.dumps(results, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("step", choices=["build", "replay", "estimate"])
    ap.add_argument("--work", required=True)
    ap.add_argument("--cli", default=os.path.join(ROOT, "build", "chess-cli" + EXE))
    ap.add_argument("--ref", default=os.path.join(ROOT, "Resources", "reference"))
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    {"build": build, "replay": replay, "estimate": estimate}[a.step](a)


if __name__ == "__main__":
    main()
