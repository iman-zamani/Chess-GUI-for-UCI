/* Chess-GUI-for-UCI — opening books for engine matches. GPL-2.0 */
#include "openings.hpp"
#include <algorithm>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

Position Opening::finalPosition() const{
    Position p = Position::fromFEN(fen);
    for (auto& u : moves){ Move m; if (!p.uciToMove(u, m)) break; p.makeMove(m); }
    return p;
}

static std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

BookLoadResult parseEpdBook(const std::string& text){
    BookLoadResult r;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)){
        lineNo++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string f[6];
        int n = 0;
        while (n < 4 && ss >> f[n]) n++;
        if (n < 4){ r.skipped++; continue; }
        std::string rest; std::getline(ss, rest); rest = trim(rest);
        std::string hm = "0", fm = "1", label = "line " + std::to_string(lineNo);
        // a 6-field FEN line: the next two tokens are the counters
        {
            std::istringstream rs(rest);
            std::string a, b;
            if (rs >> a >> b && std::all_of(a.begin(), a.end(), ::isdigit) && std::all_of(b.begin(), b.end(), ::isdigit)){
                hm = a; fm = b;
                std::getline(rs, rest); rest = trim(rest);
            }
        }
        // EPD operations: "opcode operand...;" e.g. id "x"; hmvc 3; fmvn 12;
        std::istringstream ops(rest);
        std::string op;
        while (std::getline(ops, op, ';')){
            op = trim(op);
            if (op.empty()) continue;
            size_t sp = op.find(' ');
            std::string code = op.substr(0, sp), arg = sp == std::string::npos ? "" : trim(op.substr(sp + 1));
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') arg = arg.substr(1, arg.size() - 2);
            if (code == "hmvc") hm = arg;
            else if (code == "fmvn") fm = arg;
            else if (code == "id") label = arg;
        }
        std::string fen = f[0] + " " + f[1] + " " + f[2] + " " + f[3] + " " + hm + " " + fm;
        bool ok = false;
        Position p = Position::fromFEN(fen, &ok);
        if (!ok || !enginePositionProblem(p).empty()){ r.skipped++; continue; }
        std::vector<Move> legal; p.generateLegal(legal);
        if (legal.empty()){ r.skipped++; continue; }
        r.openings.push_back({p.toFEN(), {}, label});
    }
    if (r.openings.empty() && r.error.empty()) r.error = "no usable positions";
    return r;
}

BookLoadResult parsePgnBook(const std::string& text, int plies){
    BookLoadResult r;
    auto games = parsePGN(text);
    int idx = 0;
    for (auto& pg : games){
        idx++;
        Game g; std::string err;
        gameFromPGN(pg, g, &err);                 // a broken tail still gives a usable prefix
        Position p = g.start;
        if (!enginePositionProblem(p).empty()){ r.skipped++; continue; }
        Opening o;
        o.fen = p.toFEN();
        o.label = "game " + std::to_string(idx);
        int n = plies > 0 ? std::min<int>(plies, (int)g.moves.size()) : (int)g.moves.size();
        for (int i = 0; i < n; i++){ o.moves.push_back(p.moveToUCI(g.moves[i])); p.makeMove(g.moves[i]); }
        std::vector<Move> legal; p.generateLegal(legal);
        if (legal.empty()){ r.skipped++; continue; }
        r.openings.push_back(o);
    }
    if (r.openings.empty()) r.error = games.empty() ? "no games found" : "no usable games";
    return r;
}

BookLoadResult loadOpeningBook(const std::string& path, BookFormat fmt, int plies){
    std::ifstream f(path, std::ios::binary);
    if (!f){ BookLoadResult r; r.error = "cannot open " + path; return r; }
    std::stringstream ss; ss << f.rdbuf();
    if (fmt == BookFormat::Auto){
        std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        fmt = ext == ".pgn" ? BookFormat::Pgn : BookFormat::Epd;
    }
    return fmt == BookFormat::Pgn ? parsePgnBook(ss.str(), plies) : parseEpdBook(ss.str());
}

OpeningSchedule::OpeningSchedule(size_t n, bool random, uint64_t seed) : n_(std::max<size_t>(n, 1)), random_(random), seed_(seed) {}

size_t OpeningSchedule::forPair(size_t k) const{
    if (!random_) return k % n_;
    // each pass over the book is its own permutation, derived from (seed, pass)
    size_t pass = k / n_, pos = k % n_;
    if (pass != cachedPass_){
        perm_.resize(n_);
        std::iota(perm_.begin(), perm_.end(), 0);
        std::mt19937_64 rng(seed_ * 1000003ULL + pass);
        for (size_t i = n_ - 1; i > 0; i--){                 // Fisher-Yates
            std::uniform_int_distribution<size_t> d(0, i);
            std::swap(perm_[i], perm_[d(rng)]);
        }
        cachedPass_ = pass;
    }
    return perm_[pos];
}
