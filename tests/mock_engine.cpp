/* Chess-GUI-for-UCI — mock UCI engine for tests. GPL-2.0
 *
 * A tiny but protocol-correct UCI engine built on the GUI's own move
 * generator. Command-line flags make it misbehave in specific ways so the
 * GUI's error handling, the UCI compliance checker and the perft bisection
 * tool can be tested without a real engine:
 *
 *   --name NAME / --author NAME   id strings
 *   --report-env                  print "info string cwd ..." / "args ..." after "uci"
 *   --stderr TEXT                 write TEXT to stderr at startup
 *   --no-uciok                    never answer "uci" with uciok
 *   --slow-uciok MS               delay uciok
 *   --no-readyok                  never answer isready
 *   --slow-readyok MS             delay every readyok
 *   --busy-readyok                ignore isready while searching
 *   --illegal-bestmove            answer every go with an illegal move
 *   --none-bestmove               answer "bestmove (none)" even when moves exist
 *   --ignore-stop                 ignore "stop" (infinite searches never end)
 *   --crash-on-go N               exit abruptly during the Nth "go" (1-based)
 *   --output-after-bestmove       keep printing info lines after bestmove
 *   --mate-blind                  never report mate scores
 *   --time-hog                    spend the whole remaining clock on every move
 *   --no-perft                    reply "Unknown command" to "go perft"
 *   --perft-bug KIND              deliberately broken move generator for perft:
 *                                 ep (no en passant), underpromo (no N/B/R promotions),
 *                                 castle (no queenside castling),
 *                                 king-into-check (king may step onto attacked squares)
 *   --no-quit                     ignore "quit" (must be killed)
 *   --exit-at-start CODE          exit immediately with CODE (not a UCI engine)
 */
#include "chess.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Flags {
    std::string name = "MockEngine 1.0", author = "Chess-GUI-for-UCI tests", stderrText, perftBug;
    bool reportEnv = false, noUciok = false, noReadyok = false, busyReadyok = false;
    bool illegalBestmove = false, noneBestmove = false, ignoreStop = false;
    bool outputAfterBestmove = false, mateBlind = false, timeHog = false, noPerft = false, noQuit = false;
    int slowUciok = 0, slowReadyok = 0, crashOnGo = 0;
} F;

