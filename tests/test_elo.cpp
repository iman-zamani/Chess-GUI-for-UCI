/* Chess-GUI-for-UCI — Elo estimator: parity with the Python fit, adaptive
 * testing on simulated answers, and a run against the mock engine. GPL-2.0 */
#include "testlib.hpp"
#include "elo_estimator.hpp"
#include "chess.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <random>
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
    EngineConfig cfg; cfg.path = kMock; cfg.name = "mock";
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
