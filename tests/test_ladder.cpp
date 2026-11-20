/* Chess-GUI-for-UCI — Elo from games against a strength-limited reference:
 * the fit on simulated games (bias and interval coverage), bounds, the level
 * plan, the bundled book, and runs against the mock engine. GPL-2.0 */
#include "testlib.hpp"
#include "elo_ladder.hpp"
#include "openings.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <filesystem>
#include <cmath>
#include <random>
#include <string>

using json = nlohmann::json;
static const std::string kMock = MOCK_ENGINE_PATH;

// Simulated colour-swapped pairs at one level for an engine rated `elo`
// (independent games; a draw rate that falls as the score moves away from 50%).
static LadderLevel simulate(std::mt19937_64& rng, double elo, int level, int pairs){
    LadderLevel l;
    l.elo = level;
    double s = ladderExpectedScore(elo, level);
    double d = 0.6 * std::min(s, 1 - s), w = s - d / 2;
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < pairs; i++){
        double g[2];
        for (double& x : g){
            double r = u(rng);
            x = r < w ? 1 : r < w + d ? 0.5 : 0;
            if (x == 1) l.wdl.wins++; else if (x == 0.5) l.wdl.draws++; else l.wdl.losses++;
        }
        l.penta.addPair(g[0], g[1]);
    }
    return l;
}

static LadderLevel counts(int level, int w, int d, int l){
    LadderLevel x;
    x.elo = level; x.wdl.wins = w; x.wdl.draws = d; x.wdl.losses = l;
    return x;
}

TEST_CASE("ladder/expected-score"){
    CHECK(std::fabs(ladderExpectedScore(1500, 1500) - 0.5) < 1e-12);
    CHECK(std::fabs(ladderExpectedScore(1900, 1500) - 10.0 / 11.0) < 1e-12);
    CHECK(std::fabs(ladderExpectedScore(1500, 1900) - 1.0 / 11.0) < 1e-12);
}

TEST_CASE("ladder/one-level-matches-match-elo"){
    // one level: the fit is the level plus the match Elo of the score
    LadderLevel l = counts(1800, 50, 20, 30);
    LadderFit f = fitLadder({l});
    REQUIRE(f.status == LadderStatus::Ok);
    EloEstimate e = eloFromWdl(l.wdl);
    CHECK_MSG(std::fabs(f.elo - (1800 + e.elo)) < 1e-4, std::to_string(f.elo));
    CHECK(f.games == 100);
    CHECK(f.fitP == 1);
    // the performance column is that same fit, also near a perfect score
    CHECK(ladderPerformance(l) == ladderHeadline(f));
    LadderLevel near = counts(2190, 9, 0, 1);
    LadderFit nf = fitLadder({near});
    CHECK(nf.status == LadderStatus::Above || nf.ci95 < 400);
    CHECK(ladderPerformance(counts(2000, 4, 0, 4)).empty());     // fewer than 10 games
}

TEST_CASE("ladder/fit-recovers-the-rating"){
    std::mt19937_64 rng(11);
    std::vector<LadderLevel> lv;
    for (int L : {1500, 1700, 1900, 2100, 2300}) lv.push_back(simulate(rng, 1960, L, 400));
    LadderFit f = fitLadder(lv);
    REQUIRE(f.status == LadderStatus::Ok);
    CHECK_MSG(std::fabs(f.elo - 1960) < f.ci95, std::to_string(f.elo) + " +/- " + std::to_string(f.ci95));
    CHECK_MSG(f.ci95 > 5 && f.ci95 < 25, std::to_string(f.ci95));
    CHECK(!f.extrapolated);
    CHECK(f.dispersion < 1);                      // draws make games less variable than the binomial
}

