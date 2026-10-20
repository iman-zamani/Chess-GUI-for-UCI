/* Chess-GUI-for-UCI — perft divide comparison and bisection. GPL-2.0 */
#include "perft_bisect.hpp"
#include <cctype>
#include <chrono>
#include <sstream>

static bool isUciMove(const std::string& s){
    if (s.size() != 4 && s.size() != 5) return false;
    if (nameToSquare(s.substr(0, 2)) < 0 || nameToSquare(s.substr(2, 2)) < 0) return false;
    if (s.size() == 5 && std::string("qrbn").find((char)std::tolower((unsigned char)s[4])) == std::string::npos) return false;
    return true;
}

static bool parseCount(const std::string& tok, uint64_t& out){
    if (tok.empty()) return false;
    uint64_t v = 0;
    for (char c : tok){
        if (c == ',' || c == '\'') continue;              // thousands separators
        if (!std::isdigit((unsigned char)c)) return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    out = v;
    return true;
}

bool parseDivideLine(const std::string& lineIn, std::string& move, uint64_t& count){
    std::string line = lineIn;
    for (char& c : line) if (c == ':' || c == '\t' || c == '=') c = ' ';
    std::istringstream ss(line);
    std::vector<std::string> t;
    std::string w;
    while (ss >> w) t.push_back(w);
    if (t.empty()) return false;
    // "Move e2e4 Count 20"
    if (t.size() >= 4 && (t[0] == "Move" || t[0] == "move") && isUciMove(t[1])){
        for (size_t i = 2; i + 1 < t.size(); i++)
            if ((t[i] == "Count" || t[i] == "count" || t[i] == "Nodes" || t[i] == "nodes") && parseCount(t[i + 1], count)){
                move = t[1]; return true;
            }
        return false;
    }
    // "e2e4 20", "e2e4 - 20", "e2e4 20 nodes"
    if (!isUciMove(t[0]) || t.size() < 2) return false;
    size_t i = 1;
    if (t[i] == "-" && t.size() > 2) i++;
    if (!parseCount(t[i], count)) return false;
    move = t[0];
    for (char& c : move) c = (char)std::tolower((unsigned char)c);
    return true;
}

bool parseTotalLine(const std::string& line, long long& total){
    static const char* keys[] = {"Nodes searched:", "Total nodes:", "Nodes:", "Total:", "nodes searched:", "total:"};
    for (const char* k : keys){
        size_t p = line.find(k);
        if (p == std::string::npos) continue;
        std::istringstream ss(line.substr(p + std::string(k).size()));
        std::string tok; ss >> tok;
        uint64_t v;
        if (parseCount(tok, v)){ total = (long long)v; return true; }
    }
    return false;
}

// King-takes-rook castling (Chess960 notation) -> standard notation.
static std::string normaliseCastle(const Position& p, const std::string& m){
    if (m.size() != 4) return m;
    int from = nameToSquare(m.substr(0, 2)), to = nameToSquare(m.substr(2, 2));
    if (from < 0 || to < 0) return m;
    int king = p.whiteToMove ? WHITE_KING : BLACK_KING, rook = p.whiteToMove ? WHITE_ROOK : BLACK_ROOK;
    if (p.board[from] != king || p.board[to] != rook || from / 8 != to / 8) return m;
    if (from % 8 != 4) return m;
    if (to % 8 == 7) return m.substr(0, 2) + squareName(from + 2);
    if (to % 8 == 0) return m.substr(0, 2) + squareName(from - 2);
    return m;
}

PerftDivide enginePerftDivide(UciEngine& e, const std::string& fen, int depth, int timeoutMs){
    PerftDivide d;
    bool okFen = false;
    Position p = Position::fromFEN(fen, &okFen);
    e.setPosition(fen, {});
    e.sendRaw("go perft " + std::to_string(depth));
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&](){ return std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0).count(); };
    bool quietAfterTotal = false;
    long long lastDivideAt = -1;
    while (elapsed() < timeoutMs && e.alive()){
        std::string ln = e.readLine(quietAfterTotal ? 150 : 200);
        if (ln.empty()){
            if (quietAfterTotal) break;
            // a divide without a total line: finished once the output stops for 2 s
            if (lastDivideAt >= 0 && elapsed() - lastDivideAt > 2000) break;
            continue;
        }
        std::string mv; uint64_t n;
        long long tot;
        if (parseDivideLine(ln, mv, n)){
            if (okFen) mv = normaliseCastle(p, mv);
            d.moves[mv] = n;
            lastDivideAt = elapsed();
        } else if (parseTotalLine(ln, tot)){
            d.total = tot;
            quietAfterTotal = true;                 // some engines print more after the total
        } else if (ln.rfind("bestmove", 0) == 0){
            d.error = "the engine ran a search instead of perft (no 'go perft' support?)"; break;
        } else if (ln.find("nknown") != std::string::npos){
            d.error = "the engine does not support 'go perft': " + ln; break;
        }
    }
    e.isReady(5000);
    if (d.error.empty()){
        if (!e.alive()) d.error = "the engine exited during perft";
        else if (d.moves.empty() && d.total < 0) d.error = "no perft output within " + std::to_string(timeoutMs / 1000) + " s";
    }
    d.ok = d.error.empty() && !d.moves.empty();
    if (d.error.empty() && d.moves.empty()) d.error = "the engine printed a total but no per-move counts (divide)";
    return d;
}

