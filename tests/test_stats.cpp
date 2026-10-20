/* Chess-GUI-for-UCI — Elo / LOS / SPRT tests. GPL-2.0
 *
 * Reference values were computed with fastchess v1.8.2-alpha's own code
 * (https://github.com/Disservin/fastchess, MIT; app/src/matchmaking/sprt/sprt.cpp
 * and app/src/matchmaking/elo/elo_{pentanomial,wdl}.cpp) for the inputs below:
 * SPRT(alpha 0.05, beta 0.05, elo0, elo1, model).getLLR(...), EloPentanomial and
 * EloWDL diff()/error()/nEloDiff()/nEloError()/los() (los printed rounded to 0.01%).
 */
#include "testlib.hpp"
#include "stats.hpp"
#include <cmath>

struct LlrCase { bool penta, logistic; double elo0, elo1; int c[6]; double llr; };
static const LlrCase kLlr[] = {
    {true, false, 0, 5, {10,40,30,60,45,15}, 0.2765061099},
    {true, false, 0, 5, {5,20,10,40,30,12}, 0.4628355040},
    {true, false, 0, 5, {30,80,60,120,70,20}, -0.7152985448},
    {true, false, 0, 5, {1,3,2,8,4,2}, 0.0586393504},
    {true, false, 0, 5, {0,10,5,30,20,5}, 0.5066445718},
    {false, false, 0, 5, {120,100,280}, 0.3821898151},
    {false, false, 0, 5, {50,60,140}, -0.2428093922},
    {false, false, 0, 5, {300,250,450}, 0.8667817929},
    {false, false, 0, 5, {10,2,5}, 0.1353249941},
    {false, false, 0, 5, {0,0,10}, -0.0012803433},
    {true, false, 0, 2, {10,40,30,60,45,15}, 0.1205188621},
    {true, false, 0, 2, {5,20,10,40,30,12}, 0.1908412947},
    {true, false, 0, 2, {30,80,60,120,70,20}, -0.2672994675},
    {true, false, 0, 2, {1,3,2,8,4,2}, 0.0244374908},
    {true, false, 0, 2, {0,10,5,30,20,5}, 0.2059892759},
    {false, false, 0, 2, {120,100,280}, 0.1652889567},
    {false, false, 0, 2, {50,60,140}, -0.0909190592},
    {false, false, 0, 2, {300,250,450}, 0.3715465112},
    {false, false, 0, 2, {10,2,5}, 0.0545398093},
    {false, false, 0, 2, {0,0,10}, -0.0001755044},
    {true, true, -1.75, 0.25, {10,40,30,60,45,15}, 0.1978347688},
    {true, true, -1.75, 0.25, {5,20,10,40,30,12}, 0.2795560350},
    {true, true, -1.75, 0.25, {30,80,60,120,70,20}, -0.3554427301},
    {true, true, -1.75, 0.25, {1,3,2,8,4,2}, 0.0374538347},
    {true, true, -1.75, 0.25, {0,10,5,30,20,5}, 0.3820670450},
    {false, true, -1.75, 0.25, {120,100,280}, 0.2757040443},
    {false, true, -1.75, 0.25, {50,60,140}, -0.1237969053},
    {false, true, -1.75, 0.25, {300,250,450}, 0.5458158350},
    {false, true, -1.75, 0.25, {10,2,5}, 0.0654999777},
    {false, true, -1.75, 0.25, {0,0,10}, 0.0414156046},
    {true, true, 0, 5, {10,40,30,60,45,15}, 0.3786191100},
    {true, true, 0, 5, {5,20,10,40,30,12}, 0.6405769890},
    {true, true, 0, 5, {30,80,60,120,70,20}, -1.1079412124},
    {true, true, 0, 5, {1,3,2,8,4,2}, 0.0827331667},
    {true, true, 0, 5, {0,10,5,30,20,5}, 0.8946806824},
    {false, true, 0, 5, {120,100,280}, 0.5368361060},
    {false, true, 0, 5, {50,60,140}, -0.3855241335},
    {false, true, 0, 5, {300,250,450}, 1.1205362674},
    {false, true, 0, 5, {10,2,5}, 0.1609275709},
    {false, true, 0, 5, {0,0,10}, -0.1396819972},
};

// pentanomial counts in fastchess order: LL, LD, WL, DD, WD, WW
struct EloPCase { int c[6]; double elo, err, nelo, nerr, los; };
static const EloPCase kEloP[] = {
    {{10,40,30,60,45,15}, 13.0349468814, 23.1515325509, 19.2164968394, 34.0480617286, 0.8656999999999999},
    {{5,20,10,40,30,12}, 35.7601644917, 31.3617920050, 51.2614185375, 44.5158285011, 0.988},
    {{30,80,60,120,70,20}, -13.7216924680, 16.7449284495, -20.2781880594, 24.7010601464, 0.0538},
    {{1,3,2,8,4,2}, 26.1066926050, 74.8952764799, 38.2642848048, 107.6694249764, 0.757},
    {{0,10,5,30,20,5}, 49.9754946433, 33.1495010552, 88.2487269715, 57.5517284704, 0.9987},
};
// WDL counts in fastchess order: wins, losses, draws
struct EloWCase { int c[3]; double elo, err, nelo, nerr, los; };
static const EloWCase kEloW[] = {
    {{120,100,280}, 13.9048425037, 20.2190304443, 20.9893513223, 30.4535122109, 0.9116},
    {{50,60,140}, -13.9048425037, 28.6265513075, -20.9893513223, 43.0677699905, 0.1697},
    {{300,250,450}, 17.3862775124, 15.9849075088, 23.4775211724, 21.5338849953, 0.9837},
    {{10,2,5}, 177.4789996931, 167.1136825746, 234.9093306788, 165.1573586524, 0.9973000000000001},
};