TEST_CASE("ladder/interval-coverage"){
    // The 95% interval should hold the true rating about 95% of the time, for
    // small runs as well as large ones.
    std::mt19937_64 rng(5);
    for (int pairs : {6, 30}){
        int hit = 0, n = 0;
        for (int rep = 0; rep < 400; rep++){
            std::uniform_real_distribution<double> u(1600, 2600);
            double truth = u(rng);
            std::vector<LadderLevel> lv;
            for (int L : {1700, 2000, 2300, 2600}) lv.push_back(simulate(rng, truth, L, pairs));
            LadderFit f = fitLadder(lv);
            if (f.status != LadderStatus::Ok) continue;
            n++;
            if (std::fabs(f.elo - truth) <= f.ci95) hit++;
        }
        double cover = (double)hit / n;
        CHECK_MSG(n > 380, std::to_string(n));
        CHECK_MSG(cover > 0.91 && cover < 0.99, "pairs " + std::to_string(pairs) + ": coverage " + std::to_string(cover));
    }
}

TEST_CASE("ladder/perfect-and-zero-scores-give-bounds"){
    std::vector<LadderLevel> win = {counts(2400, 10, 0, 0), counts(2850, 10, 0, 0)};
    LadderFit f = fitLadder(win);
    CHECK(f.status == LadderStatus::Above);
    // P(20 straight wins | bound) = 5%
    double logp = 10 * std::log(ladderExpectedScore(f.bound, 2400)) + 10 * std::log(ladderExpectedScore(f.bound, 2850));
    CHECK_MSG(std::fabs(logp - std::log(0.05)) < 1e-6, std::to_string(logp));
    CHECK(f.bound > 2850);
    CHECK(ladderHeadline(f).rfind("above ", 0) == 0);

    LadderFit g = fitLadder({counts(1350, 0, 0, 12)});
    CHECK(g.status == LadderStatus::Below);
    CHECK(g.bound < 1350);
    CHECK(ladderHeadline(g).rfind("below ", 0) == 0);

    CHECK(fitLadder({}).status == LadderStatus::NoGames);
    CHECK(fitLadder({counts(1500, 0, 0, 0)}).status == LadderStatus::NoGames);
    CHECK(ladderHeadline(LadderFit{}) == "no games");
}

TEST_CASE("ladder/far-extrapolation-is-a-bound"){
    // 95% against the only level: +512, too far beyond it for a number; the bound is the
    // lower end of the 95% likelihood-ratio interval
    LadderFit f = fitLadder({counts(2000, 95, 0, 5)});
    CHECK(f.status == LadderStatus::Above);
    CHECK(f.bound > 2000 && f.bound < f.elo);
    CHECK(f.bound == f.low95);
    CHECK(f.high95 > f.elo);
    LadderFit b = fitLadder({counts(2000, 5, 0, 95)});
    CHECK(b.status == LadderStatus::Below);
    CHECK(b.bound == b.high95 && b.bound < 2000);
    // 70%: +147, a number, flagged as extrapolated
    LadderFit g = fitLadder({counts(2000, 70, 0, 30)});
    CHECK(g.status == LadderStatus::Ok);
    CHECK(g.extrapolated);
    CHECK(g.low95 < g.elo && g.elo < g.high95);
    CHECK(ladderHeadline(g).find(" to ") != std::string::npos);   // the interval, not +/-
    // 55%: +35, close enough to the level not to be
    LadderFit h = fitLadder({counts(2000, 55, 0, 45)});
    CHECK(h.status == LadderStatus::Ok);
    CHECK(!h.extrapolated);
}

TEST_CASE("ladder/levels-that-disagree-fail-the-fit-check"){
    // nearly all lost against the weak level, nearly all won against the strong one
    LadderFit f = fitLadder({counts(1500, 3, 4, 53), counts(2000, 30, 0, 30), counts(2500, 53, 4, 3)});
    CHECK_MSG(f.fitP < 0.001, std::to_string(f.fitP));
    std::mt19937_64 rng(3);
    std::vector<LadderLevel> ok;
    for (int L : {1500, 2000, 2500}) ok.push_back(simulate(rng, 2000, L, 30));
    CHECK_MSG(fitLadder(ok).fitP > 0.01, std::to_string(fitLadder(ok).fitP));
}

