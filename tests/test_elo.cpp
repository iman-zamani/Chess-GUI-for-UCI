/* Chess-GUI-for-UCI — Elo estimator: parity with the Python fit, adaptive
 * testing on simulated answers, runs against the mock engine, the speed
 * measurement with (mock) reference engines, range statuses and run checks. GPL-2.0 */
#include "testlib.hpp"
#include "elo_estimator.hpp"
#include "chess.hpp"
#include "cpu_placement.hpp"
#include "uci_engine.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

using json = nlohmann::json;
static const std::string kMock = MOCK_ENGINE_PATH;

static json fixture(){
    std::ifstream f(std::string(CHESSGUI_SOURCE_DIR) + "/tests/data/elo_fixture.json");
    std::stringstream ss; ss << f.rdbuf();
    return json::parse(ss.str());
}

TEST_CASE("elo/theta-matches-the-python-fit"){
    json fx = fixture();
    EloModel m;
    std::string err;
    REQUIRE(m.loadJson(fx["model"].dump(), &err));
    int n = 0;
    for (auto& c : fx["cases"]){
        std::vector<std::pair<int, bool>> resp;
        for (auto& r : c["responses"]) resp.push_back({r[0].get<int>(), r[1].get<bool>()});
        ThetaEstimate t = estimateTheta(m, resp);
        CHECK_MSG(std::fabs(t.theta - c["theta"].get<double>()) < 1e-6,
                  "theta " + std::to_string(t.theta) + " vs " + std::to_string(c["theta"].get<double>()));
        CHECK(std::fabs(t.se - c["se"].get<double>()) < 1e-6);
        n++;
    }
    CHECK_EQ(n, 5);
}

// A synthetic model with many positions, for simulated engines.
static EloModel syntheticModel(int n, unsigned seed){
    EloModel m;
    m.version = 0;
    m.eloIntercept = 2500; m.eloSlope = 400;
    m.priorMean = 0; m.priorSd = 3;
    m.seTargetElo = 50; m.maxItems = 300; m.minItems = 20;
    m.groupShare = {{"tactics", 0.4}, {"positional", 0.35}, {"endgame", 0.25}};
    std::mt19937 rng(seed);
    std::lognormal_distribution<double> la(0.2, 0.35);
    std::uniform_real_distribution<double> ub(-3.5, 3.5);
    const char* g[3] = {"tactics", "positional", "endgame"};
    for (int i = 0; i < n; i++){
        EloItem it;
        it.id = "s" + std::to_string(i); it.group = it.category = g[i % 3];
        it.a = la(rng); it.b = ub(rng);
        m.items.push_back(it);
    }
    return m;
}

TEST_CASE("elo/adaptive-test-converges-on-simulated-answers"){
    EloModel m = syntheticModel(1000, 7);
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(0, 1);
    int runs = 0, inside = 0, maxUsed = 0;
    double sumAbs = 0;
    for (double trueTheta : {-2.5, -1.0, 0.0, 1.2, 2.5}){
        for (int rep = 0; rep < 10; rep++){
            EloSession s(m, true);
            int idx;
            while ((idx = s.next()) >= 0){
                const EloItem& it = m.items[idx];
                double p = 1.0 / (1.0 + std::exp(-it.a * (trueTheta - it.b)));
                s.record(idx, u(rng) < p);
            }
            int used = (int)s.responses().size();
            maxUsed = std::max(maxUsed, used);
            CHECK(used <= m.maxItems);
            CHECK(s.eloSe() <= m.seTargetElo || used == m.maxItems);
            double errElo = (s.estimate().theta - trueTheta) * m.eloSlope;
            sumAbs += std::fabs(errElo);
            if (std::fabs(errElo) <= 1.96 * s.eloSe()) inside++;
            runs++;
            // the groups follow their shares
            int tactics = 0;
            for (auto& r : s.responses()) tactics += m.items[r.first].group == "tactics";
            CHECK(std::fabs((double)tactics / used - 0.4) < 0.1);
        }
    }
    CHECK(sumAbs / runs < 60);                 // SE target 50 Elo
    CHECK(inside >= runs * 80 / 100);          // ~95% intervals: allow for 50 runs' noise
    CHECK(maxUsed > 20);
}

