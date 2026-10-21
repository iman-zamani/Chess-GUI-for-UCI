/* Chess-GUI-for-UCI — a game with variations, comments and NAGs. GPL-2.0 */
#include "game_tree.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

void GameTree::reset(const Position& start){
    nodes_.clear();
    GameNode root;
    root.pos = start;
    nodes_.push_back(root);
    result = "*";
}

int GameTree::add(int parent, const Move& m){
    const Position& p = nodes_[parent].pos;
    std::string uci = p.moveToUCI(m);
    for (int c : nodes_[parent].children)
        if (nodes_[c].alive && p.moveToUCI(nodes_[c].move) == uci) return c;
    GameNode n;
    n.parent = parent;
    n.move = m;
    n.san = p.moveToSAN(m);
    n.pos = p;
    n.pos.makeMove(m);
    nodes_.push_back(n);
    int id = (int)nodes_.size() - 1;
    nodes_[parent].children.push_back(id);
    return id;
}

std::vector<int> GameTree::path(int id) const{
    std::vector<int> out;
    for (int n = id; n > 0; n = nodes_[n].parent) out.push_back(n);
    std::reverse(out.begin(), out.end());
    return out;
}

int GameTree::lineEnd(int id) const{
    while (!nodes_[id].children.empty()) id = nodes_[id].children[0];
    return id;
}

std::vector<int> GameTree::mainLine() const{ return path(mainLineEnd()); }
int GameTree::mainLineEnd() const{ return lineEnd(0); }

bool GameTree::onMainLine(int id) const{
    for (int n = id; n > 0; n = nodes_[n].parent){
        const auto& sib = nodes_[nodes_[n].parent].children;
        if (sib.empty() || sib[0] != n) return false;
    }
    return true;
}

int GameTree::depth(int id) const{
    int d = 0;
    for (int n = id; n > 0; n = nodes_[n].parent) d++;
    return d;
}

void GameTree::promote(int id){
    for (int n = id; n > 0; n = nodes_[n].parent){
        auto& sib = nodes_[nodes_[n].parent].children;
        auto it = std::find(sib.begin(), sib.end(), n);
        if (it != sib.end() && it != sib.begin()) std::rotate(sib.begin(), it, it + 1);
    }
}

void GameTree::remove(int id){
    if (id <= 0) return;
    auto& sib = nodes_[nodes_[id].parent].children;
    sib.erase(std::remove(sib.begin(), sib.end(), id), sib.end());
    std::vector<int> stack{id};
    while (!stack.empty()){
        int n = stack.back(); stack.pop_back();
        nodes_[n].alive = false;
        for (int c : nodes_[n].children) stack.push_back(c);
        nodes_[n].children.clear();
    }
}

Game GameTree::lineGame(int id) const{
    Game g;
    g.reset(start());
    g.whiteName = white; g.blackName = black;
    for (int n : path(id)) if (!g.tryMove(nodes_[n].move)) break;
    return g;
}

// ---------------------------------------------------------------- PGN writing
static std::string nagText(int n){ return "$" + std::to_string(n); }

static std::string evalText(const GameNode& n){
    if (!n.hasEval) return "";
    char b[48];
    if (n.evalMate) std::snprintf(b, sizeof b, "[%%eval #%d]", n.mateIn);
    else std::snprintf(b, sizeof b, "[%%eval %.2f]", n.evalCp / 100.0);
    return b;
}

static std::string clockText(long long ms){
    long long s = ms / 1000;
    char b[48];
    std::snprintf(b, sizeof b, "[%%clk %lld:%02lld:%02lld.%lld]", s / 3600, s / 60 % 60, s % 60, ms % 1000 / 100);
    return b;
}

namespace {
struct Writer {
    std::string out, line;
    void put(const std::string& tok){
        if (tok.empty()) return;
        if (!line.empty() && line.size() + 1 + tok.size() > 79){ out += line + "\n"; line.clear(); }
        if (!line.empty() && line.back() != '(' && tok != ")") line += ' ';
        line += tok;
    }
    std::string done(){ if (!line.empty()) out += line + "\n"; return out; }
};
}