TEST_CASE("ladder/level-plan"){
    using V = std::vector<int>;
    LadderStage a = ladderFirstStage(200, 1350, 2850);
    CHECK((a.levels == V{1350, 1730, 2100, 2480, 2850}));
    CHECK((a.games == V{12, 12, 12, 12, 12}));
    CHECK(a.total() == 60);
    CHECK((ladderFirstStage(20, 1350, 2850).games == V{2, 2, 2, 2, 2}));    // at least a pair each
    CHECK(ladderFirstStage(1000, 1350, 2850).total() == 300);

    LadderFit f; f.status = LadderStatus::Ok; f.elo = 1834;
    LadderStage b = ladderSecondStage(f, 140, 1350, 2850);
    CHECK((b.levels == V{1630, 1830, 2030}));
    CHECK((b.games == V{46, 48, 46}));             // every game used: the odd pair to the middle level
    CHECK(ladderSecondStage(f, 10, 1350, 2850).total() == 10);
    CHECK((ladderSecondStage(f, 10, 1350, 2850).games == V{2, 6, 2}));
    f.status = LadderStatus::Above;                 // past the range: all at the end level
    LadderStage up = ladderSecondStage(f, 140, 1350, 2850);
    CHECK((up.levels == V{2850}) && (up.games == V{140}));
    f.status = LadderStatus::Below;
    CHECK((ladderSecondStage(f, 141, 1350, 2850).levels == V{1350}));
    CHECK((ladderSecondStage(f, 141, 1350, 2850).games == V{140}));
    f.status = LadderStatus::Ok; f.elo = 1400;      // near the end of the range: clamped, no repeats
    CHECK((ladderSecondStage(f, 120, 1350, 2850).levels == V{1350, 1400, 1600}));
    LadderStage few = ladderSecondStage(f, 5, 1350, 2850);
    CHECK((few.levels == V{1400}) && (few.games == V{4}));
    CHECK(ladderSecondStage(f, 1, 1350, 2850).levels.empty());

    LadderStage g = ladderGivenStage({2100, 1500, 1500}, 9);
    CHECK((g.levels == V{1500, 2100}));
    CHECK((g.games == V{4, 4}));
    CHECK((ladderGivenStage({1500, 1600, 1700}, 2).games == V{2, 2, 2}));    // at least one pair each
    CHECK((ladderGivenStage({1500, 1600, 1700, 1800}, 22).games == V{6, 6, 6, 4}));   // odd pairs to the middle levels first
}

TEST_CASE("ladder/stockfish-11-pick-depth"){
    // ((UCI_Elo - 1346.6) / 143.4) ^ (1 / 0.806), rounded down, plus one (search.cpp at sf_11)
    CHECK(stockfish11PickDepth(1350) == 1);
    CHECK(stockfish11PickDepth(2100) == 8);
    CHECK(stockfish11PickDepth(2500) == 14);
    CHECK(stockfish11PickDepth(2850) == 19);
    CHECK(ladderIsStockfish11("Stockfish 11 64 POPCNT"));
    CHECK(!ladderIsStockfish11("Stockfish 16.1"));
}

TEST_CASE("ladder/parse-levels"){
    std::vector<int> v;
    CHECK(parseLadderLevels("1500, 1800 2100", v));
    CHECK((v == std::vector<int>{1500, 1800, 2100}));
    CHECK(parseLadderLevels("2100;1500,,1500", v));
    CHECK((v == std::vector<int>{1500, 2100}));
    CHECK(!parseLadderLevels("", v));
    CHECK(!parseLadderLevels("1500 abc", v));
    CHECK(!parseLadderLevels("-5", v));
    CHECK(!parseLadderLevels("1500.5", v));
}

TEST_CASE("ladder/settings-validation"){
    LadderSettings s;
    CHECK(!s.validate().empty());                 // no engine
    s.engine.path = kMock; s.reference.path = kMock;
    CHECK(s.validate().empty());
    s.games = 10;
    CHECK(s.validate().find("automatic levels") != std::string::npos);
    s.levels = {1500};
    CHECK(s.validate().empty());
    s.tc.baseMs = 0;
    CHECK(!s.validate().empty());
}

