/* Chess-GUI-for-UCI — headless command-line tool (chess-cli). GPL-2.0 */
#include "engine_config.hpp"
#include "perft_bisect.hpp"
#include "uci_check.hpp"
#include "stats.hpp"
#include "testing.hpp"
#include "tournament.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Exit codes (documented in --help).
enum Exit { EXIT_OK = 0, EXIT_H0 = 1, EXIT_FAILED = 1, EXIT_USAGE = 2, EXIT_INCONCLUSIVE = 3, EXIT_INTERRUPTED = 130 };

static std::atomic<bool> g_interrupted{false};
static void onSignal(int){ g_interrupted = true; }

static const char* kUsage =
R"(chess-cli - headless tools for UCI engine developers

Usage:
  chess-cli match [options]     play a match, round-robin or gauntlet
  chess-cli sprt  [options]     a match that stops when the SPRT accepts H0 or H1
  chess-cli perft [--engine E] [--fen FEN --depth N | --suite FILE] [--divide]
                                count moves (no engine), or check an engine's "go perft" against them
  chess-cli perft-bisect --engine E [--fen FEN] --depth N
                                find the position where an engine's move generator goes wrong
  chess-cli uci-check --engine E [--quick] [--only ID,ID...]
                                check an engine against the UCI protocol
  chess-cli analyse --engine E [--fen FEN] [--moves M...] [--depth N | --movetime MS | --nodes N]
                    [--multipv K]
  chess-cli bench [--runs N]    single-threaded speed (internal perft, best of N runs)
  chess-cli help

Every command takes --json for machine-readable output.

Engines (give two or more):
  --engine NAME                 an engine from the GUI's engine list
  --engine cmd=PATH [name=N] [args="A B"] [dir=D] [option.K=V]...
                                an engine given on the command line

Games:
  --mode match|roundrobin|gauntlet   (default match: the first two engines)
  --seeds N                     gauntlet: the first N engines play all others (1)
  --tc BASE[+INC]               seconds, e.g. 10+0.1 or 60 (default 10+0.1)
  --margin MS                   allowed clock overrun before a time loss (100)
  --games N                     games per pairing, rounded up to even (100; sprt: 20000)
  --concurrency N               games in parallel (default: half the CPU cores)
  --book FILE                   .epd / .pgn / FEN list; each opening is a colour-swapped pair
  --book-plies N                moves used from each PGN game (8)
  --random [--seed S]           shuffle the book reproducibly
  --fen FEN                     a start position (repeatable; used without --book)
  --resign CP:MOVES             resign adjudication, e.g. 600:3
  --draw MOVE:CP:MOVES          draw adjudication, e.g. 40:10:8
  --max-moves N                 draw after N full moves
  --syzygy DIRS                 adjudicate with Syzygy tables (directories separated by ':', ';' on Windows)
  --syzygy-pieces N             only positions with at most N pieces (default: what the tables cover)
  --pgn FILE                    append finished games to FILE
  --state FILE                  resumable state; an existing matching file is resumed

SPRT (sprt only):
  --elo0 E --elo1 E             hypotheses (0 and 5)
  --alpha A --beta B            error rates (0.05 and 0.05)
  --model normalized|logistic   Elo model (normalized)

Output:
  --report N                    a progress line every N games (10)
  --verbose                     print every game result and runner message
  --json                        print the results as JSON on stdout (progress goes to stderr)

Exit codes: 0 success (sprt: H1 accepted), 1 a check failed (perft mismatch, movegen bug found,
UCI check FAIL; sprt: H0 accepted), 2 usage or start error, 3 sprt: games ran out before a
decision, 130 interrupted (the match state file can resume it).
)";

static bool parseDouble(const std::string& s, double& v){
    char* end = nullptr; v = std::strtod(s.c_str(), &end);
    return !s.empty() && end && *end == 0;
}
static bool parseInt(const std::string& s, int& v){
    char* end = nullptr; long x = std::strtol(s.c_str(), &end, 10); v = (int)x;
    return !s.empty() && end && *end == 0;
}
static std::vector<std::string> splitOn(const std::string& s, char sep){
    std::vector<std::string> out; std::string cur; std::istringstream in(s);
    while (std::getline(in, cur, sep)) out.push_back(cur);
    return out;
}