std::mutex outMx;
void out(const std::string& s){
    std::lock_guard<std::mutex> lk(outMx);
    std::cout << s << "\n" << std::flush;
}
void sleepMs(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
using clk = std::chrono::steady_clock;
long long msSince(clk::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count();
}

// ---- move generation (optionally buggy, for perft) ----
std::vector<Move> genMoves(const Position& p, bool buggy){
    std::vector<Move> legal; p.generateLegal(legal);
    if (!buggy || F.perftBug.empty()) return legal;
    std::vector<Move> outv;
    for (const Move& m : legal){
        if (F.perftBug == "ep" && m.isEnPassant) continue;
        if (F.perftBug == "underpromo" && m.promo && m.promo != WHITE_QUEEN) continue;
        if (F.perftBug == "castle" && m.isCastle && m.to % 8 == 2) continue;
        outv.push_back(m);
    }
    if (F.perftBug == "king-into-check"){
        // add pseudo-legal king steps onto attacked squares (illegal moves)
        std::vector<Move> pseudo; p.generatePseudo(pseudo);
        for (const Move& m : pseudo){
            if (std::abs(p.board[m.from]) != 20 || m.isCastle) continue;
            if (std::find(outv.begin(), outv.end(), m) != outv.end()) continue;
            Position q = p; q.makeMove(m);
            if (q.kingSquare(!q.whiteToMove) >= 0) outv.push_back(m);
        }
    }
    return outv;
}
uint64_t perft(const Position& p, int d){
    if (d <= 0) return 1;
    std::vector<Move> ms = genMoves(p, true);
    if (d == 1) return ms.size();
    uint64_t n = 0;
    for (const Move& m : ms){ Position q = p; q.makeMove(m); n += perft(q, d - 1); }
    return n;
}

// ---- search state ----
Position pos = Position::startpos();
std::atomic<bool> stopFlag{false}, searching{false}, ponderhitFlag{false};
std::thread searchThread;
int goCount = 0;

struct GoParams {
    int movetime = -1, depth = -1, wtime = -1, btime = -1, winc = 0, binc = 0;
    long long nodes = -1;
    bool infinite = false, ponder = false;
};

int material(const Position& p){
    int s = 0;
    for (int v : p.board){
        int a = std::abs(v), val = a==1?100: a==3?300: a==4?320: a==5?500: a==9?900: 0;
        s += v > 0 ? val : -val;
    }
    return p.whiteToMove ? s : -s;
}

static bool isMate(const Position& q){
    std::vector<Move> r; q.generateLegal(r);
    return r.empty() && q.inCheck(q.whiteToMove);
}

// Choose a move: a mate in one or two if there is one, else the most valuable
// capture, else the first legal move. Deterministic, so tests are reproducible.
// `mate` receives the mate distance in moves (0 = none found).
bool pickMove(const Position& p, Move& best, int& mate){
    std::vector<Move> ms; p.generateLegal(ms);
    if (ms.empty()) return false;
    mate = 0; best = ms[0]; int bestCap = -1;
    for (const Move& m : ms){
        Position q = p; q.makeMove(m);
        if (isMate(q)){ best = m; mate = 1; return true; }
        int cap = std::abs(m.captured);
        if (cap > bestCap){ bestCap = cap; best = m; }
    }
    if (bestCap <= 0) best = ms[0];
    for (const Move& m : ms){                       // mate in 2: every reply allows a mate in 1
        Position q = p; q.makeMove(m);
        std::vector<Move> replies; q.generateLegal(replies);
        if (replies.empty()) continue;              // stalemate
        bool all = true;
        for (const Move& r : replies){
            Position s2 = q; s2.makeMove(r);
            std::vector<Move> ours; s2.generateLegal(ours);
            bool found = false;
            for (const Move& m2 : ours){ Position t = s2; t.makeMove(m2); if (isMate(t)){ found = true; break; } }
            if (!found){ all = false; break; }
        }
        if (all){ best = m; mate = 2; return true; }
    }
    return true;
}

int thinkTimeMs(const GoParams& g, const Position& p){
    if (g.movetime >= 0) return g.movetime;
    if (g.depth >= 0 || g.nodes >= 0) return 0;
    int t = p.whiteToMove ? g.wtime : g.btime, inc = p.whiteToMove ? g.winc : g.binc;
    if (t < 0) return 0;
    if (F.timeHog) return t + 50;
    return std::max(1, std::min(t / 30 + inc / 2, std::max(1, t - 50)));
}

void runSearch(GoParams g, Position p){
    auto t0 = clk::now();
    goCount++;
    if (F.crashOnGo && goCount == F.crashOnGo){ sleepMs(20); std::fflush(stdout); std::_Exit(3); }
    Move best; int mate = 0;
    bool have = pickMove(p, best, mate);
    int limit = thinkTimeMs(g, p);
    bool waitStop = g.infinite || g.ponder;
    int depth = 0; long long nodes = 0;
    auto info = [&](){
        depth++; nodes += 1000 + 37 * depth;
        long long ms = std::max(1LL, msSince(t0));
        std::ostringstream o;
        o << "info depth " << depth << " seldepth " << depth + 2 << " multipv 1 score ";
        if (mate && !F.mateBlind) o << "mate " << mate; else o << "cp " << material(p);
        o << " nodes " << nodes << " nps " << nodes * 1000 / ms << " hashfull " << std::min(1000, depth * 3)
          << " tbhits 0 time " << ms;
        if (have) o << " pv " << p.moveToUCI(best);
        out(o.str());
    };
    info();
    while (true){
        if (stopFlag && !F.ignoreStop) break;
        if (g.ponder && ponderhitFlag){             // ponderhit: becomes a normal timed search
            g.ponder = false; waitStop = g.infinite; t0 = clk::now();
        }
        if (!waitStop && msSince(t0) >= limit) break;
        if (g.depth >= 0 && depth >= std::max(1, g.depth)) break;
        sleepMs(std::min(10, std::max(1, limit / 8)));
        if (depth < 60 && msSince(t0) / 50 >= depth) info();
    }
    std::string bm;
    if (!have || F.noneBestmove) bm = "(none)";
    else if (F.illegalBestmove) bm = "a1a1";
    else bm = p.moveToUCI(best);
    out("bestmove " + bm);
    searching = false;
    if (F.outputAfterBestmove){ sleepMs(5); out("info depth 99 score cp 0 string after bestmove"); }
}

void stopSearch(){
    stopFlag = true;
    if (searchThread.joinable() && !F.ignoreStop) searchThread.join();
}

void handlePosition(std::istringstream& ss){
    std::string tok; ss >> tok;
    Position p;
    if (tok == "startpos"){ p = Position::startpos(); ss >> tok; }
    else if (tok == "fen"){
        std::string fen, part;
        while (ss >> part && part != "moves") fen += (fen.empty() ? "" : " ") + part;
        bool ok = false; p = Position::fromFEN(fen, &ok);
        if (!ok){ out("info string invalid fen"); return; }
        tok = part;
    } else return;
    if (tok == "moves"){
        std::string mv;
        while (ss >> mv){
            Move m;
            if (!p.uciToMove(mv, m)){ out("info string illegal move " + mv); break; }
            p.makeMove(m);
        }
    }
    pos = p;
}

void handleGo(std::istringstream& ss){
    GoParams g; std::string tok;
    while (ss >> tok){
        if (tok == "perft"){
            int d = 1; ss >> d;
            if (F.noPerft){ out("Unknown command: go perft"); return; }
            uint64_t total = 0;
            for (const Move& m : genMoves(pos, true)){
                Position q = pos; q.makeMove(m);
                uint64_t n = perft(q, d - 1); total += n;
                out(pos.moveToUCI(m) + ": " + std::to_string(n));
            }
            out(""); out("Nodes searched: " + std::to_string(total)); out("");
            return;
        }
        else if (tok == "movetime") ss >> g.movetime;
        else if (tok == "depth") ss >> g.depth;
        else if (tok == "nodes") ss >> g.nodes;
        else if (tok == "wtime") ss >> g.wtime;
        else if (tok == "btime") ss >> g.btime;
        else if (tok == "winc") ss >> g.winc;
        else if (tok == "binc") ss >> g.binc;
        else if (tok == "infinite") g.infinite = true;
        else if (tok == "ponder") g.ponder = true;
    }
    if (searchThread.joinable()) searchThread.join();
    stopFlag = false; ponderhitFlag = false; searching = true;
    searchThread = std::thread(runSearch, g, pos);
}

} // namespace

