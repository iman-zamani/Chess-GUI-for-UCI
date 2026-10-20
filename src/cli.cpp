/* Chess-GUI-for-UCI — headless command-line tool (chess-cli). GPL-2.0 */
#include "engine_config.hpp"
#include "stats.hpp"
#include "testing.hpp"
#include "tournament.hpp"
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
enum Exit { EXIT_OK = 0, EXIT_H0 = 1, EXIT_USAGE = 2, EXIT_INCONCLUSIVE = 3, EXIT_INTERRUPTED = 130 };

static std::atomic<bool> g_interrupted{false};
static void onSignal(int){ g_interrupted = true; }

static const char* kUsage =
R"(chess-cli - headless tools for UCI engine developers

Usage:
  chess-cli match [options]     play a match, round-robin or gauntlet
  chess-cli sprt  [options]     a match that stops when the SPRT accepts H0 or H1
  chess-cli bench [--runs N] [--json]   single-threaded speed (internal perft, best of N runs)
  chess-cli help

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

Exit codes: 0 finished (sprt: H1 accepted), 1 sprt: H0 accepted, 2 usage or start error,
3 sprt: games ran out before a decision, 130 interrupted (the state file can resume it).
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
            if (i + 1 >= a.size()) return fail("--engine needs a name or cmd=PATH");
            if (a[i + 1].rfind("cmd=", 0) != 0){
                if (!regLoaded){ reg.load((fs::u8path(userConfigDir()) / "engines.json").u8string()); regLoaded = true; }
                const EngineConfig* c = reg.findByName(a[++i]);
                if (!c) return fail("no engine named '" + a[i] + "' in " + userConfigDir() + "/engines.json");
                o.s.engines.push_back(*c);
                continue;
            }
            EngineConfig c;
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
    if (cmd == "match") return runMatch(args, false);
    if (cmd == "sprt") return runMatch(args, true);
    return fail("unknown command '" + cmd + "'");
}