TEST_CASE("elo/full-mode-asks-every-position-in-order"){
    EloModel m = syntheticModel(30, 3);
    EloSession s(m, false);
    int expect = 0, idx;
    while ((idx = s.next()) >= 0){ CHECK_EQ(idx, expect); s.record(idx, idx % 2 == 0); expect++; }
    CHECK_EQ(expect, 30);
    EloResult r = eloResultFrom(m, s);
    CHECK(r.ok);
    CHECK_EQ(r.answered, 30);
    CHECK_EQ(r.solved, 15);
    CHECK_EQ((int)r.groups.size(), 3);
}

TEST_CASE("elo/run-against-the-mock-engine"){
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.minMovetimeMs = 10;                      // the fixture's base time is 100 ms
    EngineConfig cfg; cfg.path = kMock; cfg.name = "mock"; cfg.args = {"--burn-cpu"};
    EloOptions opt;
    opt.adaptive = true;
    opt.factor = 1;                            // skip the speed benchmark
    int calls = 0;
    opt.progress = [&](const EloProgress&){ calls++; };
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK(r.answered >= m.minItems && r.answered <= m.maxItems);
    CHECK_EQ(calls, r.answered);
    CHECK_EQ(r.movetimeMs, 100);
    CHECK_EQ((int)r.answers.size(), r.answered);
    for (auto& a : r.answers) CHECK(!a.move.empty());
    json j = json::parse(eloResultJson(r, m));
    CHECK(j.contains("elo") && j["positions"] == r.answered);
    CHECK(eloResultMarkdown(r, m).find("not a rating from games") != std::string::npos);
    // the engine's own speed is followed with a fixed search, checked at the end
    CHECK(!r.diag.ownProbe.empty());
    REQUIRE(!r.diag.speedChecks.empty());
    CHECK(r.diag.speedChecks.back().ownOk);
    CHECK(r.diag.cpuRatio > 0.6);
    CHECK(r.hasNumber());
}

TEST_CASE("elo/model-validation"){
    EloModel m;
    std::string err;
    CHECK(!m.loadJson("{\"format\": \"something else\"}", &err));
    CHECK(!err.empty());
    CHECK(!m.loadJson("not json", &err));
    json fx = fixture();
    fx["model"]["items"] = json::array();
    CHECK(!m.loadJson(fx["model"].dump(), &err));
    CHECK(err.find("no positions") != std::string::npos);
}

TEST_CASE("elo/shipped-model"){
    // Resources/elo/model.json: loads, and every position and acceptable move is legal
    std::ifstream f(std::string(CHESSGUI_SOURCE_DIR) + "/Resources/elo/model.json");
    REQUIRE(f.good());
    std::stringstream ss; ss << f.rdbuf();
    EloModel m;
    std::string err;
    bool loaded = m.loadJson(ss.str(), &err);
    CHECK_MSG(loaded, err);
    REQUIRE(loaded);
    CHECK(m.calibNps > 0);
    CHECK(m.errorRmse > 0);
    CHECK(!m.validationText.empty());
    CHECK(m.items.size() >= 300);
    for (auto& it : m.items){
        bool ok = false;
        Position p = Position::fromFEN(it.fen, &ok);
        CHECK_MSG(ok, it.id);
        CHECK_MSG(!it.ok.empty(), it.id);
        for (auto& u : it.ok){
            Move mv;
            CHECK_MSG(p.uciToMove(u, mv), it.id + " " + u);
        }
    }
}

TEST_CASE("elo/range-warning"){
    EloModel m;
    EloResult r; r.ok = true; r.elo = 1900;
    CHECK(eloRangeWarning(r, m).empty());          // the model sets no limit
    m.reliableFromElo = 2500;
    CHECK(eloRangeWarning(r, m).find("2500") != std::string::npos);
    CHECK(eloResultMarkdown(r, m).find("Warning") != std::string::npos);
    CHECK_EQ(json::parse(eloResultJson(r, m))["warning"].get<std::string>(), eloRangeWarning(r, m));
    r.elo = 2600;
    CHECK(eloRangeWarning(r, m).empty());
}

TEST_CASE("elo/error-by-band"){
    json fx = fixture();
    fx["model"]["errorRmse"] = 200.0;
    fx["model"]["errorBands"] = json::array({{{"from", 0}, {"to", 2500}, {"rmse", 400.0}},
                                             {{"from", 2500}, {"to", 99999}, {"rmse", 60.0}}});
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    CHECK_EQ(m.errorAt(1800), 400.0);
    CHECK_EQ(m.errorAt(2500), 60.0);
    CHECK_EQ(m.errorAt(-100), 200.0);              // outside every band
}

