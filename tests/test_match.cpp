/* Chess-GUI-for-UCI — match runner tests (concurrency, books, adjudication,
 * time losses, crashes, SPRT stop, resume) against the mock engine. GPL-2.0 */
#include "testlib.hpp"
#include "power.hpp"
#include "tournament.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <thread>

namespace fs = std::filesystem;
static const std::string kMock = MOCK_ENGINE_PATH;

static EngineConfig mock(const std::string& name, std::vector<std::string> extra = {}){
    EngineConfig c; c.path = kMock; c.name = name;
    c.args = {"--name", name, "--think-ms", "5"};
    c.args.insert(c.args.end(), extra.begin(), extra.end());
    return c;
}

static MatchSettings base(){
    MatchSettings s;
    s.engines = {mock("Alpha"), mock("Beta")};
    s.tc = {2000, 20};
    s.gamesPerPairing = 4;
    s.concurrency = 2;
    s.adj.maxMoves = 12;                       // keep the mock's games short
    return s;
}

static void waitDone(MatchRunner& r, int seconds = 60){
    for (int i = 0; i < seconds * 20 && r.running(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

static fs::path tmpFile(const std::string& name){
    fs::path p = fs::temp_directory_path() / ("chessgui-match-" + name);
    std::error_code ec; fs::remove(p, ec);
    return p;
}

TEST_CASE("match/pairings-per-mode"){
    CHECK_EQ(MatchRunner::pairingsFor(TournamentMode::Match, 4, 1).size(), (size_t)1);
    CHECK_EQ(MatchRunner::pairingsFor(TournamentMode::RoundRobin, 4, 1).size(), (size_t)6);
    auto g = MatchRunner::pairingsFor(TournamentMode::Gauntlet, 5, 1);
    CHECK_EQ(g.size(), (size_t)4);
    for (auto& p : g) CHECK_EQ(p.first, 0);
    CHECK_EQ(MatchRunner::pairingsFor(TournamentMode::Gauntlet, 5, 2).size(), (size_t)6);
}

TEST_CASE("match/adjudication-rules"){
    AdjudicationConfig a; a.resign = true; a.resignCp = 500; a.resignMoves = 2;
    a.draw = true; a.drawAfterMove = 30; a.drawCp = 10; a.drawMoves = 2;
    GameResult r;
    CHECK(!MatchRunner::adjudicate(a, {600, 600, 600}, 20, r));          // needs 2 moves each = 4 plies
    CHECK(MatchRunner::adjudicate(a, {0, 600, 520, 700, 800}, 20, r));
    CHECK(r == GameResult::WHITE_WINS);
    CHECK(MatchRunner::adjudicate(a, {-900, -600, -510, -32000}, 20, r));
    CHECK(r == GameResult::BLACK_WINS);
    CHECK(!MatchRunner::adjudicate(a, {600, 600, 400, 600}, 20, r));      // one engine disagrees
    CHECK(!MatchRunner::adjudicate(a, {0, 5, -3, 2}, 29, r));             // too early for a draw
    CHECK(MatchRunner::adjudicate(a, {0, 5, -3, 2}, 30, r));
    CHECK(r == GameResult::DRAW);
    AdjudicationConfig m; m.maxMoves = 40;
    CHECK(!MatchRunner::adjudicate(m, {}, 40, r));
    CHECK(MatchRunner::adjudicate(m, {}, 41, r));
}

TEST_CASE("match/concurrent-games-pgn-and-stats"){
    MatchSettings s = base();
    MatchRunner r;
    std::string err;
    REQUIRE(r.start(s, &err));
    waitDone(r);
    auto snap = r.snapshot();
    CHECK(snap.finished);
    CHECK_EQ(snap.gamesDone, 4);
    CHECK_EQ(snap.standings[0].games(), 4);
    CHECK_EQ(snap.standings[0].points() + snap.standings[1].points(), 4.0);
    CHECK_EQ(snap.pairings[0].penta.pairs(), 2);
    CHECK_EQ(snap.pairings[0].wdl.games(), 4);
    auto pgns = r.collectedPGNs();
    REQUIRE(pgns.size() == 4);
    std::string all;
    for (auto& p : pgns) all += p;
    for (const char* tag : {"[Round \"1.1\"]", "[Round \"2.2\"]", "[TimeControl \"2+0.02\"]",
                            "[Termination \"adjudication\"]", "[%eval ", "[%clk 0:00:0", "[PlyCount \""})
        CHECK_MSG(all.find(tag) != std::string::npos, "missing " << tag);
    auto j = nlohmann::json::parse(r.resultsJson());
    CHECK_EQ(j["gamesPlayed"].get<int>(), 4);
    CHECK_EQ(j["pairings"].size(), (size_t)1);
}

TEST_CASE("match/book-pairs-swap-colours"){
    fs::path book = tmpFile("book.epd");
    { std::ofstream f(book); f << "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - id \"a\";\n"
                               << "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - id \"b\";\n"; }
    MatchSettings s = base();
    s.bookPath = book.u8string();
    MatchRunner r;
    REQUIRE(r.start(s));
    waitDone(r);
    auto pgns = r.collectedPGNs();
    REQUIRE(pgns.size() == 4);
    auto tagOf = [](const std::string& pgn, const std::string& k){
        size_t p = pgn.find("[" + k + " \""); if (p == std::string::npos) return std::string();
        p += k.size() + 3; return pgn.substr(p, pgn.find('"', p) - p);
    };
    // games 1+2 and 3+4 share an opening, with the engines' colours swapped
    std::map<std::string, std::vector<std::string>> byRound;
    for (auto& p : pgns) byRound[tagOf(p, "Round").substr(0, tagOf(p, "Round").find('.'))].push_back(p);
    REQUIRE(byRound.size() == 2);
    std::set<std::string> fens;
    for (auto& kv : byRound){
        REQUIRE(kv.second.size() == 2);
        CHECK_EQ(tagOf(kv.second[0], "FEN"), tagOf(kv.second[1], "FEN"));
        CHECK_EQ(tagOf(kv.second[0], "White"), tagOf(kv.second[1], "Black"));
        fens.insert(tagOf(kv.second[0], "FEN"));
    }
    CHECK_EQ(fens.size(), (size_t)2);                                   // both openings used
}

TEST_CASE("match/time-loss-and-crash-recorded"){
    MatchSettings s = base();
    s.engines = {mock("Alpha"), mock("Hog", {"--time-hog"})};
    s.gamesPerPairing = 2;
    MatchRunner r;
    REQUIRE(r.start(s));
    waitDone(r);
    auto snap = r.snapshot();
    CHECK_EQ(snap.gamesDone, 2);
    CHECK_EQ(snap.pairings[0].timeLosses, 2);
    CHECK_EQ(snap.standings[0].wins, 2);
    std::string all; for (auto& p : r.collectedPGNs()) all += p;
    CHECK(all.find("[Termination \"time forfeit\"]") != std::string::npos);

    s.engines = {mock("Alpha"), mock("Crashy", {"--crash-on-go", "2"})};
    s.gamesPerPairing = 4;
    MatchRunner r2;
    REQUIRE(r2.start(s));
    waitDone(r2);
    snap = r2.snapshot();
    CHECK_EQ(snap.gamesDone, 4);                                        // restarted after every crash
    CHECK_EQ(snap.pairings[0].crashes, 4);
    CHECK_EQ(snap.standings[1].losses, 4);
}

TEST_CASE("match/sprt-stops-early"){
    MatchSettings s = base();
    s.engines = {mock("Good"), mock("Broken", {"--illegal-bestmove"})};
    s.gamesPerPairing = 400;
    s.concurrency = 4;
    s.sprtEnabled = true;
    s.sprt.elo0 = 0; s.sprt.elo1 = 10;
    MatchRunner r;
    REQUIRE(r.start(s));
    waitDone(r);
    auto snap = r.snapshot();
    CHECK(snap.sprtOutcome == SprtOutcome::AcceptH1);
    CHECK(snap.llr >= snap.llrUpper);
    CHECK(snap.gamesDone < 400);
    CHECK(snap.finished);
    MatchSettings bad = base(); bad.engines.push_back(mock("Third")); bad.mode = TournamentMode::RoundRobin;
    bad.sprtEnabled = true;
    MatchRunner r3; std::string err;
    CHECK(!r3.start(bad, &err));
    CHECK(err.find("SPRT") != std::string::npos);
}

TEST_CASE("match/resume-from-state-file"){
    fs::path state = tmpFile("state.json"), pgn = tmpFile("games.pgn");
    MatchSettings s = base();
    s.statePath = state.u8string(); s.pgnPath = pgn.u8string();
    {
        MatchRunner r;
        REQUIRE(r.start(s));
        waitDone(r);
        CHECK_EQ(r.snapshot().gamesDone, 4);
    }
    MatchRunner again;
    REQUIRE(again.start(s));
    waitDone(again);
    auto snap = again.snapshot();
    CHECK_EQ(snap.gamesResumed, 4);
    CHECK_EQ(snap.gamesDone, 4);
    CHECK(snap.finished);
    CHECK_EQ(snap.pairings[0].penta.pairs(), 2);
    std::ifstream f(pgn); std::string text((std::istreambuf_iterator<char>(f)), {});
    size_t games = 0; for (size_t p = 0; (p = text.find("[Event ", p)) != std::string::npos; p++) games++;
    CHECK_EQ(games, (size_t)4);                                         // nothing played twice
    // a different run doesn't reuse the state
    s.gamesPerPairing = 2;
    MatchRunner other;
    REQUIRE(other.start(s));
    waitDone(other);
    CHECK_EQ(other.snapshot().gamesResumed, 0);
}

TEST_CASE("match/gauntlet-and-round-robin"){
    MatchSettings s = base();
    s.engines = {mock("Seed"), mock("B"), mock("C")};
    s.mode = TournamentMode::Gauntlet;
    s.gamesPerPairing = 2;
    s.concurrency = 3;
    MatchRunner r;
    REQUIRE(r.start(s));
    waitDone(r);
    auto snap = r.snapshot();
    CHECK_EQ(snap.gamesDone, 4);
    CHECK_EQ(snap.standings[0].games(), 4);
    CHECK_EQ(snap.standings[1].games(), 2);
    s.mode = TournamentMode::RoundRobin;
    MatchRunner rr;
    REQUIRE(rr.start(s));
    waitDone(rr);
    CHECK_EQ(rr.snapshot().gamesDone, 6);
}

TEST_CASE("match/game-interrupted-by-sleep-is-replayed"){
    MatchSettings s = base();
    s.gamesPerPairing = 4;
    s.concurrency = 1;
    injectSuspendForTests(60000);                // the first move "spans" a 60 s sleep
    MatchRunner m;
    REQUIRE(m.start(s));
    for (int i = 0; i < 600 && m.running(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(!m.running());
    auto snap = m.snapshot();
    CHECK_EQ(snap.gamesDone, 4);                 // still four games, none lost on time
    CHECK_EQ(snap.pairings[0].timeLosses, 0);
    CHECK_EQ(snap.gamesReplayed, 1);
    bool noted = false;
    for (auto& l : snap.log) if (l.find("Game 1 was interrupted: the computer slept for") != std::string::npos) noted = true;
    CHECK(noted);
    if (!noted) for (auto& l : snap.log) std::fprintf(stderr, "    log: %s\n", l.c_str());
}