PerftDivide internalPerftDivide(const Position& p, int depth){
    PerftDivide d;
    std::vector<Move> legal; p.generateLegal(legal);
    uint64_t total = 0;
    for (const Move& m : legal){
        Position q = p; q.makeMove(m);
        uint64_t n = depth <= 1 ? 1 : q.perft(depth - 1);
        d.moves[p.moveToUCI(m)] = n;
        total += n;
    }
    d.total = (long long)total;
    d.ok = true;
    return d;
}

DivideDiff compareDivide(const PerftDivide& eng, const PerftDivide& truth){
    DivideDiff r;
    for (auto& kv : truth.moves){
        auto it = eng.moves.find(kv.first);
        if (it == eng.moves.end()) r.missing.push_back(kv.first);
        else if (it->second != kv.second) r.wrongCount.push_back(kv.first);
    }
    for (auto& kv : eng.moves) if (!truth.moves.count(kv.first)) r.extra.push_back(kv.first);
    return r;
}

static std::string join(const std::vector<std::string>& v){
    std::string s;
    for (auto& x : v){ if (!s.empty()) s += ", "; s += x; }
    return s;
}

BisectReport perftBisect(UciEngine& e, const std::string& startFen, int depth,
                         std::function<void(const BisectStep&)> progress, std::atomic<bool>& abortFlag){
    BisectReport rep;
    bool ok = false;
    Position pos = Position::fromFEN(startFen, &ok);
    if (!ok){ rep.error = true; rep.errorText = "invalid FEN"; rep.summary = rep.errorText; return rep; }
    std::vector<std::string> path;
    while (depth >= 1 && !abortFlag){
        BisectStep st;
        st.fen = pos.toFEN(); st.depth = depth;
        PerftDivide truth = internalPerftDivide(pos, depth);
        PerftDivide eng = enginePerftDivide(e, st.fen, depth);
        st.expected = (uint64_t)truth.total;
        st.engineNodes = eng.total;
        if (!eng.ok){
            rep.error = true; rep.errorText = eng.error;
            rep.summary = "Perft failed at " + st.fen + " depth " + std::to_string(depth) + ": " + eng.error;
            rep.steps.push_back(st);
            if (progress) progress(st);
            return rep;
        }
        st.diff = compareDivide(eng, truth);
        bool setWrong = !st.diff.missing.empty() || !st.diff.extra.empty();
        if (!setWrong && st.diff.wrongCount.empty()){
            rep.steps.push_back(st);
            if (progress) progress(st);
            if (rep.steps.size() == 1){
                rep.engineAgrees = true;
                rep.summary = "The engine agrees with the reference at depth " + std::to_string(depth)
                            + " (" + std::to_string(st.expected) + " nodes).";
            } else {
                // the totals differed one level up but every child agrees: the engine's
                // own sum is wrong (or nondeterministic)
                rep.found = true;
                rep.bugFen = st.fen;
                rep.summary = "Every move agrees here, but the parent's count was wrong: the engine adds up "
                              "its per-move counts incorrectly or its perft is not deterministic.";
            }
            return rep;
        }
        if (setWrong){
            rep.steps.push_back(st);
            if (progress) progress(st);
            rep.found = true;
            rep.bugFen = st.fen;
            rep.missing = st.diff.missing;
            rep.extra = st.diff.extra;
            std::string s = "Move generator bug in " + st.fen + ":";
            if (!rep.missing.empty()) s += " missing legal move(s) " + join(rep.missing) + ".";
            if (!rep.extra.empty()) s += " illegal move(s) generated " + join(rep.extra) + ".";
            if (!path.empty()) s += " Reached by " + join(path) + " from the start position.";
            rep.summary = s;
            return rep;
        }
        if (depth == 1){                             // a leaf counts as one node
            rep.steps.push_back(st);
            if (progress) progress(st);
            rep.found = true; rep.bugFen = st.fen;
            rep.summary = "At depth 1 the engine reports more than one node for " + join(st.diff.wrongCount)
                        + " in " + st.fen + ": its perft counts leaves incorrectly.";
            return rep;
        }
        st.chosenMove = st.diff.wrongCount.front();
        rep.steps.push_back(st);
        if (progress) progress(st);
        Move m;
        if (!pos.uciToMove(st.chosenMove, m)){ rep.error = true; rep.errorText = "internal error"; return rep; }
        pos.makeMove(m);
        path.push_back(st.chosenMove);
        depth--;
    }
    if (abortFlag){ rep.error = true; rep.errorText = "aborted"; rep.summary = "Aborted."; }
    return rep;
}