// ---------------------------------------------------------------- speed, range, checks
static void setEnv(const char* k, const std::string& v){
#ifdef _WIN32
    _putenv_s(k, v.c_str());                                  // "" removes it
#else
    if (v.empty()) unsetenv(k); else setenv(k, v.c_str(), 1);
#endif
}

// Two copies of the mock engine as Resources/reference/<file>, found through CHESSGUI_RESOURCES.
static std::string referenceDir(){
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "chessgui-elo-reference";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "reference");
#ifdef _WIN32
    const std::string exe = ".exe";
#else
    const std::string exe;
#endif
    for (const char* f : {"ref-a", "ref-b"})
        fs::copy_file(fs::u8path(kMock), dir / "reference" / (f + exe), fs::copy_options::overwrite_existing);
    return dir.string();
}

// every failure and warning of a run, for messages
static std::string failuresText(const EloResult& r){
    std::string s = "time use " + std::to_string(r.diag.timeUse) + ", cpu " + std::to_string(r.diag.cpuRatio)
                  + ", idle cpu " + std::to_string(r.diag.idleCpu) + ", drift " + std::to_string(r.diag.driftLog2) + ";";
    for (auto& f : r.diag.failures) s += " FAIL: " + f;
    for (auto& w : r.diag.warnings) s += " WARN: " + w;
    return s;
}

