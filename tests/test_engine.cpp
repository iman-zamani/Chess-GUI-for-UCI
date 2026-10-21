/* Chess-GUI-for-UCI — engine launch, configuration and info parsing tests. GPL-2.0 */
#include "testlib.hpp"
#include "chess.hpp"
#include "engine_config.hpp"
#include "uci_engine.hpp"
#include "settings.hpp"
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
static const std::string kMock = MOCK_ENGINE_PATH;

static fs::path scratchDir(const std::string& name){
    fs::path d = fs::temp_directory_path() / ("chessgui-test-" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

// "info string <key> <value>" printed by the mock with --report-env
static std::string reported(UciEngine& e, const std::string& key){
    uint64_t cur = 0;
    for (auto& l : e.logSince(cur)){
        std::string pre = "info string " + key + " ";
        if (l.dir == '<' && l.text.rfind(pre, 0) == 0) return l.text.substr(pre.size());
    }
    return "";
}

static bool sameDir(const std::string& a, const fs::path& b){
    std::error_code ec;
    return fs::equivalent(fs::u8path(a), b, ec);
}

TEST_CASE("engine/working-dir-default-is-engine-dir"){
    EngineConfig c; c.path = kMock; c.args = {"--report-env"};
    UciEngine e;
    REQUIRE(e.start(c));
    CHECK_MSG(sameDir(reported(e, "cwd"), fs::u8path(kMock).parent_path()), reported(e, "cwd"));
}

TEST_CASE("engine/working-dir-respected"){
    fs::path d = scratchDir("wd");
    EngineConfig c; c.path = kMock; c.args = {"--report-env"}; c.workingDir = d.u8string();
    UciEngine e;
    REQUIRE(e.start(c));
    CHECK_MSG(sameDir(reported(e, "cwd"), d), reported(e, "cwd") << " vs " << d.u8string());
}

TEST_CASE("engine/bad-working-dir-fails-fast"){
    EngineConfig c; c.path = kMock; c.workingDir = (scratchDir("gone") / "missing").u8string();
    UciEngine e;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!e.start(c));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    CHECK(!e.lastError().empty());
}

TEST_CASE("engine/args-passed-with-quoting"){
    EngineConfig c; c.path = kMock;
    c.args = {"--report-env", "--name", "Arg Test \"quoted\" engine", "--author", "back\\slash"};
    UciEngine e;
    REQUIRE(e.start(c));
    CHECK_EQ(e.name(), std::string("Arg Test \"quoted\" engine"));
    CHECK_EQ(e.author(), std::string("back\\slash"));
    CHECK_EQ(reported(e, "args"), std::string("--report-env|--name|Arg Test \"quoted\" engine|--author|back\\slash"));
}

TEST_CASE("engine/stderr-kept-separate"){
    EngineConfig c; c.path = kMock; c.args = {"--stderr", "warming-up-the-net"};
    UciEngine e;
    REQUIRE(e.start(c));
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK(!e.goDepth(1).empty());
    uint64_t cur = 0; bool onStderr = false, onStdout = false;
    for (auto& l : e.logSince(cur)){
        if (l.text.find("warming-up-the-net") == std::string::npos) continue;
        if (l.dir == '!') onStderr = true;
        if (l.dir == '<') onStdout = true;
    }
    CHECK(onStderr);
    CHECK(!onStdout);
    CHECK(e.stderrTail().find("warming-up-the-net") != std::string::npos);
}

TEST_CASE("engine/options-applied-at-start"){
    EngineConfig c; c.path = kMock; c.options = {{"Hash", "64"}, {"Clear Hash", ""}};
    UciEngine e;
    REQUIRE(e.start(c));
    uint64_t cur = 0; bool hash = false, button = false;
    for (auto& l : e.logSince(cur)){
        if (l.dir == '>' && l.text == "setoption name Hash value 64") hash = true;
        if (l.dir == '>' && l.text == "setoption name Clear Hash") button = true;
    }
    CHECK(hash);
    CHECK(button);
    CHECK(e.hasOption("hash"));
    CHECK(!e.hasOption("NoSuchOption"));
}

TEST_CASE("engine/missing-binary-fails-fast"){
    UciEngine e;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!e.start(kMock + "-does-not-exist"));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    CHECK(e.lastError().find("cannot launch") != std::string::npos);
}