static std::string commentOf(const GameNode& n){
    std::string c;
    std::string e = evalText(n), k = n.clockMs >= 0 ? clockText(n.clockMs) : "";
    for (const std::string& part : {e, k, n.comment})
        if (!part.empty()){ if (!c.empty()) c += ' '; c += part; }
    return c;
}

static void emitMove(Writer& w, const GameTree& t, int id, bool forceNumber){
    const GameNode& n = t.at(id);
    const Position& before = t.at(n.parent).pos;
    if (before.whiteToMove) w.put(std::to_string(before.fullmoveNumber) + ".");
    else if (forceNumber) w.put(std::to_string(before.fullmoveNumber) + "...");
    w.put(n.san);
    for (int g : n.nags) w.put(nagText(g));
    std::string c = commentOf(n);
    if (!c.empty()) w.put("{" + c + "}");
}

// The line continuing from `parent`, with the alternatives to each move in brackets.
static void emitLine(Writer& w, const GameTree& t, int parent, bool forceNumber){
    int p = parent;
    bool force = forceNumber;
    while (!t.at(p).children.empty()){
        const auto& kids = t.at(p).children;
        int main = kids[0];
        emitMove(w, t, main, force);
        force = !commentOf(t.at(main)).empty();
        for (size_t i = 1; i < kids.size(); i++){
            w.put("(");
            emitMove(w, t, kids[i], true);
            emitLine(w, t, kids[i], !commentOf(t.at(kids[i])).empty());
            w.put(")");
            force = true;
        }
        p = main;
    }
}