static PentaCounts penta(const int* c){ PentaCounts p; p.ll = c[0]; p.ld = c[1]; p.wl = c[2]; p.dd = c[3]; p.wd = c[4]; p.ww = c[5]; return p; }
static WdlCounts wdl(const int* c){ WdlCounts w; w.wins = c[0]; w.losses = c[1]; w.draws = c[2]; return w; }

TEST_CASE("stats/sprt-llr-matches-fastchess"){
    for (auto& k : kLlr){
        SprtConfig cfg; cfg.elo0 = k.elo0; cfg.elo1 = k.elo1;
        cfg.model = k.logistic ? SprtModel::Logistic : SprtModel::Normalized;
        double v = k.penta ? sprtLlr(cfg, penta(k.c)) : sprtLlr(cfg, wdl(k.c));
        CHECK_MSG(std::abs(v - k.llr) < 1e-6, (k.penta ? "penta " : "wdl ") << sprtModelName(cfg.model)
                  << " [" << k.elo0 << ", " << k.elo1 << "]: got " << v << ", fastchess " << k.llr);
    }
}

TEST_CASE("stats/sprt-bounds-and-outcome"){
    SprtConfig cfg;
    CHECK(std::abs(cfg.lowerBound() - (-2.944438979166)) < 1e-9);
    CHECK(std::abs(cfg.upperBound() - 2.944438979166) < 1e-9);
    CHECK(sprtOutcome(cfg, 3.0) == SprtOutcome::AcceptH1);
    CHECK(sprtOutcome(cfg, -3.0) == SprtOutcome::AcceptH0);
    CHECK(sprtOutcome(cfg, 0.1) == SprtOutcome::Continue);
    CHECK(cfg.validate().empty());
    SprtConfig bad; bad.elo0 = 5; bad.elo1 = 0;
    CHECK(!bad.validate().empty());
    CHECK_EQ(sprtLlr(cfg, PentaCounts{}), 0.0);
}

TEST_CASE("stats/elo-matches-fastchess"){
    for (auto& k : kEloP){
        EloEstimate e = eloFromPenta(penta(k.c));
        REQUIRE(e.valid);
        CHECK_MSG(std::abs(e.elo - k.elo) < 1e-6, "penta elo " << e.elo << " vs " << k.elo);
        CHECK_MSG(std::abs(e.eloError - k.err) < 1e-6, "penta err " << e.eloError << " vs " << k.err);
        CHECK_MSG(std::abs(e.nelo - k.nelo) < 1e-6, "penta nelo " << e.nelo << " vs " << k.nelo);
        CHECK_MSG(std::abs(e.neloError - k.nerr) < 1e-6, "penta nerr " << e.neloError << " vs " << k.nerr);
        CHECK_MSG(std::abs(e.los - k.los) < 0.00006, "penta los " << e.los << " vs " << k.los);
    }
    for (auto& k : kEloW){
        EloEstimate e = eloFromWdl(wdl(k.c));
        REQUIRE(e.valid);
        CHECK_MSG(std::abs(e.elo - k.elo) < 1e-6, "wdl elo " << e.elo << " vs " << k.elo);
        CHECK_MSG(std::abs(e.eloError - k.err) < 1e-6, "wdl err " << e.eloError << " vs " << k.err);
        CHECK_MSG(std::abs(e.nelo - k.nelo) < 1e-6, "wdl nelo " << e.nelo << " vs " << k.nelo);
        CHECK_MSG(std::abs(e.los - k.los) < 0.00006, "wdl los " << e.los << " vs " << k.los);
    }
}

TEST_CASE("stats/pairs-and-degenerate-scores"){
    PentaCounts p;
    p.addPair(1, 1); p.addPair(1, 0.5); p.addPair(1, 0); p.addPair(0, 1); p.addPair(0.5, 0.5); p.addPair(0.5, 0); p.addPair(0, 0);
    CHECK_EQ(p.ww, 1); CHECK_EQ(p.wd, 1); CHECK_EQ(p.wl, 2); CHECK_EQ(p.dd, 1); CHECK_EQ(p.ld, 1); CHECK_EQ(p.ll, 1);
    WdlCounts all; all.wins = 10;
    CHECK(!eloFromWdl(all).valid);                 // 100% score: no finite Elo
    CHECK(!eloFromWdl(WdlCounts{}).valid);
}