static bool parseTc(const std::string& s, TimeControl& tc){
    auto parts = splitOn(s, '+');
    double base = 0, inc = 0;
    if (parts.empty() || parts.size() > 2 || !parseDouble(parts[0], base) || base <= 0) return false;
    if (parts.size() == 2 && (!parseDouble(parts[1], inc) || inc < 0)) return false;
    tc.baseMs = (int)std::lround(base * 1000); tc.incMs = (int)std::lround(inc * 1000);
    return true;
}

struct CliOptions {
    MatchSettings s;
    bool sprt = false, json = false, verbose = false;
    int report = 10;
    bool gamesSet = false;
};

static int fail(const std::string& msg){
    std::fprintf(stderr, "chess-cli: %s\n(see chess-cli help)\n", msg.c_str());
    return EXIT_USAGE;
}

// Parses "--engine NAME" or "--engine cmd=PATH [key=value]..." starting at a[i]
// ("--engine"); leaves i on the last token used. Returns -1 on success.
static int parseEngine(const std::vector<std::string>& a, size_t& i, EngineConfig& c,
                       EngineRegistry& reg, bool& regLoaded){
    if (i + 1 >= a.size()) return fail("--engine needs a name or cmd=PATH");
    if (a[i + 1].rfind("cmd=", 0) != 0){
        if (!regLoaded){ reg.load((fs::u8path(userConfigDir()) / "engines.json").u8string()); regLoaded = true; }
        const EngineConfig* found = reg.findByName(a[++i]);
        if (!found) return fail("no engine named '" + a[i] + "' in " + userConfigDir() + "/engines.json");
        c = *found;
        return -1;
    }
    c = EngineConfig{};
    while (i + 1 < a.size() && a[i + 1].rfind("--", 0) != 0){
        std::string t = a[++i];
        size_t eq = t.find('=');
        if (eq == std::string::npos) return fail("engine setting '" + t + "' is not key=value");
        std::string key = t.substr(0, eq), val = t.substr(eq + 1);
        if (key == "cmd") c.path = val;
        else if (key == "name") c.name = val;
        else if (key == "dir") c.workingDir = val;
        else if (key == "args"){
            std::string err; c.args = splitArgs(val, &err);
            if (!err.empty()) return fail("args: " + err);
        }
        else if (key.rfind("option.", 0) == 0) c.setOption(key.substr(7), val);
        else return fail("unknown engine setting '" + key + "'");
    }
    if (c.path.empty()) return fail("--engine needs cmd=PATH");
    if (c.name.empty()) c.name = fs::u8path(c.path).filename().u8string();
    return -1;
}

