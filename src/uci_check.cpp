/* Chess-GUI-for-UCI — UCI compliance checker. GPL-2.0 */
#include "uci_check.hpp"
#include "chess.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>

using clk = std::chrono::steady_clock;
static long long msSince(clk::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count();
}

const char* checkStatusName(CheckStatus s){
    switch (s){
    case CheckStatus::Pass: return "PASS";
    case CheckStatus::Warn: return "WARN";
    case CheckStatus::Fail: return "FAIL";
    default: return "SKIP";
    }
}

namespace {

// One check: owns a result, collects the engine log produced while it runs.
struct Check {
    CheckResult r;
    clk::time_point t0 = clk::now();
    uint64_t cursor = 0;
    Check(const char* id, const char* title){ r.id = id; r.title = title; }
    void fail(const std::string& why){ r.status = CheckStatus::Fail; add(why); }
    void warn(const std::string& why){ if (r.status != CheckStatus::Fail) r.status = CheckStatus::Warn; add(why); }
    void note(const std::string& s){ add(s); }
    void add(const std::string& s){ if (!r.detail.empty()) r.detail += "\n"; r.detail += s; }
    void grab(UciEngine& e){
        for (auto& l : e.logSince(cursor)) r.log.push_back(l);
        if (r.log.size() > 400) r.log.erase(r.log.begin(), r.log.end() - 400);   // keep the tail
    }
    CheckResult done(UciEngine* e){
        if (e) grab(*e);
        r.ms = msSince(t0);
        return r;
    }
};

std::string firstWords(const std::string& s, size_t n){ return s.size() > n ? s.substr(0, n) + "..." : s; }

// Reads until a bestmove line; returns it (or "" on timeout/crash). Every other
// line read meanwhile is in the engine log.
std::string readBestmove(UciEngine& e, int timeoutMs, long long* elapsed = nullptr){
    auto t0 = clk::now();
    while (msSince(t0) < timeoutMs && e.alive()){
        std::string ln = e.readLine(50);
        if (ln.rfind("bestmove", 0) == 0){ if (elapsed) *elapsed = msSince(t0); return ln; }
    }
    if (elapsed) *elapsed = msSince(t0);
    return "";
}
std::string moveOf(const std::string& bestmoveLine){
    std::istringstream ss(bestmoveLine); std::string a, b; ss >> a >> b; return b;
}
// Lines the engine prints in the `ms` after bestmove (should be none).
std::vector<std::string> trailingOutput(UciEngine& e, int ms){
    std::vector<std::string> out;
    auto t0 = clk::now();
    while (msSince(t0) < ms && e.alive()){
        std::string ln = e.readLine(20);
        if (!ln.empty()) out.push_back(ln);
    }
    return out;
}
bool waitReadyok(UciEngine& e, int timeoutMs, long long* elapsed = nullptr){
    auto t0 = clk::now();
    e.sendRaw("isready");
    while (msSince(t0) < timeoutMs && e.alive()){
        if (e.readLine(20) == "readyok"){ if (elapsed) *elapsed = msSince(t0); return true; }
    }
    if (elapsed) *elapsed = msSince(t0);
    return false;
}

Position replay(const std::string& fen, const std::vector<std::string>& moves){
    Position p = Position::fromFEN(fen);
    for (auto& u : moves){ Move m; if (!p.uciToMove(u, m)) break; p.makeMove(m); }
    return p;
}

// Checks a bestmove answer for legality; returns "" when fine.
std::string judgeMove(const Position& p, const std::string& line){
    if (line.empty()) return "no bestmove";
    std::string mv = moveOf(line);
    std::vector<Move> legal; p.generateLegal(legal);
    if (mv == "(none)" || mv == "0000" || mv.empty())
        return legal.empty() ? "" : "'" + line + "' although the position has legal moves";
    Move m;
    if (!p.uciToMove(mv, m)) return "illegal move '" + mv + "' in " + p.toFEN();
    return "";
}

bool started(Check& c, UciEngine& e, const EngineConfig& cfg){
    e.setStartTimeouts(10000, 30000);
    e.setRole("UCI check");
    if (e.start(cfg)) return true;
    c.fail("The engine did not start: " + e.lastError());
    return false;
}

// A long, legal, still-ongoing game from the start position (deterministic).
std::vector<std::string> longGame(int plies){
    std::vector<std::string> out;
    Game g; g.reset(Position::startpos());
    for (int ply = 0; ply < plies; ply++){
        std::vector<Move> legal; g.pos.generateLegal(legal);
        bool moved = false;
        for (size_t k = 0; k < legal.size() && !moved; k++){
            const Move& m = legal[(ply * 7 + k * 3) % legal.size()];
            Game t = g;
            if (!t.tryMove(m)) continue;
            if (t.result != GameResult::ONGOING) continue;        // keep the game alive
            out.push_back(g.pos.moveToUCI(m));
            g = t; moved = true;
        }
        if (!moved) break;
    }
    return out;
}

// ---------------------------------------------------------------- checks
CheckResult checkHandshake(const EngineConfig& cfg, UciEngine& e, bool& startedOk){
    Check c("handshake", "uci -> id name, id author, options, uciok");
    startedOk = started(c, e, cfg);
    if (!startedOk) return c.done(&e);
    if (e.name().empty() || e.name() == "engine") c.warn("No 'id name' line before uciok.");
    else c.note("id name: " + e.name());
    if (e.author().empty()) c.warn("No 'id author' line before uciok (the UCI spec asks for both id lines).");
    std::set<std::string> seen;
    int bad = 0;
    for (auto& o : e.options()){
        std::string why;
        std::string lname = o.name; for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
        if (!seen.insert(lname).second) why = "is advertised twice";
        else if (o.type == "spin"){
            if (o.minV > o.maxV) why = "has min > max";
            else {
                long long d = std::atoll(o.defVal.c_str());
                if (o.defVal.empty() || d < o.minV || d > o.maxV) why = "default '" + o.defVal + "' outside [min, max]";
            }
        } else if (o.type == "check"){
            if (o.defVal != "true" && o.defVal != "false") why = "check default must be true or false, is '" + o.defVal + "'";
        } else if (o.type == "combo"){
            if (o.vars.empty()) why = "combo without any var";
            else if (std::find(o.vars.begin(), o.vars.end(), o.defVal) == o.vars.end()) why = "default '" + o.defVal + "' is not one of its vars";
        } else if (o.type != "string" && o.type != "button") why = "unknown type '" + o.type + "'";
        if (!why.empty()){ c.warn("Option '" + o.name + "' " + why + "."); bad++; }
    }
    c.note(std::to_string(e.options().size()) + " options advertised" + (bad ? "" : ", all well-formed") + ".");
    return c.done(&e);
}

CheckResult checkIsreadyIdle(UciEngine& e){
    Check c("isready-idle", "isready -> readyok while idle");
    c.cursor = 0; e.logSince(c.cursor);
    long long ms = 0;
    if (!waitReadyok(e, 5000, &ms)) c.fail("No readyok within 5 s.");
    else if (ms > 1000) c.warn("readyok took " + std::to_string(ms) + " ms (expected well under a second when idle).");
    else c.note("readyok after " + std::to_string(ms) + " ms.");
    return c.done(&e);
}

CheckResult checkIsreadyDuringSearch(const EngineConfig& cfg){
    Check c("isready-during-search", "isready -> readyok during a search (go infinite)");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    e.setPosition(Position::startpos().toFEN(), {});
    e.sendRaw("go infinite");
    auto t0 = clk::now();
    while (msSince(t0) < 300) e.readLine(20);
    long long ms = 0;
    bool ok = waitReadyok(e, 3000, &ms);
    if (!ok) c.fail("No readyok within 3 s while searching. The spec requires an immediate readyok, "
                    "even during a search; GUIs use it to check that the engine is alive.");
    else if (ms > 500) c.warn("readyok during the search took " + std::to_string(ms) + " ms.");
    else c.note("readyok after " + std::to_string(ms) + " ms while searching.");
    e.sendRaw("stop");
    std::string bm = readBestmove(e, 3000);
    if (bm.empty()) c.fail("No bestmove within 3 s after stop.");
    return c.done(&e);
}

CheckResult checkStop(const EngineConfig& cfg){
    Check c("stop", "go infinite + stop -> bestmove quickly, a legal move");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    Position p = Position::startpos();
    e.setPosition(p.toFEN(), {});
    e.sendRaw("go infinite");
    auto t0 = clk::now();
    bool early = false;
    while (msSince(t0) < 500 && e.alive())
        if (e.readLine(20).rfind("bestmove", 0) == 0) early = true;
    if (early) c.fail("bestmove arrived before stop during 'go infinite' (the engine must wait for stop).");
    long long ms = 0;
    e.sendRaw("stop");
    std::string bm = readBestmove(e, 5000, &ms);
    if (bm.empty()){ c.fail("No bestmove within 5 s of stop."); return c.done(&e); }
    std::string why = judgeMove(p, bm);
    if (!why.empty()) c.fail(why);
    if (ms > 1000) c.fail("bestmove came " + std::to_string(ms) + " ms after stop.");
    else if (ms > 200) c.warn("bestmove came " + std::to_string(ms) + " ms after stop (expected < 200 ms).");
    else c.note("bestmove " + std::to_string(ms) + " ms after stop.");
    return c.done(&e);
}

struct GoCase { const char* label; std::string cmd; int limitMs, warnMs; };

CheckResult checkGoModes(const EngineConfig& cfg){
    Check c("go-modes", "go movetime / depth / nodes / wtime+btime / movestogo -> legal bestmove in time");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    Position p = Position::fromFEN("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    std::vector<GoCase> cases = {
        {"movetime 300", "go movetime 300", 2000, 800},
        {"depth 4", "go depth 4", 60000, 20000},
        {"nodes 20000", "go nodes 20000", 60000, 20000},
        {"wtime/btime 2 s, no increment", "go wtime 2000 btime 2000 winc 0 binc 0", 2000, 1000},
        {"wtime/btime 1 s + 0.1 s", "go wtime 1000 btime 1000 winc 100 binc 100", 1100, 700},
        {"movestogo 1", "go wtime 1000 btime 1000 movestogo 1", 1000, 1000},
    };
    for (auto& gc : cases){
        e.setPosition(p.toFEN(), {});
        e.sendRaw(gc.cmd);
        long long ms = 0;
        std::string bm = readBestmove(e, gc.limitMs + 5000, &ms);
        std::string why = judgeMove(p, bm);
        if (!why.empty()){ c.fail(std::string(gc.label) + ": " + why); if (!e.alive()) break; continue; }
        if (ms > gc.limitMs) c.fail(std::string(gc.label) + ": took " + std::to_string(ms) + " ms (limit " + std::to_string(gc.limitMs) + ").");
        else if (ms > gc.warnMs) c.warn(std::string(gc.label) + ": took " + std::to_string(ms) + " ms.");
        else c.note(std::string(gc.label) + ": " + moveOf(bm) + " in " + std::to_string(ms) + " ms.");
        auto trail = trailingOutput(e, 200);
        if (!trail.empty())
            c.fail(std::string(gc.label) + ": output after bestmove ('" + firstWords(trail[0], 60) + "'). Nothing may follow bestmove until the next command.");
    }
    if (!e.alive()) c.fail("The engine exited. stderr: " + e.stderrTail(3));
    return c.done(&e);
}

CheckResult checkPositions(const EngineConfig& cfg){
    Check c("positions", "position startpos moves (320 plies) and unusual but legal FENs");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    auto moves = longGame(320);
    Position end = replay(Position::startpos().toFEN(), moves);
    std::string cmd = "position startpos moves";
    for (auto& m : moves) cmd += " " + m;
    e.sendRaw(cmd);
    e.sendRaw("go movetime 200");
    std::string bm = readBestmove(e, 10000);
    std::string why = judgeMove(end, bm);
    if (!why.empty()) c.fail("After " + std::to_string(moves.size()) + " plies via 'position startpos moves': " + why);
    else c.note(std::to_string(moves.size()) + "-ply move list: " + moveOf(bm));
    const char* fens[][2] = {
        {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "en passant available"},
        {"r3k2r/8/8/8/8/8/8/R3K2R b Kq - 0 1", "partial castling rights"},
        {"8/8/4k3/8/2p5/8/3P4/4K3 w - - 99 250", "high move counters (halfmove 99, move 250)"},
        {"4k3/8/8/8/8/8/8/R3K2r w Q - 0 1", "in check, castling right present but not usable"},
        {"8/1P6/8/8/8/8/6k1/K7 w - - 0 1", "promotion available"},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", "FEN without move counters"},
    };
    for (auto& f : fens){
        if (!e.alive()) break;
        bool ok = false;
        Position p = Position::fromFEN(f[0], &ok);
        e.sendRaw(std::string("position fen ") + f[0]);
        e.sendRaw("go movetime 150");
        std::string line = readBestmove(e, 8000);
        std::string w = judgeMove(p, line);
        if (!w.empty()) c.fail(std::string(f[1]) + ": " + w);
        else c.note(std::string(f[1]) + ": " + moveOf(line));
    }
    if (!e.alive()) c.fail("The engine exited. stderr: " + e.stderrTail(3));
    return c.done(&e);
}

CheckResult checkNoLegalMoves(const EngineConfig& cfg){
    Check c("no-legal-moves", "positions without legal moves (checkmate, stalemate)");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    const char* fens[][2] = {
        {"7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", "checkmated"},
        {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", "stalemated"},
    };
    for (auto& f : fens){
        e.sendRaw(std::string("position fen ") + f[0]);
        e.sendRaw("go movetime 200");
        std::string line = readBestmove(e, 5000);
        std::string mv = moveOf(line);
        if (!e.alive()){ c.fail(std::string(f[1]) + ": the engine crashed. stderr: " + e.stderrTail(3)); break; }
        if (line.empty()) c.fail(std::string(f[1]) + ": no bestmove within 5 s (the engine hangs).");
        else if (mv == "(none)" || mv == "0000") c.note(std::string(f[1]) + ": " + line);
        else c.warn(std::string(f[1]) + ": answered '" + line + "'; the usual answer is 'bestmove (none)' or 'bestmove 0000'.");
    }
    return c.done(&e);
}

CheckResult checkMateScores(const EngineConfig& cfg){
    Check c("mate-scores", "forced mates are reported as mate scores");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    struct M { const char* fen; int n; const char* first; };
    const M cases[] = {
        {"6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", 1, "a1a8"},
        {"kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", 2, "a1a6"},
    };
    for (auto& m : cases){
        e.setPosition(m.fen, {});
        e.sendRaw("go movetime 1000");
        std::string line = readBestmove(e, 8000);
        EngineInfo info;
        std::vector<PvLine> pvs;
        uint64_t cur = 0;
        for (auto& l : e.logSince(cur)) if (l.dir == '<') parseInfoLine(l.text, info, pvs);
        std::string tag = "mate in " + std::to_string(m.n);
        if (line.empty()){ c.fail(tag + ": no bestmove."); continue; }
        if (moveOf(line) != m.first) c.warn(tag + ": played " + moveOf(line) + ", the mating move is " + m.first + ".");
        if (!info.isMate) c.warn(tag + ": last score was 'cp " + std::to_string(info.scoreCp) + "', not a mate score.");
        else if (info.mateIn != m.n) c.warn(tag + ": reported 'mate " + std::to_string(info.mateIn) + "'.");
        else c.note(tag + ": reported mate " + std::to_string(info.mateIn) + " with " + moveOf(line) + ".");
    }
    return c.done(&e);
}

// Self-play with real clocks; the engine plays both sides.
void timeManagementGame(Check& c, UciEngine& e, int baseMs, int incMs, int movesPerSide, const char* label){
    long long clock[2] = {baseMs, baseMs};
    long long used[2] = {0, 0};
    int moves[2] = {0, 0};
    Game g; g.reset(Position::fromFEN("r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 4 4"));
    e.newGame();
    std::vector<std::string> ucis;
    std::string startFen = g.start.toFEN();
    bool lost = false;
    while (g.result == GameResult::ONGOING && moves[0] < movesPerSide && e.alive()){
        int side = g.pos.whiteToMove ? 0 : 1;
        e.setPosition(startFen, ucis);
        char cmd[160];
        std::snprintf(cmd, sizeof cmd, "go wtime %lld btime %lld winc %d binc %d", clock[0], clock[1], incMs, incMs);
        auto t0 = clk::now();
        e.sendRaw(cmd);
        std::string line = readBestmove(e, (int)clock[side] + 5000);
        long long spent = msSince(t0);
        clock[side] -= spent; used[side] += spent;
        if (clock[side] < 0){
            c.fail(std::string(label) + ": lost on time on move " + std::to_string(moves[side] + 1)
                   + " (" + std::to_string(-clock[side]) + " ms over).");
            lost = true; break;
        }
        clock[side] += incMs;
        std::string why = judgeMove(g.pos, line);
        if (!why.empty()){ c.fail(std::string(label) + ": " + why); lost = true; break; }
        Move m; g.pos.uciToMove(moveOf(line), m);
        g.tryMove(m);
        ucis.push_back(moveOf(line));
        moves[side]++;
    }
    if (!e.alive()){ c.fail(std::string(label) + ": the engine exited. stderr: " + e.stderrTail(3)); return; }
    if (lost) return;
    double avail = baseMs + (double)incMs * std::max(moves[0], 1);
    double pct = 100.0 * std::max(used[0], used[1]) / avail;
    char b[200];
    std::snprintf(b, sizeof b, "%s: %d moves per side, no time loss; used %.0f%% of the available time (%.0f ms max).",
                  label, moves[0], pct, (double)std::max(used[0], used[1]));
    if (pct < 5) c.warn(std::string(b) + " That is very little: the engine may be wasting its time.");
    else c.note(b);
}

CheckResult checkTimeManagement(const EngineConfig& cfg, bool quick){
    Check c("time-management", quick ? "time management: self-play game at 1s+0.01s (quick)"
                                     : "time management: self-play games at 1s+0.01s and 10s+0.1s");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    timeManagementGame(c, e, 1000, 10, quick ? 25 : 50, "1s + 0.01s");
    if (e.alive() && !quick) timeManagementGame(c, e, 10000, 100, 50, "10s + 0.1s");
    return c.done(&e);
}

CheckResult checkSetoption(const EngineConfig& cfg){
    Check c("setoption", "setoption for every advertised option, then isready");
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    int tried = 0;
    for (auto& o : e.options()){
        std::vector<std::string> values;
        if (o.type == "spin"){ values = {std::to_string(o.minV), o.defVal}; }
        else if (o.type == "check") values = {o.defVal == "true" ? "false" : "true", o.defVal};
        else if (o.type == "combo"){ values = o.vars; values.push_back(o.defVal); }
        else if (o.type == "string") values = {o.defVal};
        else if (o.type == "button") values = {""};
        for (auto& v : values){
            e.setOption(o.name, v == "" && o.type == "string" ? "<empty>" : v);
            tried++;
            if (!waitReadyok(e, 10000)){
                c.fail("No readyok after 'setoption name " + o.name + (v.empty() ? "" : " value " + v) + "'"
                       + (e.alive() ? "." : ": the engine exited. stderr: " + e.stderrTail(3)));
                return c.done(&e);
            }
        }
    }
    e.setPosition(Position::startpos().toFEN(), {});
    e.sendRaw("go depth 2");
    std::string why = judgeMove(Position::startpos(), readBestmove(e, 20000));
    if (!why.empty()) c.fail("Search after setting the options: " + why);
    c.note(std::to_string(tried) + " setoption commands accepted.");
    return c.done(&e);
}

CheckResult checkNewGame(UciEngine& e){
    Check c("ucinewgame", "ucinewgame + isready, then a normal search");
    e.logSince(c.cursor);
    e.setPosition(Position::startpos().toFEN(), {"e2e4"});
    e.sendRaw("go depth 3");
    readBestmove(e, 20000);
    e.sendRaw("ucinewgame");
    long long ms = 0;
    if (!waitReadyok(e, 10000, &ms)){ c.fail("No readyok within 10 s after ucinewgame."); return c.done(&e); }
    if (ms > 2000) c.warn("readyok after ucinewgame took " + std::to_string(ms) + " ms.");
    Position p = Position::fromFEN("8/8/4k3/8/8/4K3/4P3/8 w - - 0 1");
    e.setPosition(p.toFEN(), {});
    e.sendRaw("go depth 3");
    std::string why = judgeMove(p, readBestmove(e, 20000));
    if (!why.empty()) c.fail("Search after ucinewgame: " + why);
    else c.note("ucinewgame accepted, readyok after " + std::to_string(ms) + " ms.");
    return c.done(&e);
}

CheckResult checkPonder(const EngineConfig& cfg, bool hasPonder){
    Check c("ponder", "go ponder + ponderhit, and go ponder + stop");
    if (!hasPonder){
        c.r.status = CheckStatus::Skip;
        c.note("The engine does not advertise the Ponder option.");
        return c.done(nullptr);
    }
    UciEngine e;
    if (!started(c, e, cfg)) return c.done(&e);
    e.logSince(c.cursor);
    e.setOption("Ponder", "true");
    waitReadyok(e, 5000);
    // ponderhit: the ponder search continues as a normal timed search
    Position p = replay(Position::startpos().toFEN(), {"e2e4", "e7e5"});
    e.setPosition(Position::startpos().toFEN(), {"e2e4", "e7e5"});
    e.sendRaw("go ponder wtime 3000 btime 3000 winc 0 binc 0");
    auto t0 = clk::now();
    bool early = false;
    while (msSince(t0) < 300 && e.alive()) if (e.readLine(20).rfind("bestmove", 0) == 0) early = true;
    if (early) c.fail("bestmove during 'go ponder' before ponderhit or stop.");
    e.sendRaw("ponderhit");
    long long ms = 0;
    std::string line = readBestmove(e, 8000, &ms);
    std::string why = judgeMove(p, line);
    if (!why.empty()) c.fail("After ponderhit: " + why);
    else if (ms > 3000) c.fail("After ponderhit the engine took " + std::to_string(ms) + " ms with 3 s on its clock.");
    else c.note("ponderhit -> " + moveOf(line) + " after " + std::to_string(ms) + " ms.");
    trailingOutput(e, 150);
    // ponder miss: stop must end the ponder search with a bestmove
    e.setPosition(Position::startpos().toFEN(), {"d2d4", "d7d5"});
    e.sendRaw("go ponder wtime 3000 btime 3000 winc 0 binc 0");
    t0 = clk::now();
    while (msSince(t0) < 200 && e.alive()) e.readLine(20);
    e.sendRaw("stop");
    line = readBestmove(e, 3000, &ms);
    if (line.empty()) c.fail("No bestmove within 3 s after stop during a ponder search.");
    else c.note("stop during ponder -> bestmove after " + std::to_string(ms) + " ms.");
    return c.done(&e);
}

CheckResult checkQuit(UciEngine& e){
    Check c("quit", "quit -> the process exits within 1 s");
    e.logSince(c.cursor);
    e.sendRaw("quit");
    auto t0 = clk::now();
    while (msSince(t0) < 3000 && e.alive()) e.readLine(20);
    long long ms = msSince(t0);
    if (e.alive()) c.fail("Still running 3 s after quit.");
    else if (ms > 1000) c.fail("Exited " + std::to_string(ms) + " ms after quit (limit 1000 ms).");
    else c.note("Exited after " + std::to_string(ms) + " ms.");
    return c.done(&e);
}

} // namespace

const std::vector<std::string>& uciCheckIds(){
    static const std::vector<std::string> ids = {
        "handshake", "isready-idle", "ucinewgame", "quit", "isready-during-search", "stop", "go-modes",
        "positions", "no-legal-moves", "mate-scores", "setoption", "ponder", "time-management"};
    return ids;
}

std::vector<CheckResult> runUciChecks(const EngineConfig& cfg, const UciCheckOptions& opt){
    std::vector<CheckResult> out;
    auto add = [&](CheckResult r){
        out.push_back(r);
        if (opt.progress) opt.progress(out.back());
    };
    auto want = [&](const char* id){
        if (opt.abort && *opt.abort) return false;
        return opt.only.empty() || std::find(opt.only.begin(), opt.only.end(), id) != opt.only.end();
    };
    UciEngine e;
    bool ok = false;
    add(checkHandshake(cfg, e, ok));
    if (!ok) return out;                                  // nothing else can run
    bool hasPonder = e.hasOption("Ponder");
    if (want("isready-idle")) add(checkIsreadyIdle(e));
    if (want("ucinewgame")) add(checkNewGame(e));
    if (want("quit")) add(checkQuit(e));
    else e.quit();
    if (want("isready-during-search")) add(checkIsreadyDuringSearch(cfg));
    if (want("stop")) add(checkStop(cfg));
    if (want("go-modes")) add(checkGoModes(cfg));
    if (want("positions")) add(checkPositions(cfg));
    if (want("no-legal-moves")) add(checkNoLegalMoves(cfg));
    if (want("mate-scores")) add(checkMateScores(cfg));
    if (want("setoption")) add(checkSetoption(cfg));
    if (want("ponder")) add(checkPonder(cfg, hasPonder));
    if (want("time-management")) add(checkTimeManagement(cfg, opt.quick));
    return out;
}

CheckSummary summarize(const std::vector<CheckResult>& r){
    CheckSummary s;
    for (auto& x : r){
        if (x.status == CheckStatus::Pass) s.pass++;
        else if (x.status == CheckStatus::Warn) s.warn++;
        else if (x.status == CheckStatus::Fail) s.fail++;
        else s.skip++;
    }
    return s;
}

std::string checkReportText(const std::string& name, const std::vector<CheckResult>& r){
    std::ostringstream o;
    CheckSummary s = summarize(r);
    o << "UCI compliance report: " << name << "\n"
      << s.pass << " passed, " << s.warn << " warnings, " << s.fail << " failed, " << s.skip << " skipped\n\n";
    for (auto& x : r){
        o << "[" << checkStatusName(x.status) << "] " << x.title << "  (" << x.ms << " ms)\n";
        std::istringstream d(x.detail); std::string line;
        while (std::getline(d, line)) o << "    " << line << "\n";
        if (x.status == CheckStatus::Fail || x.status == CheckStatus::Warn){
            o << "    log (last lines):\n";
            size_t from = x.log.size() > 25 ? x.log.size() - 25 : 0;
            for (size_t i = from; i < x.log.size(); i++)
                o << "      " << x.log[i].tMs << " " << x.log[i].dir << " " << x.log[i].text << "\n";
        }
        o << "\n";
    }
    return o.str();
}