TEST_CASE("engine/exit-at-start-reported"){
    EngineConfig c; c.path = kMock; c.args = {"--exit-at-start", "3"};
    UciEngine e;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!e.start(c));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    CHECK_MSG(e.lastError().find("exited") != std::string::npos, e.lastError());
    CHECK_MSG(e.lastError().find("exiting at start") != std::string::npos, e.lastError());
}

TEST_CASE("engine/crash-is-detected-quickly"){
    EngineConfig c; c.path = kMock; c.args = {"--crash-on-go", "2"};
    UciEngine e;
    REQUIRE(e.start(c));
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK(!e.goMovetime(20).empty());
    auto t0 = std::chrono::steady_clock::now();
    CHECK(e.goMovetime(5000).empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    CHECK(!e.alive());
}

TEST_CASE("engine/quit-kills-unresponsive-engine"){
    EngineConfig c; c.path = kMock; c.args = {"--no-quit"};
    UciEngine e;
    REQUIRE(e.start(c));
    auto t0 = std::chrono::steady_clock::now();
    e.quit();
    CHECK(!e.alive());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
}

TEST_CASE("engine/info-line-parsing"){
    EngineInfo inf; std::vector<PvLine> pvs;
    parseInfoLine("info depth 24 seldepth 31 multipv 1 score cp 37 lowerbound nodes 1234567 nps 987654 "
                  "hashfull 412 tbhits 5 time 1250 pv e2e4 e7e5 g1f3", inf, pvs);
    CHECK(inf.valid);
    CHECK_EQ(inf.depth, 24); CHECK_EQ(inf.seldepth, 31);
    CHECK_EQ(inf.scoreCp, 37); CHECK(inf.lowerbound); CHECK(!inf.upperbound);
    CHECK_EQ(inf.nodes, 1234567LL); CHECK_EQ(inf.nps, 987654LL);
    CHECK_EQ(inf.hashfull, 412); CHECK_EQ(inf.tbhits, 5LL); CHECK_EQ(inf.timeMs, 1250LL);
    CHECK_EQ(inf.pv, std::string("e2e4 e7e5 g1f3"));
    REQUIRE(pvs.size() == 1);
    CHECK_EQ(pvs[0].firstMove(), std::string("e2e4"));
    CHECK_EQ(pvs[0].nodes, 1234567LL);
    // second MultiPV line, mate score; must not overwrite the summary
    parseInfoLine("info depth 24 seldepth 28 multipv 2 score mate -3 upperbound nodes 1300000 pv d2d4", inf, pvs);
    REQUIRE(pvs.size() == 2);
    CHECK(pvs[1].isMate); CHECK_EQ(pvs[1].mateIn, -3); CHECK(pvs[1].upperbound);
    CHECK_EQ(inf.scoreCp, 37);
    CHECK_EQ(inf.pv, std::string("e2e4 e7e5 g1f3"));
    CHECK_EQ(inf.nodes, 1300000LL);            // node count is global, not per line
    // currmove lines and strings update without touching the score
    parseInfoLine("info depth 25 currmove g1f3 currmovenumber 3", inf, pvs);
    CHECK_EQ(inf.currmove, std::string("g1f3")); CHECK_EQ(inf.currmoveNumber, 3);
    CHECK_EQ(inf.depth, 25); CHECK_EQ(inf.scoreCp, 37);
    parseInfoLine("info string NNUE evaluation using nn-abc.nnue enabled", inf, pvs);
    CHECK_EQ(inf.lastString, std::string("NNUE evaluation using nn-abc.nnue enabled"));
    // a bound-only update for an existing line keeps its PV
    parseInfoLine("info depth 26 multipv 1 score cp 45 upperbound nodes 2000000", inf, pvs);
    CHECK_EQ(pvs[0].scoreCp, 45); CHECK(pvs[0].upperbound);
    CHECK_EQ(pvs[0].pv, std::string("e2e4 e7e5 g1f3"));
}

TEST_CASE("engine/live-info-from-engine"){
    EngineConfig c; c.path = kMock;
    UciEngine e;
    REQUIRE(e.start(c));
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK(!e.goMovetime(150).empty());
    EngineInfo inf = e.lastInfo();
    CHECK(inf.depth >= 2);
    CHECK(inf.seldepth > inf.depth);
    CHECK(inf.nodes > 0);
    CHECK(inf.hashfull >= 0);
}

TEST_CASE("engine/registry-roundtrip"){
    fs::path d = scratchDir("registry");
    std::string file = (d / "sub" / "engines.json").u8string();
    EngineRegistry r;
    CHECK(r.load(file));                       // missing file: fine, empty
    CHECK(r.engines().empty());
    EngineConfig a; a.name = "Dev build"; a.path = "/opt/eng/dev"; a.args = {"--uci", "x y"};
    a.workingDir = "/opt/eng"; a.options = {{"Hash", "128"}, {"EvalFile", "nets/big net.nnue"}};
    r.add(a);
    EngineConfig b; b.name = "Auto"; b.path = "/opt/eng/auto"; b.discovered = true;
    r.add(b);                                  // discovered, unconfigured: not saved
    std::string err;
    REQUIRE(r.save(file, &err));
    EngineRegistry r2;
    REQUIRE(r2.load(file, &err));
    REQUIRE(r2.engines().size() == 1);
    const EngineConfig& x = r2.engines()[0];
    CHECK_EQ(x.name, a.name); CHECK_EQ(x.path, a.path);
    CHECK(x.args == a.args);
    CHECK_EQ(x.workingDir, a.workingDir);
    CHECK(x.options == a.options);
    CHECK_EQ(x.effectiveWorkingDir(), std::string("/opt/eng"));
    // corrupt file is reported, not crashed on
    { std::ofstream f(fs::u8path(file)); f << "{ not json"; }
    CHECK(!r2.load(file, &err));
    CHECK(!err.empty());
}

TEST_CASE("engine/registry-discover"){
    fs::path d = scratchDir("discover");
    fs::create_directories(d / "sub");
    auto touch = [](const fs::path& p, bool exec){
        std::ofstream(p) << "x";
#ifndef _WIN32
        if (exec) fs::permissions(p, fs::perms::owner_all);
#else
        (void)exec;
#endif
    };
#ifdef _WIN32
    touch(d / "alpha.exe", true); touch(d / "sub" / "beta.exe", true); touch(d / "notes.txt", false);
#else
    touch(d / "alpha", true); touch(d / "sub" / "beta", true); touch(d / "notes.txt", false);
    touch(d / "net.nnue", false);
#endif
    EngineRegistry r;
    CHECK_EQ(r.discover(d.u8string()), 2);
    CHECK_EQ(r.discover(d.u8string()), 0);       // no duplicates
    for (auto& c : r.engines()) CHECK(c.discovered);
}

TEST_CASE("engine/args-split-join"){
    std::string err;
    auto v = splitArgs("--threads 2   'a b' \"c \\\"d\\\"\" e\\ f C:\\nets\\x.nnue \"\"", &err);
    REQUIRE(v.size() == 7);
    CHECK_EQ(v[0], std::string("--threads")); CHECK_EQ(v[1], std::string("2"));
    CHECK_EQ(v[2], std::string("a b"));
    CHECK_EQ(v[3], std::string("c \"d\""));
    CHECK_EQ(v[4], std::string("e f"));
    CHECK_EQ(v[5], std::string("C:\\nets\\x.nnue"));
    CHECK_EQ(v[6], std::string(""));
    CHECK(err.empty());
    std::vector<std::string> odd = {"plain", "with space", "it's", "", "back\\slash", "q\"uote"};
    CHECK(splitArgs(joinArgs(odd)) == odd);
    splitArgs("'open", &err);
    CHECK(!err.empty());
}

TEST_CASE("engine/settings-roundtrip-keeps-unknown-keys"){
    fs::path d = scratchDir("settings");
    std::string file = (d / "settings.json").u8string();
    { std::ofstream f(fs::u8path(file)); f << R"({"tcBaseMs": 180000, "futureKey": {"a": 1}, "accCount": "oops"})"; }
    Settings s;
    std::string err;
    REQUIRE(s.load(file, &err));
    CHECK_EQ(s.tcBaseMs, 180000);
    CHECK_EQ(s.accCount, 100);                 // wrong type: default kept
    s.theme = "light"; s.perftEngine = "Dev build";
    REQUIRE(s.save(file, &err));
    Settings t;
    REQUIRE(t.load(file, &err));
    CHECK_EQ(t.theme, std::string("light"));
    CHECK_EQ(t.perftEngine, std::string("Dev build"));
    std::ifstream f(fs::u8path(file)); std::stringstream ss; ss << f.rdbuf();
    CHECK(ss.str().find("futureKey") != std::string::npos);
    { std::ofstream g(fs::u8path(file)); g << "[1,2"; }
    CHECK(!t.load(file, &err));
}

TEST_CASE("engine/idle-output-is-logged-and-monitored"){
    uint64_t id = 0;
    std::shared_ptr<UciLogBuffer> log;
    {
        EngineConfig c; c.path = kMock;
        UciEngine e;
        REQUIRE(e.start(c));
        e.setRole("Test");
        for (auto& m : EngineMonitor::instance().list())
            if (m.label == "Test: MockEngine 1.0" && m.alive){ id = m.id; log = m.log; }
        REQUIRE(id != 0);
        // a raw command from the console while nobody is waiting for output
        CHECK(EngineMonitor::instance().send(id, "frobnicate"));
        bool seen = false;
        for (int i = 0; i < 50 && !seen; i++){
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            uint64_t cur = 0;
            for (auto& l : log->since(cur)) if (l.dir == '<' && l.text.find("frobnicate") != std::string::npos) seen = true;
        }
        CHECK(seen);
    }
    // the engine object is gone: the entry is kept as ended, with its log
    bool ended = false;
    for (auto& m : EngineMonitor::instance().list()) if (m.id == id){ ended = !m.alive; }
    CHECK(ended);
    CHECK(!EngineMonitor::instance().send(id, "isready"));
    uint64_t cur = 0;
    CHECK(!log->since(cur).empty());
}

TEST_CASE("engine/chess960-option-follows-the-position"){
    UciEngine e;
    REQUIRE(e.start(kMock));
    auto sent = [&](const std::string& text){
        uint64_t cur = 0;
        int n = 0;
        for (auto& l : e.logSince(cur)) if (l.dir == '>' && l.text == text) n++;
        return n;
    };
    // a standard game: the option is never touched
    e.setPosition(Position::startpos().toFEN(), {"e2e4"});
    CHECK(!e.goDepth(1).empty());
    CHECK_EQ(sent("setoption name UCI_Chess960 value true") + sent("setoption name UCI_Chess960 value false"), 0);
    // Chess960: switched on, and castling comes back king-takes-rook
    Position p = Position::fromFEN("6k1/8/8/8/8/8/8/RK5R w KQ - 0 1");
    e.setPosition(p.toFEN(), {});
    std::string best = e.goDepth(1);
    Move m;
    CHECK(p.uciToMove(best, m));
    CHECK_EQ(sent("setoption name UCI_Chess960 value true"), 1);
    // back to standard chess: switched off again
    e.setPosition(Position::startpos().toFEN(), {});
    CHECK(!e.goDepth(1).empty());
    CHECK_EQ(sent("setoption name UCI_Chess960 value false"), 1);
    e.quit();
}