// Parses everything after the subcommand. Returns -1 on success, else an exit code.
static int parseMatchArgs(const std::vector<std::string>& a, CliOptions& o){
    EngineRegistry reg;
    bool regLoaded = false;
    auto need = [&](size_t& i, std::string& v) -> bool {
        if (i + 1 >= a.size()) return false;
        v = a[++i]; return true;
    };
    for (size_t i = 0; i < a.size(); i++){
        const std::string& k = a[i];
        std::string v;
        if (k == "--engine"){
            EngineConfig c;
            int r = parseEngine(a, i, c, reg, regLoaded);
            if (r >= 0) return r;
            o.s.engines.push_back(c);
        }
        else if (k == "--mode"){
            if (!need(i, v)) return fail("--mode needs a value");
            if (v == "match") o.s.mode = TournamentMode::Match;
            else if (v == "roundrobin" || v == "rr") o.s.mode = TournamentMode::RoundRobin;
            else if (v == "gauntlet") o.s.mode = TournamentMode::Gauntlet;
            else return fail("unknown mode '" + v + "'");
        }
        else if (k == "--seeds"){ if (!need(i, v) || !parseInt(v, o.s.gauntletSeeds) || o.s.gauntletSeeds < 1) return fail("bad --seeds"); }
        else if (k == "--tc"){ if (!need(i, v) || !parseTc(v, o.s.tc)) return fail("bad --tc (use seconds, e.g. 10+0.1)"); }
        else if (k == "--margin"){ if (!need(i, v) || !parseInt(v, o.s.timeMarginMs) || o.s.timeMarginMs < 0) return fail("bad --margin"); }
        else if (k == "--games"){ if (!need(i, v) || !parseInt(v, o.s.gamesPerPairing) || o.s.gamesPerPairing < 1) return fail("bad --games"); o.gamesSet = true; }
        else if (k == "--concurrency"){ if (!need(i, v) || !parseInt(v, o.s.concurrency) || o.s.concurrency < 1) return fail("bad --concurrency"); }
        else if (k == "--book"){ if (!need(i, v)) return fail("--book needs a file"); o.s.bookPath = v; }
        else if (k == "--book-plies"){ if (!need(i, v) || !parseInt(v, o.s.bookPlies) || o.s.bookPlies < 0) return fail("bad --book-plies"); }
        else if (k == "--random") o.s.bookRandom = true;
        else if (k == "--seed"){
            if (!need(i, v)) return fail("--seed needs a value");
            char* end = nullptr; o.s.seed = std::strtoull(v.c_str(), &end, 10);
            if (!end || *end) return fail("bad --seed");
        }
        else if (k == "--fen"){ if (!need(i, v)) return fail("--fen needs a FEN"); o.s.startFens.push_back(v); }
        else if (k == "--resign"){
            auto p = splitOn(need(i, v) ? v : "", ':');
            if (p.size() != 2 || !parseInt(p[0], o.s.adj.resignCp) || !parseInt(p[1], o.s.adj.resignMoves)) return fail("bad --resign (CP:MOVES)");
            o.s.adj.resign = true;
        }
        else if (k == "--draw"){
            auto p = splitOn(need(i, v) ? v : "", ':');
            if (p.size() != 3 || !parseInt(p[0], o.s.adj.drawAfterMove) || !parseInt(p[1], o.s.adj.drawCp)
                || !parseInt(p[2], o.s.adj.drawMoves)) return fail("bad --draw (MOVE:CP:MOVES)");
            o.s.adj.draw = true;
        }
        else if (k == "--max-moves"){ if (!need(i, v) || !parseInt(v, o.s.adj.maxMoves) || o.s.adj.maxMoves < 0) return fail("bad --max-moves"); }
        else if (k == "--syzygy"){ if (!need(i, v)) return fail("--syzygy needs a directory"); o.s.adj.syzygyPath = v; }
        else if (k == "--syzygy-pieces"){ if (!need(i, v) || !parseInt(v, o.s.adj.syzygyPieces) || o.s.adj.syzygyPieces < 0) return fail("bad --syzygy-pieces"); }
        else if (k == "--pgn"){ if (!need(i, v)) return fail("--pgn needs a file"); o.s.pgnPath = v; }
        else if (k == "--state"){ if (!need(i, v)) return fail("--state needs a file"); o.s.statePath = v; }
        else if (k == "--elo0"){ if (!need(i, v) || !parseDouble(v, o.s.sprt.elo0)) return fail("bad --elo0"); }
        else if (k == "--elo1"){ if (!need(i, v) || !parseDouble(v, o.s.sprt.elo1)) return fail("bad --elo1"); }
        else if (k == "--alpha"){ if (!need(i, v) || !parseDouble(v, o.s.sprt.alpha)) return fail("bad --alpha"); }
        else if (k == "--beta"){ if (!need(i, v) || !parseDouble(v, o.s.sprt.beta)) return fail("bad --beta"); }
        else if (k == "--model"){
            if (!need(i, v)) return fail("--model needs a value");
            if (v == "normalized") o.s.sprt.model = SprtModel::Normalized;
            else if (v == "logistic") o.s.sprt.model = SprtModel::Logistic;
            else return fail("unknown --model '" + v + "'");
        }
        else if (k == "--report"){ if (!need(i, v) || !parseInt(v, o.report) || o.report < 1) return fail("bad --report"); }
        else if (k == "--verbose") o.verbose = true;
        else if (k == "--json") o.json = true;
        else return fail("unknown option '" + k + "'");
    }
    if (o.s.engines.size() < 2) return fail("give at least two --engine");
    if (o.sprt){
        if (o.s.mode != TournamentMode::Match) return fail("sprt needs --mode match");
        std::string e = o.s.sprt.validate();
        if (!e.empty()) return fail("SPRT: " + e);
        o.s.sprtEnabled = true;
        if (!o.gamesSet) o.s.gamesPerPairing = 20000;
    }
    if (o.s.concurrency < 1) o.s.concurrency = std::max(1, (int)std::thread::hardware_concurrency() / 2);
    return -1;
}