TEST_CASE("ladder/bundled-book"){
    BookLoadResult b = loadOpeningBook(std::string(CHESSGUI_SOURCE_DIR) + "/Resources/books/openings.pgn", BookFormat::Auto, 8);
    CHECK_MSG(b.error.empty(), b.error);
    CHECK_EQ((int)b.openings.size(), 50);
    CHECK_MSG(b.truncated == 0 && b.skipped == 0, b.firstProblem);
    for (auto& o : b.openings) CHECK_MSG(o.moves.size() == 8, o.label);
}

static EngineConfig mockConfig(const std::string& name, std::vector<std::string> args){
    EngineConfig c;
    c.name = name; c.path = kMock; c.args = std::move(args);
    return c;
}

static LadderSettings quickSettings(){
    LadderSettings s;
    s.engine = mockConfig("Tested", {"--think-ms", "2"});
    s.reference = mockConfig("Ref", {"--think-ms", "2", "--uci-elo", "1000:2000", "--weak-below", "1500"});
    s.tc = TimeControl{1000, 10};
    s.concurrency = 4;
    s.bookPath = std::string(CHESSGUI_SOURCE_DIR) + "/Resources/books/openings.pgn";
    return s;
}

TEST_CASE("ladder/run-given-levels"){
    LadderSettings s = quickSettings();
    s.levels = {1000, 2000};
    s.games = 8;
    int progressCalls = 0;
    LadderOptions o;
    o.progress = [&](const LadderProgress& p){ progressCalls++; CHECK(p.stages == 1); };
    LadderResult r = runEloLadder(s, o);
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.refMinElo == 1000 && r.refMaxElo == 2000);
    REQUIRE(r.levels.size() == 2);
    // below --weak-below the reference plays illegal moves: the options reached the right games
    CHECK(r.levels[0].elo == 1000);
    CHECK_EQ(r.levels[0].wdl.wins, 4);
    CHECK(r.levels[1].elo == 2000);
    CHECK_EQ(r.levels[1].wdl.games(), 4);
    CHECK(r.levels[1].wdl.wins < 4);
    CHECK(r.levels[0].refMoves == 0);               // the depth check is for Stockfish 11 only
    CHECK_EQ(r.gamesPlayed, 8);
    CHECK(r.stages.size() == 1);
    CHECK(progressCalls > 0);
    json j = json::parse(ladderResultJson(r));
    CHECK(j["gamesPlayed"] == 8);
    CHECK(j["levels"].size() == 2);
    CHECK(j["fit"].contains("status"));
    CHECK(j["settings"]["seed"] == s.seed);
    CHECK(j["settings"]["levels"].size() == 2);
}

TEST_CASE("ladder/run-automatic-levels"){
    LadderSettings s = quickSettings();
    s.games = 20;
    std::vector<std::string> lines;
    LadderOptions o;
    o.log = [&](const std::string& l){ lines.push_back(l); };
    LadderResult r = runEloLadder(s, o);
    REQUIRE(r.ok);
    REQUIRE(r.stages.size() == 2);
    CHECK((r.stages[0].levels == std::vector<int>{1000, 1250, 1500, 1750, 2000}));
    CHECK((r.stages[0].games == std::vector<int>{2, 2, 2, 2, 2}));
    for (int l : r.stages[1].levels) CHECK(l >= 1000 && l <= 2000);
    CHECK_EQ(r.gamesPlayed, 20);
    bool sawStage2 = false;
    for (auto& l : lines) if (l.rfind("Stage 2 of 2", 0) == 0) sawStage2 = true;
    CHECK(sawStage2);
}

TEST_CASE("ladder/reference-needs-uci-elo"){
    LadderSettings s = quickSettings();
    s.reference = mockConfig("Plain", {});
    s.levels = {1500};
    LadderResult r = runEloLadder(s);
    CHECK(!r.ok);
    CHECK_MSG(r.error.find("UCI_Elo") != std::string::npos, r.error);
}