std::string GameTree::toPGN() const{
    std::ostringstream o;
    auto tag = [&](const std::string& k, const std::string& v){
        std::string e;
        for (char c : v){ if (c == '"' || c == '\\') e += '\\'; e += c; }
        o << "[" << k << " \"" << e << "\"]\n";
    };
    auto value = [&](const char* k, const char* def){ auto it = tags.find(k); return it != tags.end() ? it->second : std::string(def); };
    tag("Event", value("Event", "Chess-GUI-for-UCI"));
    tag("Site", value("Site", "?"));
    tag("Date", value("Date", "????.??.??"));
    tag("Round", value("Round", "?"));
    tag("White", white);
    tag("Black", black);
    tag("Result", result);
    std::string sf = start().toFEN();
    if (start().chess960) tag("Variant", "Chess960");
    if (sf != Position::startpos().toFEN() || start().chess960){ tag("SetUp", "1"); tag("FEN", sf); }
    static const char* skip[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result", "SetUp", "FEN", "Variant"};
    for (auto& kv : tags){
        const std::string& k = kv.first;
        if (std::find_if(std::begin(skip), std::end(skip), [&](const char* x){ return k == x; }) == std::end(skip))
            tag(k, kv.second);
    }
    o << "\n";
    Writer w;
    if (!nodes_[0].comment.empty()) w.put("{" + nodes_[0].comment + "}");
    emitLine(w, *this, 0, !nodes_[0].comment.empty());
    w.put(result);
    o << w.done();
    return o.str();
}

// ---------------------------------------------------------------- PGN reading
// Pulls [%eval ...] and [%clk ...] out of a comment into the node.
static void absorbComment(GameNode& n, std::string c){
    auto take = [&](const std::string& cmd) -> std::string {
        size_t a = c.find("[%" + cmd + " ");
        if (a == std::string::npos) return "";
        size_t b = c.find(']', a);
        if (b == std::string::npos) return "";
        std::string arg = c.substr(a + cmd.size() + 3, b - a - cmd.size() - 3);
        c.erase(a, b - a + 1);
        return arg;
    };
    std::string e = take("eval");
    if (!e.empty()){
        if (e[0] == '#'){ n.hasEval = n.evalMate = true; n.mateIn = std::atoi(e.c_str() + 1); }
        else { char* end = nullptr; double v = std::strtod(e.c_str(), &end);
               if (end != e.c_str()){ n.hasEval = true; n.evalMate = false; n.evalCp = (int)std::lround(v * 100); } }
    }
    std::string k = take("clk");
    if (!k.empty()){
        int h = 0, m = 0; double s = 0;
        if (std::sscanf(k.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) n.clockMs = (long long)std::llround(((h * 60.0 + m) * 60.0 + s) * 1000);
    }
    // tidy what is left
    std::string t;
    bool space = false;
    for (char ch : c){
        if (std::isspace((unsigned char)ch)){ space = !t.empty(); continue; }
        if (space){ t += ' '; space = false; }
        t += ch;
    }
    if (!t.empty()) n.comment = n.comment.empty() ? t : n.comment + " " + t;
}

static int suffixNag(const std::string& s){
    if (s == "!") return 1; if (s == "?") return 2; if (s == "!!") return 3;
    if (s == "??") return 4; if (s == "!?") return 5; if (s == "?!") return 6;
    return 0;
}

bool gameTreeFromPGN(const PgnGame& pg, GameTree& out, std::string* err){
    Position start = Position::startpos();
    if (!pg.fenTag.empty()){
        bool ok = false;
        start = Position::fromFEN(pg.fenTag, &ok);
        if (!ok){ if (err) *err = "invalid FEN tag"; out.reset(Position::startpos()); return false; }
    }
    std::string v = pg.variant;
    for (auto& c : v) c = (char)std::tolower((unsigned char)c);
    if (v.find("960") != std::string::npos || v.find("fischer") != std::string::npos) start.chess960 = true;
    out.reset(start);
    out.white = pg.white; out.black = pg.black; out.result = pg.result.empty() ? "*" : pg.result;
    for (auto& [k, val] : pg.tags) out.tags[k] = val;
    const std::string& s = pg.movetext;
    size_t i = 0, n = s.size();
    int cur = 0, prev = 0;                 // node after / before the last move of the current line
    std::vector<std::pair<int, int>> stack;
    while (i < n){
        char c = s[i];
        if (std::isspace((unsigned char)c)){ i++; continue; }
        if (c == '{'){
            size_t e = s.find('}', i);
            std::string txt = s.substr(i + 1, e == std::string::npos ? std::string::npos : e - i - 1);
            absorbComment(out.at(cur), txt);
            i = e == std::string::npos ? n : e + 1;
            continue;
        }
        if (c == ';'){ size_t e = s.find('\n', i); absorbComment(out.at(cur), s.substr(i + 1, e - i - 1)); i = e == std::string::npos ? n : e; continue; }
        if (c == '('){ stack.push_back({cur, prev}); cur = prev; i++; continue; }
        if (c == ')'){ if (!stack.empty()){ cur = stack.back().first; prev = stack.back().second; stack.pop_back(); } i++; continue; }
        if (c == '$'){
            size_t j = i + 1; while (j < n && std::isdigit((unsigned char)s[j])) j++;
            if (cur > 0) out.at(cur).nags.push_back(std::atoi(s.substr(i + 1, j - i - 1).c_str()));
            i = j; continue;
        }
        size_t j = i;
        while (j < n && !std::isspace((unsigned char)s[j]) && s[j] != '{' && s[j] != '(' && s[j] != ')' && s[j] != ';' && s[j] != '$') j++;
        std::string tok = s.substr(i, j - i);
        i = j;
        if (tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*") continue;
        // "12." "12..." or "12.e4"
        size_t d = 0; while (d < tok.size() && std::isdigit((unsigned char)tok[d])) d++;
        if (d > 0 && d < tok.size() && tok[d] == '.'){ while (d < tok.size() && tok[d] == '.') d++; tok = tok.substr(d); }
        else if (d == tok.size()) continue;
        if (tok.empty()) continue;
        // annotation suffix
        std::string suf;
        while (!tok.empty() && (tok.back() == '!' || tok.back() == '?')){ suf.insert(suf.begin(), tok.back()); tok.pop_back(); }
        if (tok.empty()){ if (int g = suffixNag(suf); g && cur > 0) out.at(cur).nags.push_back(g); continue; }
        Move m;
        std::string san = tok;
        if (san == "0-0") san = "O-O"; else if (san == "0-0-0") san = "O-O-O";
        if (!out.at(cur).pos.sanToMove(san, m)){
            if (err) *err = "illegal or unreadable move '" + tok + "'";
            return false;
        }
        prev = cur;
        cur = out.add(cur, m);
        if (int g = suffixNag(suf)) out.at(cur).nags.push_back(g);
    }
    return true;
}