static std::vector<SpeedReference> mockReferences(){
    SpeedReference a{"ref-a", "ref-a", "Reference Mock", {}}, b{"ref-b", "ref-b", "Reference Mock", {}};
    for (int i = 0; i < 8; i++){
        a.items.push_back({"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 200000, 0, 400});   // 200 ms at 1x
        b.items.push_back({"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0, 5, 1000});       // 500 ms at 1x
    }
    return {a, b};
}

TEST_CASE("elo/speed-from-reference-engines"){
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-scale|2");
    SpeedOptions o;
    o.warmupSec = 0.2;
    o.positions = 6;
    SpeedMeasurement s = measureSpeed(mockReferences(), o);
    CHECK(s.ok);
    REQUIRE(s.engines.size() == 2);
    // the mock reports half the recorded times' base, at 2x speed: 400 / 100 and 1000 / 250
    CHECK(std::fabs(s.engines[0].speed - 4.0) < 1e-9);
    CHECK(std::fabs(s.engines[1].speed - 4.0) < 1e-9);
    CHECK_EQ(s.engines[0].positions, 6);
    CHECK(std::fabs(s.speed - 4.0) < 1e-9);
    CHECK(std::fabs(s.spreadLog2) < 1e-9);
    // a wrong engine is not used, a missing one is reported
    auto refs = mockReferences();
    refs[0].idName = "Stockfish";
    refs[1].file = "missing";
    SpeedMeasurement bad = measureSpeed(refs, o);
    CHECK(!bad.ok);
    CHECK(bad.engines[0].error.find("expected") != std::string::npos);
    CHECK(bad.engines[1].error.find("not installed") != std::string::npos);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/estimate-uses-the-measured-speed"){
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-scale|2|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    EloOptions opt;
    opt.adaptive = true;
    EngineConfig cfg; cfg.path = kMock;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK(r.diag.referenceSpeed);
    CHECK_EQ(r.movetimeMs, (int)std::lround(m.baseMovetimeMs / 4.0));   // 4x the calibration machine
    CHECK(r.diag.speedEnd.ok);
    CHECK(std::fabs(r.diag.driftLog2) < 1e-9);
    CHECK(std::fabs(r.diag.speedErrorElo - m.eloPerDoubling * m.speedSigmaLog2) < 1e-6);
    CHECK(r.diag.timeUse > 0.5);
    json j = json::parse(eloResultJson(r, m));
    CHECK_EQ(j["schema"].get<int>(), 2);
    CHECK(j["diagnostics"]["speedStart"]["ok"].get<bool>());
    CHECK(j["answers"][0].contains("wallMs"));
    // no reference engines: the perft benchmark, with a warning and a wider interval
    setEnv("CHESSGUI_RESOURCES", "");
    m.speedRefs[0].file = m.speedRefs[1].file = "missing";
    EloResult f = runEloEstimate(cfg, m, opt);
    REQUIRE(f.ok);
    CHECK(!f.diag.referenceSpeed);
    CHECK(f.diag.speedErrorElo >= m.eloPerDoubling * m.fallbackSigmaLog2 - 1e-6);
    bool warned = false;
    for (auto& w : f.diag.warnings) warned |= w.find("reference engines are not available") != std::string::npos;
    CHECK(warned);
    setEnv("MOCK_ENGINE_FLAGS", "");
}

TEST_CASE("elo/outside-the-calibrated-range"){
    EloModel m = syntheticModel(400, 5);
    m.rangeMin = 1500; m.rangeMax = 3500;
    for (bool solveAll : {true, false}){
        EloSession s(m, false);
        int idx;
        while ((idx = s.next()) >= 0) s.record(idx, solveAll);
        EloResult r = eloResultFrom(m, s);
        CHECK(r.ok);
        CHECK(!r.hasNumber());
        CHECK(r.status == (solveAll ? EloStatus::AboveRange : EloStatus::BelowRange));
        if (solveAll) CHECK(r.bound <= m.rangeMax); else CHECK(r.bound >= m.rangeMin);
        CHECK(eloHeadline(r).find(solveAll ? "above" : "below") != std::string::npos);
        json j = json::parse(eloResultJson(r, m));
        CHECK(j["elo"].is_null());
        CHECK_EQ(j["status"].get<std::string>(), std::string(solveAll ? "above_range" : "below_range"));
        CHECK(j["groups"].empty());                                // no per-category numbers either
        CHECK(eloResultMarkdown(r, m).find("calibrated range") != std::string::npos);
    }
    // the adaptive test stops once the engine is clearly outside the range
    EloSession s(m, true);
    int idx, n = 0;
    while ((idx = s.next()) >= 0){ s.record(idx, true); n++; }
    CHECK(n < 100);
    // inside the range: a number
    EloSession mid(m, false);
    while ((idx = mid.next()) >= 0) mid.record(idx, m.items[idx].b < 0);
    EloResult r = eloResultFrom(m, mid);
    CHECK(r.hasNumber());
    CHECK(eloHeadline(r).find("+/-") != std::string::npos);
}

TEST_CASE("elo/speed-sigma-by-architecture"){
    json fx = fixture();
    json mj = fx["model"];
    mj["speed"] = {{"sigmaLog2", 0.35}, {"sigmaLog2Arm64", 0.49}, {"singleReferenceSigmaLog2", 0.41},
                   {"singleReferenceSigmaLog2Arm64", 0.58}};
    EloModel m;
    REQUIRE(m.loadJson(mj.dump()));
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    CHECK_EQ(m.speedSigmaLog2, 0.49); CHECK_EQ(m.singleReferenceSigmaLog2, 0.58);
#else
    CHECK_EQ(m.speedSigmaLog2, 0.35); CHECK_EQ(m.singleReferenceSigmaLog2, 0.41);
#endif
}

TEST_CASE("elo/one-range-rule"){
    // past an end of the range: a bound (the interval's end, at most the range's end),
    // the same in the result, the headline and the running estimate
    EloModel m = syntheticModel(400, 5);
    m.rangeMin = 1500; m.rangeMax = 3500;
    EloRangeStatus a = eloRangeStatus(m, 3600, 50);
    CHECK(a.status == EloStatus::AboveRange); CHECK_EQ(a.bound, 3500.0);
    EloRangeStatus b = eloRangeStatus(m, 3600, 300);
    CHECK(b.status == EloStatus::AboveRange); CHECK_EQ(b.bound, 3300.0);
    CHECK(eloRangeStatus(m, 3400, 300).status == EloStatus::Ok);
    EloRangeStatus c = eloRangeStatus(m, 1400, 50);
    CHECK(c.status == EloStatus::BelowRange); CHECK_EQ(c.bound, 1500.0);
    EloRangeStatus d = eloRangeStatus(m, 1400, 300);
    CHECK(d.status == EloStatus::BelowRange); CHECK_EQ(d.bound, 1700.0);
    // a session that ends past the top: the result and the running estimate agree
    EloSession s(m, false);
    int idx;
    while ((idx = s.next()) >= 0) s.record(idx, true);
    s.setSpeed(200, 0);
    EloResult r = eloResultFrom(m, s);
    REQUIRE(r.status == EloStatus::AboveRange);
    EloProgress p;
    p.elo = s.elo(); p.se = s.eloSe(); p.speedErrorElo = s.speedErrorElo();
    CHECK_EQ(eloProgressText(p, m), eloHeadline(r));
    // a wider interval: the bound comes down, never a number past the end
    s.setSpeed((s.elo() - 3000) / 1.96, 0);                     // the interval reaches down to about 3000
    EloResult wide = eloResultFrom(m, s);
    CHECK(wide.status == EloStatus::AboveRange);
    CHECK(wide.bound < m.rangeMax);
    CHECK(eloHeadline(wide).find("past the top") != std::string::npos);
}

TEST_CASE("elo/early-stop-follows-the-speed-term"){
    // the adaptive test stopped "clearly above the range"; a larger speed term (a
    // later speed check) means it is not over yet
    EloModel m = syntheticModel(400, 5);
    m.rangeMin = 1500; m.rangeMax = 3500;
    EloSession s(m, true);
    int idx;
    while ((idx = s.next()) >= 0) s.record(idx, true);
    REQUIRE(s.done());
    REQUIRE(s.eloSe() > m.seTargetElo);                         // it stopped for the range, not for the SE
    s.setSpeed(5000, 0);
    CHECK(!s.done());
    CHECK(s.next() >= 0);
    // the correction of the estimate moves it
    s.setSpeed(0, -100);
    CHECK(std::fabs(s.elo() - (m.toElo(s.estimate().theta) - 100)) < 1e-9);
}

TEST_CASE("elo/choose-cpu"){
    // an i7-12700: 8 P-cores with two hyperthreads each (CPUs 0-15), 4 E-cores (16-19)
    auto i7 = []{
        std::vector<CpuCandidate> c;
        for (int cpu = 0; cpu < 20; cpu++){
            CpuCandidate x;
            x.cpu = cpu;
            x.core = cpu < 16 ? cpu / 2 * 2 : cpu;
            x.speedClass = cpu < 16 ? 2 : 1;
            x.onCpu0Core = cpu < 2;
            c.push_back(x);
        }
        return c;
    };
    std::set<int> held;
    CoreLock lock = [&](int, int core, bool& taken){
        taken = held.count(core) > 0;
        if (taken) return std::shared_ptr<void>();
        held.insert(core);
        return std::shared_ptr<void>(new int(core), [&held](void* x){ held.erase(*(int*)x); delete (int*)x; });
    };
    CpuPlacement a; chooseCpu(i7(), lock, a);
    CHECK_EQ(a.cpu, 15); CHECK(!a.shared);                      // a P-core, not the core of CPU 0
    CpuPlacement b; chooseCpu(i7(), lock, b);
    CHECK(b.cpu < 14); CHECK(b.cpu >= 2); CHECK(!b.shared);     // another physical core, not CPU 14 (15's sibling)
    // a busy core is avoided, an idle hyperthread of it too
    auto busy = i7();
    for (auto& x : busy) if (x.core == 12 || x.core == 10){ x.coreBusy = 1; x.busy = x.cpu % 2 ? 1 : 0; }
    held.clear(); a = CpuPlacement{}; b = CpuPlacement{};
    CpuPlacement c; chooseCpu(busy, lock, c);
    CHECK_EQ(c.cpu, 15);
    CpuPlacement d; chooseCpu(busy, lock, d);
    CHECK_EQ(d.cpu, 9);                                         // cores 12 and 10 are busy
    // every P-core but CPU 0's held: it shares one rather than taking an E-core or CPU 0's core
    held.clear(); c = CpuPlacement{}; d = CpuPlacement{};
    for (int core = 2; core < 16; core += 2) held.insert(core);
    CpuPlacement e; chooseCpu(i7(), lock, e);
    CHECK(e.shared); CHECK(e.cpu >= 2); CHECK(e.cpu < 16);
    // one core only (a small VM): it is used, CPU 0 or not
    held.clear();
    CpuCandidate one; one.cpu = 0; one.core = 0; one.speedClass = 1; one.onCpu0Core = true;
    CpuPlacement f; chooseCpu({one}, lock, f);
    CHECK_EQ(f.cpu, 0); CHECK(!f.shared);
    // no candidates (macOS, or nothing allowed): no pinning
    CpuPlacement g; chooseCpu({}, lock, g);
    CHECK_EQ(g.cpu, -1);
}

TEST_CASE("elo/engine-crash-keeps-the-cpu-check"){
    // the engine dies during a search now and then: those searches aren't counted, the
    // others still are (one crash used to turn the CPU check off for the whole run)
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.minMovetimeMs = 10;
    EngineConfig cfg; cfg.path = kMock; cfg.args = {"--burn-cpu", "--crash-on-go", "12"};
    EloOptions opt;
    opt.adaptive = false;
    opt.factor = 0.1;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    int crashes = 0;
    for (auto& a : r.answers) crashes += a.error == "crash";
    CHECK(crashes > 0);
    CHECK_MSG(r.diag.cpuRatio > 0.5, failuresText(r));
    CHECK(r.diag.placementApplied);
}

TEST_CASE("elo/misbehaving-engines-end-to-end"){
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.minMovetimeMs = 10;
    m.maxItems = 20;
    m.graceMs = 100;                           // an overrun ends 300 ms in: clearly past 125% of 200 ms
    auto run = [&](std::vector<std::string> args){
        EngineConfig cfg; cfg.path = kMock; cfg.args = args;
        EloOptions opt;
        opt.adaptive = true;
        opt.factor = 2;                        // 200 ms per position: well above timer granularity (Windows)
        return runEloEstimate(cfg, m, opt);
    };
    auto failedOn = [](const EloResult& r, const char* what){
        bool f = false;
        for (auto& x : r.diag.failures) f |= x.find(what) != std::string::npos;
        if (!f) std::fprintf(stderr, "expected a failure \"%s\"; got: %s\n", what, failuresText(r).c_str());
        return r.status == EloStatus::Unreliable && f;
    };
    CHECK(failedOn(run({"--burn-cpu", "--movetime-scale", "0.01"}), "of its move time"));   // ignores go movetime
    CHECK(failedOn(run({"--burn-cpu", "--movetime-scale", "3"}), "needed a \"stop\""));    // overruns it
    CHECK(failedOn(run({"--burn-cpu", "--illegal-bestmove"}), "illegal moves"));
}

TEST_CASE("elo/busy-waiting-engine"){
    // an engine that spins while it waits for input takes the core from the speed
    // checks: no number, and the reason says so (not "the speed changed")
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-scale|4|--real-time|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    m.checkEverySec = 0.3;
    EngineConfig cfg; cfg.path = kMock; cfg.args = {"--busy-idle"};
    EloOptions opt;
    opt.adaptive = true;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK_MSG(r.diag.idleCpu > m.idleCpuFail, failuresText(r));
    CHECK(r.status == EloStatus::Unreliable);
    REQUIRE(!r.diag.failures.empty());
    CHECK(r.diag.failures[0].find("busy-waiting") != std::string::npos);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/own-probe-sees-the-engine-slow-down"){
    // only the engine under test gets slower (it moved to a slower core): the reference
    // engines can't see that, its own fixed search does
    namespace fs = std::filesystem;
    fs::path speed = fs::temp_directory_path() / "chessgui-elo-own-speed.txt";
    { std::ofstream(speed) << 1; }
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-scale|1|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    m.checkEverySec = 0.3;
    EngineConfig cfg; cfg.path = kMock; cfg.args = {"--speed-file", speed.string(), "--real-time", "--burn-cpu"};
    EloOptions opt;
    opt.adaptive = false;
    opt.progress = [&](const EloProgress& p){ if (p.answered == 10) std::ofstream(speed) << 0.5; };
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK(!r.diag.ownProbe.empty());
    bool ownSaw = false, refSaw = false;
    for (auto& c : r.diag.speedChecks){
        ownSaw |= c.ownOk && std::fabs(c.ownLog2 + 1) < 0.15;
        refSaw |= c.referenceOk && std::fabs(c.referenceLog2) > 0.1;
    }
    CHECK_MSG(ownSaw, failuresText(r));
    CHECK(!refSaw);
    CHECK_MSG(r.status == EloStatus::Unreliable, failuresText(r));
    std::error_code ec;
    fs::remove(speed, ec);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/checks-on-the-run"){
    EloModel m = syntheticModel(40, 9);
    auto run = [&](long long wallMs, int errors, double cpu, double drift){
        EloSession s(m, false);
        int idx;
        while ((idx = s.next()) >= 0) s.record(idx, idx % 2 == 0);
        EloDiagnostics d;
        d.cpuRatio = cpu; d.driftLog2 = drift;
        EloResult r = eloResultFrom(m, s, d);
        r.movetimeMs = 100;
        for (int i = 0; i < 40; i++){
            EloAnswer a; a.id = "s" + std::to_string(i); a.wallMs = wallMs;
            if (i < errors) a.error = "illegal move";
            r.answers.push_back(a);
        }
        applyChecks(m, r);
        return r;
    };
    EloResult ok = run(98, 0, 1.0, 0.0);
    CHECK(ok.hasNumber());
    CHECK(ok.diag.failures.empty());
    CHECK(ok.diag.warnings.empty());
    EloResult early = run(30, 0, 1.0, 0.0);                   // ignores the move time
    CHECK(early.status == EloStatus::Unreliable);
    CHECK(early.diag.failures[0].find("move time") != std::string::npos);
    CHECK(run(70, 0, 1.0, 0.0).hasNumber());                  // a big margin: a warning only
    CHECK_EQ(run(70, 0, 1.0, 0.0).diag.warnings.size(), (size_t)1);
    CHECK(run(98, 6, 1.0, 0.0).status == EloStatus::Unreliable);   // 15% errors
    CHECK(run(98, 3, 1.0, 0.0).status == EloStatus::Unreliable);   // 7.5%: calibration engines had none
    CHECK(run(98, 2, 1.0, 0.0).hasNumber());
    CHECK(run(140, 0, 1.0, 0.0).status == EloStatus::Unreliable);  // needs "stop": more search than calibration
    CHECK(run(115, 0, 1.0, 0.0).hasNumber());                 // a little over: a warning
    CHECK(run(98, 0, 2.0, 0.0).status == EloStatus::Unreliable);   // two threads
    CHECK(run(98, 0, 0.4, 0.0).status == EloStatus::Unreliable);   // starved: it got less search than measured
    CHECK(run(98, 0, 0.7, 0.0).hasNumber());                  // somewhat starved: a warning
    CHECK(run(98, 0, 1.0, 0.5).status == EloStatus::Unreliable);   // the speed changed by 41%
    CHECK(run(98, 0, 1.0, 0.15).hasNumber());
    json j = json::parse(eloResultJson(early, m));
    CHECK(j["elo"].is_null());
    CHECK_EQ(j["status"].get<std::string>(), std::string("unreliable"));
    CHECK(!j["diagnostics"]["failures"].empty());
}

TEST_CASE("elo/cpu-placement"){
    CpuPlacement p = timedRunPlacement();
    CHECK(!p.description.empty());
    setEnv("MOCK_ENGINE_FLAGS", "--report-cpu");
    UciEngine e;
    e.setPlacement(p);
    REQUIRE(e.start(kMock));
    CHECK(e.placementApplied());
    e.newGame();
    e.setPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {});
    CHECK(!e.goMovetimeStrict(100, 100).empty());
    CHECK(e.lastSearchWallMs() >= 90);
    if (p.cpu >= 0) CHECK_EQ(e.lastInfo().lastString, "cpu " + std::to_string(p.cpu));
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    CHECK(e.cpuSeconds() >= 0);
#endif
    e.quit();
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_CPU", "none");
    CHECK(!timedRunPlacement().active());
    setEnv("CHESSGUI_CPU", "");
}

TEST_CASE("elo/speed-change-during-the-test"){
    // the reference engines' speed halves a third of the way through: the checks
    // during the test see it (the start and end alone would too, but later)
    namespace fs = std::filesystem;
    fs::path speed = fs::temp_directory_path() / "chessgui-elo-speed.txt";
    { std::ofstream(speed) << 2; }
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-file|" + speed.string() + "|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    m.checkEverySec = 0.3;
    EloOptions opt;
    opt.adaptive = false;
    opt.progress = [&](const EloProgress& p){ if (p.answered == 10) std::ofstream(speed) << 1; };
    EngineConfig cfg; cfg.path = kMock;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK(r.diag.speedChecks.size() >= 2);
    bool midRun = false;
    for (size_t i = 0; i + 1 < r.diag.speedChecks.size(); i++)
        midRun |= r.diag.speedChecks[i].referenceOk && std::fabs(r.diag.speedChecks[i].referenceLog2 + 1) < 0.05;
    CHECK(midRun);
    CHECK(std::fabs(r.diag.driftLog2 + 1) < 0.05);
    CHECK(r.status == EloStatus::Unreliable);
    bool said = false;
    for (auto& f : r.diag.failures) said |= f.find("speed changed") != std::string::npos;
    CHECK(said);
    std::error_code ec;
    fs::remove(speed, ec);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/passing-slowdown-measured-again"){
    // the reference engines are 4x slower for the searches of the first check only
    // (another program's burst of load): the check measures again, and the run passes
    namespace fs = std::filesystem;
    fs::path speed = fs::temp_directory_path() / "chessgui-elo-speed-burst.txt";
    { std::ofstream(speed) << 1; }
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-file|" + speed.string() + "|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    m.baseMovetimeMs = 300;                    // 150 ms per position: a 50 ms move time overran on Windows timers
    m.checkEverySec = 0.3;
    EloOptions opt;
    opt.adaptive = false;
    bool burst = false, retaken = false;
    opt.log = [&](const std::string& s){
        if (!burst && s.rfind("Checking the speed...", 0) == 0){
            burst = true;
            std::ofstream(speed) << "0.25 " << 2 * m.checkPositions << " 1";
        }
        retaken |= s.find("measuring again") != std::string::npos;
    };
    EngineConfig cfg; cfg.path = kMock;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK(burst);
    CHECK(retaken);
    REQUIRE(r.diag.speedChecks.size() >= 2);
    CHECK(r.diag.speedChecks[0].referenceOk);
    CHECK(std::fabs(r.diag.speedChecks[0].referenceLog2) < 0.05);
    CHECK(std::fabs(r.diag.driftLog2) < 0.05);
    CHECK_MSG(r.status != EloStatus::Unreliable, failuresText(r));
    std::error_code ec;
    fs::remove(speed, ec);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/move-time-clamped"){
    // a computer 100x faster than the calibration machine: the move time can't go that low
    setEnv("CHESSGUI_RESOURCES", referenceDir());
    setEnv("MOCK_ENGINE_FLAGS", "--name|Reference Mock 1|--speed-scale|50|--burn-cpu");
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.speedRefs = mockReferences();
    m.warmupSec = 0.2; m.speedPositions = 6;
    m.minMovetimeMs = 10;
    EloOptions opt;
    opt.adaptive = true;
    EngineConfig cfg; cfg.path = kMock;
    EloResult r = runEloEstimate(cfg, m, opt);
    REQUIRE(r.ok);
    CHECK_EQ(r.movetimeMs, (int)std::lround(m.baseMovetimeMs * m.factorMin));
    bool warned = false;
    for (auto& w : r.diag.warnings) warned |= w.find("had to be raised") != std::string::npos && w.find("corrected") != std::string::npos;
    CHECK(warned);
    // the bias of the clamp is corrected, and the uncertainty of that correction is in the speed term
    double clampLog2 = std::log2(r.movetimeMs / (m.baseMovetimeMs / 100.0));
    CHECK(std::fabs(r.diag.clampLog2 - clampLog2) < 0.05);
    CHECK(std::fabs(r.diag.clampCorrectionElo + m.eloPerDoubling * r.diag.clampLog2) < 1e-6);
    CHECK(r.diag.speedErrorElo >= m.eloPerDoublingErr * r.diag.clampLog2 - 1e-6);
    setEnv("MOCK_ENGINE_FLAGS", "");
    setEnv("CHESSGUI_RESOURCES", "");
}

TEST_CASE("elo/speed-source-text"){
    EloResult r;
    r.diag.referenceSpeed = true;
    CHECK(eloSpeedSource(r).find("reference engines") != std::string::npos);
    r.diag.referenceSpeed = false; r.userNps = 1e6;
    CHECK(eloSpeedSource(r).find("benchmark") != std::string::npos);
    r.userNps = 0;                                              // --factor: nothing was measured
    CHECK_EQ(eloSpeedSource(r), std::string("move time set by hand"));
}

TEST_CASE("elo/abort"){
    json fx = fixture();
    EloModel m;
    REQUIRE(m.loadJson(fx["model"].dump()));
    m.minMovetimeMs = 10;
    EngineConfig cfg; cfg.path = kMock; cfg.args = {"--burn-cpu"};
    std::atomic<bool> stop{false};
    EloOptions opt;
    opt.adaptive = false;
    opt.factor = 1;
    opt.abort = &stop;
    opt.progress = [&](const EloProgress& p){ if (p.answered == 5) stop = true; };
    EloResult r = runEloEstimate(cfg, m, opt);
    CHECK(!r.ok);                              // no number from part of a test
    CHECK_EQ(r.error, std::string("aborted"));
}

TEST_CASE("elo/concurrent-placements"){
    // two timed runs at once get different CPUs, on different physical cores (Linux,
    // Windows; macOS doesn't pin)
    CpuPlacement a = timedRunPlacement();
    CpuPlacement b = timedRunPlacement();
    if (a.cpu >= 0 && b.cpu >= 0) CHECK((a.cpu != b.cpu || a.group != b.group || b.shared));
    int first = a.cpu;
    a = CpuPlacement{};                        // the lock goes with the last copy
    CpuPlacement c = timedRunPlacement();
    if (first >= 0) CHECK(!c.shared);
}