TEST_CASE("ladder/level-outside-the-range"){
    LadderSettings s = quickSettings();
    s.levels = {900, 1500};
    LadderResult r = runEloLadder(s);
    CHECK(!r.ok);
    CHECK_MSG(r.error.find("outside") != std::string::npos, r.error);
}

TEST_CASE("ladder/engine-that-does-not-start"){
    LadderSettings s = quickSettings();
    s.engine = mockConfig("Broken", {"--exit-at-start", "3"});
    s.levels = {1500};
    LadderResult r = runEloLadder(s);
    CHECK(!r.ok);
    CHECK_MSG(r.error.find("could not start Broken") != std::string::npos, r.error);
}

TEST_CASE("ladder/stop"){
    LadderSettings s = quickSettings();
    s.levels = {1500, 2000};
    s.games = 400;
    std::atomic<bool> stop{false};
    LadderOptions o;
    o.abort = &stop;
    o.progress = [&](const LadderProgress& p){ if (p.gamesDone >= 2) stop = true; };
    LadderResult r = runEloLadder(s, o);
    CHECK(r.aborted);
    CHECK(!r.ok);
    CHECK_MSG(r.error.empty(), r.error);
    CHECK(r.gamesPlayed >= 2 && r.gamesPlayed < 400);
}

TEST_CASE("ladder/resume"){
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("ladder_resume_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    // the tested engine is a copy of the mock, so the test can "rebuild" it
    fs::path engine = dir / ("engine" + fs::path(kMock).extension().string());   // .exe on Windows
    fs::copy_file(kMock, engine);
    LadderSettings s = quickSettings();
    s.engine.path = engine.u8string();
    s.games = 20;                                   // automatic: two stages
    s.seed = 99;
    s.statePath = (dir / "state.json").u8string();

    // stopped part-way through stage 1
    std::atomic<bool> stop{false};
    LadderOptions o;
    o.abort = &stop;
    o.progress = [&](const LadderProgress& p){ if (p.gamesDone >= 4) stop = true; };
    LadderResult a = runEloLadder(s, o);
    REQUIRE(a.aborted);
    int first = a.gamesPlayed;
    CHECK(first >= 4 && first < 20);
    CHECK(fs::exists(s.statePath + ".stage1"));

    // the same settings: the stopped games are kept and the run finishes both stages
    std::vector<std::string> lines;
    LadderOptions o2;
    o2.log = [&](const std::string& l){ lines.push_back(l); };
    LadderResult b = runEloLadder(s, o2);
    REQUIRE(b.ok);
    CHECK_EQ(b.gamesPlayed, 20);
    CHECK(b.stages.size() == 2);
    bool resumed = false;
    for (auto& l : lines) if (l.find("Resumed " + std::to_string(first) + " games of stage 1") != std::string::npos) resumed = true;
    CHECK(resumed);
    // finished: the state is gone, so the same command plays a new run
    CHECK(!fs::exists(s.statePath) && !fs::exists(s.statePath + ".stage1") && !fs::exists(s.statePath + ".stage2"));

    // stopped again, then the engine is rebuilt: the old games are not mixed with the new build's
    stop = false;
    LadderResult c = runEloLadder(s, o);
    REQUIRE(c.aborted);
    std::error_code ec;
    fs::last_write_time(engine, fs::last_write_time(engine) + std::chrono::hours(1), ec);
    REQUIRE(!ec);
    LadderResult d = runEloLadder(s);
    REQUIRE(d.ok);
    bool rebuilt = false;
    for (auto& w : d.warnings) if (w.find("rebuilt") != std::string::npos) rebuilt = true;
    CHECK(rebuilt);
    CHECK_EQ(d.gamesPlayed, 20);

    // stopped, then other settings: said, not silent
    stop = false;
    LadderResult e = runEloLadder(s, o);
    REQUIRE(e.aborted);
    s.tc.incMs = 20;
    LadderResult f = runEloLadder(s);
    REQUIRE(f.ok);
    bool other = false;
    for (auto& w : f.warnings) if (w.find("other settings") != std::string::npos) other = true;
    CHECK(other);
    fs::remove_all(dir, ec);
}