int main(int argc, char** argv){
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); i++){
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : ""; };
        if (a == "--name") F.name = next();
        else if (a == "--author") F.author = next();
        else if (a == "--report-env") F.reportEnv = true;
        else if (a == "--stderr") F.stderrText = next();
        else if (a == "--no-uciok") F.noUciok = true;
        else if (a == "--slow-uciok") F.slowUciok = std::atoi(next().c_str());
        else if (a == "--no-readyok") F.noReadyok = true;
        else if (a == "--slow-readyok") F.slowReadyok = std::atoi(next().c_str());
        else if (a == "--busy-readyok") F.busyReadyok = true;
        else if (a == "--illegal-bestmove") F.illegalBestmove = true;
        else if (a == "--none-bestmove") F.noneBestmove = true;
        else if (a == "--ignore-stop") F.ignoreStop = true;
        else if (a == "--crash-on-go") F.crashOnGo = std::atoi(next().c_str());
        else if (a == "--output-after-bestmove") F.outputAfterBestmove = true;
        else if (a == "--mate-blind") F.mateBlind = true;
        else if (a == "--time-hog") F.timeHog = true;
        else if (a == "--no-perft") F.noPerft = true;
        else if (a == "--perft-bug") F.perftBug = next();
        else if (a == "--no-quit") F.noQuit = true;
        else if (a == "--exit-at-start"){ std::cerr << "mock_engine: exiting at start" << std::endl;
                                          return std::atoi(next().c_str()); }
        else { std::cerr << "mock_engine: unknown flag " << a << "\n"; }
    }
    if (!F.stderrText.empty()) std::cerr << F.stderrText << std::endl;

    std::string line;
    while (std::getline(std::cin, line)){
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string cmd; ss >> cmd;
        if (cmd == "uci"){
            out("id name " + F.name);
            out("id author " + F.author);
            out("option name Hash type spin default 16 min 1 max 1024");
            out("option name Threads type spin default 1 min 1 max 64");
            out("option name MultiPV type spin default 1 min 1 max 256");
            out("option name Ponder type check default false");
            out("option name Style type combo default Normal var Solid var Normal var Risky");
            out("option name EvalFile type string default <empty>");
            out("option name Clear Hash type button");
            if (F.reportEnv){
                std::error_code ec;
                out("info string cwd " + std::filesystem::current_path(ec).string());
                std::string all;
                for (int i = 1; i < argc; i++) all += std::string(i > 1 ? "|" : "") + argv[i];
                out("info string args " + all);
            }
            if (F.slowUciok) sleepMs(F.slowUciok);
            if (!F.noUciok) out("uciok");
        }
        else if (cmd == "isready"){
            if (F.noReadyok || (F.busyReadyok && searching)) continue;
            if (F.slowReadyok) sleepMs(F.slowReadyok);
            out("readyok");
        }
        else if (cmd == "ucinewgame"){ stopSearch(); pos = Position::startpos(); }
        else if (cmd == "position") handlePosition(ss);
        else if (cmd == "go") handleGo(ss);
        else if (cmd == "stop") stopSearch();
        else if (cmd == "ponderhit") ponderhitFlag = true;
        else if (cmd == "setoption" || cmd == "debug" || cmd == "register") {}
        else if (cmd == "quit"){
            if (F.noQuit) continue;
            break;
        }
        else if (!cmd.empty()) out("Unknown command: " + line);
    }
    stopFlag = true;
    if (searchThread.joinable()){
        if (F.ignoreStop) searchThread.detach(); else searchThread.join();
    }
    std::fflush(stdout);
    std::_Exit(0);
}