static std::string eloText(const PairingStats& p){
    EloEstimate e = eloFromPenta(p.penta);
    if (!e.valid) e = eloFromWdl(p.wdl);
    char b[160];
    if (!e.valid) std::snprintf(b, sizeof b, "Elo n/a");
    else std::snprintf(b, sizeof b, "Elo %+.2f +/- %.2f  nElo %+.2f +/- %.2f  LOS %.2f%%",
                       e.elo, e.eloError, e.nelo, e.neloError, e.los * 100);
    return b;
}

static void printProgress(FILE* out, const MatchRunner::Snapshot& s){
    std::fprintf(out, "Games %d/%d", s.gamesDone, s.gamesTotal);
    if (s.pairings.size() == 1){
        const PairingStats& p = s.pairings[0];
        std::fprintf(out, "  W-D-L %d-%d-%d  %s  Pairs [%d, %d, %d, %d, %d]", p.wdl.wins, p.wdl.draws, p.wdl.losses,
                     eloText(p).c_str(), p.penta.ll, p.penta.ld, p.penta.wl + p.penta.dd, p.penta.wd, p.penta.ww);
    }
    if (s.sprt) std::fprintf(out, "  LLR %.2f (%.2f, %.2f)", s.llr, s.llrLower, s.llrUpper);
    std::fprintf(out, "\n");
    std::fflush(out);
}

static void printFinal(FILE* out, const MatchRunner::Snapshot& s, const MatchSettings& ms){
    std::fprintf(out, "\n%s, %s, %d of %d games played", tournamentModeName(ms.mode), ms.tc.label().c_str(),
                 s.gamesDone, s.gamesTotal);
    if (s.gamesResumed) std::fprintf(out, " (%d from the state file)", s.gamesResumed);
    std::fprintf(out, "\n");
    for (auto& p : s.pairings){
        std::fprintf(out, "%s vs %s: W-D-L %d-%d-%d (%.1f%% draws)  %s\n", s.standings[p.a].name.c_str(),
                     s.standings[p.b].name.c_str(), p.wdl.wins, p.wdl.draws, p.wdl.losses,
                     p.wdl.games() ? 100.0 * p.wdl.draws / p.wdl.games() : 0.0, eloText(p).c_str());
        std::fprintf(out, "  pairs LL/LD/WL+DD/WD/WW: %d %d %d %d %d   time losses %d, crashes %d\n",
                     p.penta.ll, p.penta.ld, p.penta.wl + p.penta.dd, p.penta.wd, p.penta.ww, p.timeLosses, p.crashes);
    }
    if (s.standings.size() > 2){
        auto table = s.standings;
        std::stable_sort(table.begin(), table.end(), [](const Standing& a, const Standing& b){ return a.points() > b.points(); });
        std::fprintf(out, "Standings:\n");
        int rank = 1;
        for (auto& t : table)
            std::fprintf(out, "%3d. %-28s %6.1f / %-5d  +%d =%d -%d\n", rank++, t.name.c_str(), t.points(), t.games(),
                         t.wins, t.draws, t.losses);
    }
    if (s.sprt){
        const char* verdict = s.sprtOutcome == SprtOutcome::AcceptH1 ? "H1 accepted"
                            : s.sprtOutcome == SprtOutcome::AcceptH0 ? "H0 accepted" : "no decision";
        std::fprintf(out, "SPRT [%.2f, %.2f] %s Elo, alpha %.3f, beta %.3f: LLR %.2f (%.2f, %.2f) -> %s\n",
                     ms.sprt.elo0, ms.sprt.elo1, sprtModelName(ms.sprt.model), ms.sprt.alpha, ms.sprt.beta,
                     s.llr, s.llrLower, s.llrUpper, verdict);
    }
    std::fflush(out);
}

static int runMatch(const std::vector<std::string>& args, bool sprt){
    CliOptions o;
    o.sprt = sprt;
    int rc = parseMatchArgs(args, o);
    if (rc >= 0) return rc;
    FILE* out = o.json ? stderr : stdout;
    MatchRunner runner;
    std::string err;
    if (!runner.start(o.s, &err)){ std::fprintf(stderr, "chess-cli: %s\n", err.c_str()); return EXIT_USAGE; }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    long long logShown = 0;
    int lastReport = -1;
    auto follow = [&](const MatchRunner::Snapshot& s){
        if (o.verbose){
            long long first = s.logTotal - (long long)s.log.size();
            for (long long n = std::max(logShown, first); n < s.logTotal; n++) std::fprintf(out, "%s\n", s.log[n - first].c_str());
        }
        logShown = s.logTotal;
        int bucket = s.gamesDone / o.report;
        if (bucket != lastReport && s.gamesDone > 0){ printProgress(out, s); lastReport = bucket; }
    };
    {
        auto s = runner.snapshot();
        std::fprintf(out, "%s: %d games, %d in parallel, %s\n", tournamentModeName(o.s.mode), s.gamesTotal,
                     o.s.concurrency, o.s.tc.label().c_str());
        lastReport = s.gamesDone / o.report;     // don't report resumed games as progress
    }
    while (runner.running()){
        if (g_interrupted){ runner.abort(); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        follow(runner.snapshot());
    }
    auto s = runner.snapshot();
    follow(s);
    printFinal(out, s, o.s);
    if (o.json) std::printf("%s\n", runner.resultsJson().c_str());
    if (g_interrupted){
        if (!o.s.statePath.empty()) std::fprintf(out, "Interrupted; run the same command again to resume.\n");
        return EXIT_INTERRUPTED;
    }
    if (sprt){
        if (s.sprtOutcome == SprtOutcome::AcceptH1) return EXIT_OK;
        if (s.sprtOutcome == SprtOutcome::AcceptH0) return EXIT_H0;
        return EXIT_INCONCLUSIVE;
    }
    return EXIT_OK;
}

// ---------------------------------------------------------------- engine tools
struct ToolArgs {
    bool hasEngine = false;
    EngineConfig engine;
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    bool fenSet = false;
    std::vector<std::string> moves;
    int depth = -1, movetime = -1, multipv = 1, runs = 3;
    long long nodes = -1;
    std::string suite;
    bool divide = false, json = false, quick = false;
    std::vector<std::string> only;
};

using json = nlohmann::json;

static int parseToolArgs(const std::vector<std::string>& a, ToolArgs& t, const std::vector<std::string>& allowed){
    EngineRegistry reg;
    bool regLoaded = false;
    auto allow = [&](const std::string& k){ return std::find(allowed.begin(), allowed.end(), k) != allowed.end(); };
    for (size_t i = 0; i < a.size(); i++){
        const std::string& k = a[i];
        if (!allow(k)) return fail("unknown option '" + k + "'");
        auto value = [&](std::string& v){ if (i + 1 >= a.size()) return false; v = a[++i]; return true; };
        std::string v;
        if (k == "--engine"){ int r = parseEngine(a, i, t.engine, reg, regLoaded); if (r >= 0) return r; t.hasEngine = true; }
        else if (k == "--fen"){ if (!value(t.fen)) return fail("--fen needs a FEN"); t.fenSet = true; }
        else if (k == "--moves"){ while (i + 1 < a.size() && a[i + 1].rfind("--", 0) != 0) t.moves.push_back(a[++i]); }
        else if (k == "--depth"){ if (!value(v) || !parseInt(v, t.depth) || t.depth < 1) return fail("bad --depth"); }
        else if (k == "--movetime"){ if (!value(v) || !parseInt(v, t.movetime) || t.movetime < 1) return fail("bad --movetime"); }
        else if (k == "--nodes"){
            if (!value(v)) return fail("--nodes needs a value");
            char* end = nullptr; t.nodes = std::strtoll(v.c_str(), &end, 10);
            if (!end || *end || t.nodes < 1) return fail("bad --nodes");
        }
        else if (k == "--multipv"){ if (!value(v) || !parseInt(v, t.multipv) || t.multipv < 1) return fail("bad --multipv"); }
        else if (k == "--runs"){ if (!value(v) || !parseInt(v, t.runs) || t.runs < 1) return fail("bad --runs"); }
        else if (k == "--suite"){ if (!value(t.suite)) return fail("--suite needs a file"); }
        else if (k == "--only"){ if (!value(v)) return fail("--only needs check ids"); t.only = splitOn(v, ','); }
        else if (k == "--divide") t.divide = true;
        else if (k == "--quick") t.quick = true;
        else if (k == "--json") t.json = true;
    }
    bool ok = false;
    Position p = Position::fromFEN(t.fen, &ok);
    if (!ok) return fail("invalid FEN '" + t.fen + "'");
    for (auto& m : t.moves){
        Move mv;
        if (!p.uciToMove(m, mv)) return fail("illegal move '" + m + "'");
        p.makeMove(mv);
    }
    return -1;
}

static Position toolPosition(const ToolArgs& t){
    Position p = Position::fromFEN(t.fen);
    for (auto& m : t.moves){ Move mv; p.uciToMove(m, mv); p.makeMove(mv); }
    return p;
}

static bool startEngine(UciEngine& e, const EngineConfig& c){
    if (e.start(c)) return true;
    std::fprintf(stderr, "chess-cli: cannot start %s: %s\n", c.path.c_str(), e.lastError().c_str());
    return false;
}

static int cmdPerft(const std::vector<std::string>& args){
    ToolArgs t;
    int rc = parseToolArgs(args, t, {"--engine", "--fen", "--moves", "--depth", "--suite", "--divide", "--json"});
    if (rc >= 0) return rc;
    if (!t.hasEngine){
        // the GUI's own verified generator
        if (t.depth < 1) return fail("perft needs --depth");
        Position p = toolPosition(t);
        auto t0 = std::chrono::steady_clock::now();
        PerftDivide d = internalPerftDivide(p, t.depth);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (t.json){
            json j = {{"fen", p.toFEN()}, {"depth", t.depth}, {"nodes", d.total}, {"seconds", sec}};
            if (t.divide) j["divide"] = d.moves;
            std::printf("%s\n", j.dump(2).c_str());
        } else {
            if (t.divide) for (auto& kv : d.moves) std::printf("%s: %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
            std::printf("Nodes searched: %lld (depth %d, %.3f s)\n", d.total, t.depth, sec);
        }
        return EXIT_OK;
    }
    UciEngine e;
    if (!startEngine(e, t.engine)) return EXIT_USAGE;
    std::vector<PerftCase> suite;
    if (!t.suite.empty()){
        suite = loadEpdSuite(t.suite);
        if (suite.empty()) return fail("no perft cases in " + t.suite);
    } else if (t.fenSet || !t.moves.empty() || t.depth > 0){
        if (t.depth < 1) return fail("perft --engine with --fen needs --depth");
        Position p = toolPosition(t);
        PerftCase c; c.fen = p.toFEN(); c.depths = {{t.depth, (uint64_t)internalPerftDivide(p, t.depth).total}};
        suite.push_back(c);
    } else suite = builtinPerftSuite();
    int maxDepth = t.depth > 0 ? t.depth : 99;
    std::atomic<bool> abortFlag{false};
    auto results = runEnginePerftSuite(e, suite, maxDepth, [&](int i, int n, const PerftResult& r){
        if (!t.json)
            std::printf("[%d/%d] %s  depth %d: %s (expected %llu, engine %lld)\n", i, n, r.pass ? "PASS" : "FAIL",
                        r.depth, r.fen.c_str(), (unsigned long long)r.expected, r.engineNodes);
        std::fflush(stdout);
    }, abortFlag);
    e.quit();
    int failed = 0, unsupported = 0;
    for (auto& r : results){ if (!r.pass) failed++; if (r.engineNodes < 0) unsupported++; }
    if (t.json){
        json arr = json::array();
        for (auto& r : results) arr.push_back({{"fen", r.fen}, {"depth", r.depth}, {"expected", r.expected},
                                               {"engine", r.engineNodes}, {"pass", r.pass}});
        std::printf("%s\n", json{{"engine", t.engine.name}, {"cases", arr}, {"failed", failed}}.dump(2).c_str());
    } else {
        std::printf("%d of %d passed", (int)results.size() - failed, (int)results.size());
        if (unsupported) std::printf(" (%d without an answer: does the engine support \"go perft\"?)", unsupported);
        std::printf("\n");
        if (failed && !unsupported) std::printf("Run chess-cli perft-bisect on a failing position to find the bug.\n");
    }
    return failed ? EXIT_FAILED : EXIT_OK;
}

static int cmdPerftBisect(const std::vector<std::string>& args){
    ToolArgs t;
    int rc = parseToolArgs(args, t, {"--engine", "--fen", "--moves", "--depth", "--json"});
    if (rc >= 0) return rc;
    if (!t.hasEngine) return fail("perft-bisect needs --engine");
    if (t.depth < 1) return fail("perft-bisect needs --depth");
    UciEngine e;
    if (!startEngine(e, t.engine)) return EXIT_USAGE;
    std::atomic<bool> abortFlag{false};
    BisectReport r = perftBisect(e, toolPosition(t).toFEN(), t.depth, [&](const BisectStep& st){
        if (!t.json)
            std::printf("depth %d  %s  expected %llu, engine %lld%s\n", st.depth, st.fen.c_str(),
                        (unsigned long long)st.expected, st.engineNodes,
                        st.chosenMove.empty() ? "" : ("  -> " + st.chosenMove).c_str());
        std::fflush(stdout);
    }, abortFlag);
    e.quit();
    if (t.json){
        json steps = json::array();
        for (auto& st : r.steps) steps.push_back({{"fen", st.fen}, {"depth", st.depth}, {"expected", st.expected},
                                                  {"engine", st.engineNodes}, {"move", st.chosenMove}});
        std::printf("%s\n", json{{"agrees", r.engineAgrees}, {"found", r.found}, {"error", r.error},
                                 {"bugFen", r.bugFen}, {"missing", r.missing}, {"extra", r.extra},
                                 {"steps", steps}, {"summary", r.summary}}.dump(2).c_str());
    } else std::printf("%s\n", r.summary.c_str());
    if (r.error) return EXIT_USAGE;
    return r.engineAgrees ? EXIT_OK : EXIT_FAILED;
}

static int cmdUciCheck(const std::vector<std::string>& args){
    ToolArgs t;
    int rc = parseToolArgs(args, t, {"--engine", "--quick", "--only", "--json"});
    if (rc >= 0) return rc;
    if (!t.hasEngine) return fail("uci-check needs --engine");
    for (auto& id : t.only)
        if (std::find(uciCheckIds().begin(), uciCheckIds().end(), id) == uciCheckIds().end())
            return fail("unknown check '" + id + "'");
    UciCheckOptions opt;
    opt.quick = t.quick;
    opt.only = t.only;
    std::atomic<bool> abortFlag{false};
    opt.abort = &abortFlag;
    opt.progress = [&](const CheckResult& r){
        if (!t.json) std::printf("%-5s %-28s %s\n", checkStatusName(r.status), r.id.c_str(), r.title.c_str());
        std::fflush(stdout);
    };
    auto results = runUciChecks(t.engine, opt);
    CheckSummary sum = summarize(results);
    if (t.json){
        json arr = json::array();
        for (auto& r : results) arr.push_back({{"id", r.id}, {"title", r.title}, {"status", checkStatusName(r.status)},
                                               {"detail", r.detail}, {"ms", r.ms}});
        std::printf("%s\n", json{{"engine", t.engine.name}, {"checks", arr}, {"pass", sum.pass}, {"warn", sum.warn},
                                 {"fail", sum.fail}, {"skip", sum.skip}}.dump(2).c_str());
    } else {
        std::printf("\n");
        for (auto& r : results)
            if (r.status == CheckStatus::Fail || r.status == CheckStatus::Warn)
                std::printf("%s %s: %s\n", checkStatusName(r.status), r.id.c_str(), r.detail.c_str());
        std::printf("%d passed, %d warnings, %d failed, %d skipped\n", sum.pass, sum.warn, sum.fail, sum.skip);
    }
    return sum.fail ? EXIT_FAILED : EXIT_OK;
}

static int cmdAnalyse(const std::vector<std::string>& args){
    ToolArgs t;
    int rc = parseToolArgs(args, t, {"--engine", "--fen", "--moves", "--depth", "--movetime", "--nodes", "--multipv", "--json"});
    if (rc >= 0) return rc;
    if (!t.hasEngine) return fail("analyse needs --engine");
    int limits = (t.depth > 0) + (t.movetime > 0) + (t.nodes > 0);
    if (limits > 1) return fail("give one of --depth, --movetime, --nodes");
    if (limits == 0) t.movetime = 1000;
    if (t.multipv > 1) t.engine.setOption("MultiPV", std::to_string(t.multipv));
    UciEngine e;
    if (!startEngine(e, t.engine)) return EXIT_USAGE;
    Position p = toolPosition(t);
    e.newGame();
    e.setPosition(p.toFEN(), {});
    auto t0 = std::chrono::steady_clock::now();
    std::string best = t.depth > 0 ? e.goDepth(t.depth) : t.nodes > 0 ? e.goNodes(t.nodes) : e.goMovetime(t.movetime);
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    auto pvs = e.lastPvs();
    EngineInfo inf = e.lastInfo();
    std::string ponder = e.lastPonder();
    bool alive = e.alive();
    e.quit();
    if (best.empty()){
        std::fprintf(stderr, "chess-cli: the engine returned no move%s\n", alive ? "" : " (it crashed)");
        return EXIT_FAILED;
    }
    auto san = [&](const std::string& pv){
        Position q = p; std::string out; std::istringstream in(pv); std::string u; int n = 0;
        while (in >> u && n < 12){
            Move m;
            if (!q.uciToMove(u, m)) break;
            out += (out.empty() ? "" : " ") + q.moveToSAN(m);
            q.makeMove(m); n++;
        }
        return out;
    };
    if (t.json){
        json lines = json::array();
        for (auto& l : pvs){
            if (!l.valid) continue;
            json x = {{"multipv", l.multipv}, {"depth", l.depth}, {"seldepth", l.seldepth}, {"nodes", l.nodes},
                      {"nps", l.nps}, {"pv", l.pv}};
            if (l.isMate) x["mate"] = l.mateIn; else x["cp"] = l.scoreCp;
            lines.push_back(x);
        }
        std::printf("%s\n", json{{"fen", p.toFEN()}, {"bestmove", best}, {"ponder", ponder}, {"ms", ms},
                                 {"depth", inf.depth}, {"nodes", inf.nodes}, {"lines", lines}}.dump(2).c_str());
    } else {
        std::printf("%s\n", p.toFEN().c_str());
        for (auto& l : pvs){
            if (!l.valid) continue;
            std::string sc = l.isMate ? "mate " + std::to_string(l.mateIn) : (l.scoreCp >= 0 ? "+" : "") + std::to_string(l.scoreCp) + " cp";
            std::printf("%d. depth %d/%d  %s  nodes %lld  %s\n", l.multipv, l.depth, l.seldepth, sc.c_str(), l.nodes,
                        san(l.pv).c_str());
        }
        Move bm;
        std::printf("bestmove %s (%s)%s, %lld ms\n", best.c_str(), p.uciToMove(best, bm) ? p.moveToSAN(bm).c_str() : "?",
                    ponder.empty() ? "" : (" ponder " + ponder).c_str(), ms);
    }
    return EXIT_OK;
}

int main(int argc, char** argv){
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h"){
        std::fputs(kUsage, args.empty() ? stderr : stdout);
        return args.empty() ? EXIT_USAGE : EXIT_OK;
    }
    std::string cmd = args[0];
    args.erase(args.begin());
    if (cmd == "bench"){
        int runs = 3; bool js = false;
        for (size_t i = 0; i < args.size(); i++){
            if (args[i] == "--runs" && i + 1 < args.size() && parseInt(args[i + 1], runs) && runs > 0) i++;
            else if (args[i] == "--json") js = true;
            else return fail("bench: unknown option '" + args[i] + "'");
        }
        PerftBenchmark b = runPerftBenchmark(runs);
        if (js) std::printf("{\"nps\": %.0f, \"nodes\": %llu, \"seconds\": %.4f, \"runs\": %d}\n",
                            b.nps, (unsigned long long)b.nodes, b.bestSeconds, runs);
        else std::printf("perft benchmark: %llu nodes in %.3f s (best of %d) = %.0f nodes/s\n",
                         (unsigned long long)b.nodes, b.bestSeconds, runs, b.nps);
        return EXIT_OK;
    }
    if (cmd == "perft") return cmdPerft(args);
    if (cmd == "perft-bisect") return cmdPerftBisect(args);
    if (cmd == "uci-check") return cmdUciCheck(args);
    if (cmd == "analyse" || cmd == "analyze") return cmdAnalyse(args);
    if (cmd == "match") return runMatch(args, false);
    if (cmd == "sprt") return runMatch(args, true);
    return fail("unknown command '" + cmd + "'");
}
