/* Chess-GUI-for-UCI — Elo from games against a strength-limited reference engine. GPL-2.0 */
#include "elo_ladder.hpp"
#include "uci_engine.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A fit this far beyond the strongest (weakest) level played is reported as a bound:
// the levels say little about ratings much further away (expected score above 85%).
constexpr double kMaxExtrapolation = 300;
// Beyond this, the estimate rests mostly on the curve's shape rather than on games near
// it (expected score above 64% against the strongest level): flagged as extrapolated.
constexpr double kExtrapolationNote = 100;
// Pseudo-pairs with the model's variance added to the observed pair scores, so that
// a few identical pairs (all draws, say) can't make the interval vanish.
constexpr double kPriorPairs = 4;
// The 97.5% quantile of Student's t with `dof` degrees of freedom (the 95% two-sided
// multiplier): a table up to 9, Hill's series beyond (error below 2e-4 from 10).
double tQuantile975(double dof){
    static const double small[] = {12.7062, 4.3027, 3.1824, 2.7764, 2.5706, 2.4469, 2.3646, 2.3060, 2.2622};
    if (dof < 1) return small[0];
    if (dof < 10) return small[(int)dof - 1];
    const double z = 1.959963984540054, z2 = z * z, n = dof;
    return z + z * (z2 + 1) / (4 * n) + z * ((5 * z2 + 16) * z2 + 3) / (96 * n * n)
         + z * (((3 * z2 + 19) * z2 + 17) * z2 - 15) / (384 * n * n * n);
}

int round10(double x){ return (int)std::lround(x / 10.0) * 10; }

int clampLevel(double x, int lo, int hi){
    return std::max(lo, std::min(hi, round10(x)));
}

std::vector<int> sortedUnique(std::vector<int> v){
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// Bisection for the root of a monotone function on [lo, hi] (f(lo) and f(hi) differ in sign).
template<class F> double bisect(F f, double lo, double hi){
    double flo = f(lo);
    for (int i = 0; i < 200 && hi - lo > 1e-7; i++){
        double mid = 0.5 * (lo + hi), fm = f(mid);
        if ((fm > 0) == (flo > 0)){ lo = mid; flo = fm; } else hi = mid;
    }
    return 0.5 * (lo + hi);
}

// Upper tail of the chi-square distribution (Wilson-Hilferty approximation).
double chiSquareP(double x, int dof){
    if (dof <= 0) return 1;
    if (x <= 0) return 1;
    double k = dof, v = 2.0 / (9.0 * k);
    double z = (std::cbrt(x / k) - (1 - v)) / std::sqrt(v);
    return 0.5 * std::erfc(z / std::sqrt(2.0));
}

bool equalsNoCase(const std::string& a, const std::string& b){
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

std::string fmtElo(double x){ return std::to_string((long)std::lround(x)); }

void addLevel(LadderLevel& to, const LadderLevel& from){
    to.wdl.wins += from.wdl.wins; to.wdl.draws += from.wdl.draws; to.wdl.losses += from.wdl.losses;
    PentaCounts& p = to.penta; const PentaCounts& q = from.penta;
    p.ll += q.ll; p.ld += q.ld; p.wl += q.wl; p.dd += q.dd; p.wd += q.wd; p.ww += q.ww;
    to.timeLosses += from.timeLosses; to.crashes += from.crashes;
    to.refMoves += from.refMoves; to.refShallowMoves += from.refShallowMoves;
}

// What identifies a program's build: its size and modification time ("" if it can't be read).
std::string binaryId(const std::string& path){
    std::error_code ec;
    fs::path p = fs::u8path(path);
    auto size = fs::file_size(p, ec);
    if (ec) return "";
    auto t = fs::last_write_time(p, ec);
    if (ec) return "";
    return std::to_string(size) + "@" + std::to_string((long long)t.time_since_epoch().count());
}

// Games recorded in a match state file (0 if there is none or it can't be read).
int stateGames(const std::string& path){
    std::ifstream f(fs::u8path(path));
    if (!f) return 0;
    try { json j = json::parse(f); return (int)j.value("games", json::array()).size(); }
    catch (...) { return 0; }
}

void removeStateFiles(const std::string& statePath){
    std::error_code ec;
    for (const char* x : {"", ".stage1", ".stage2"}) fs::remove(fs::u8path(statePath + x), ec);
}

std::string stageText(const LadderStage& st){
    std::string s;
    for (size_t i = 0; i < st.levels.size(); i++)
        s += (i ? ", " : "") + std::to_string(st.levels[i]) + " (" + std::to_string(st.games[i]) + " games)";
    return s;
}

} // namespace

// ---------------------------------------------------------------- the fit
double ladderExpectedScore(double elo, double levelElo){
    return 1.0 / (1.0 + std::pow(10.0, (levelElo - elo) / 400.0));
}

LadderFit fitLadder(const std::vector<LadderLevel>& all){
    LadderFit f;
    std::vector<const LadderLevel*> lv;
    double n = 0, score = 0;
    for (auto& l : all) if (l.wdl.games() > 0){ lv.push_back(&l); n += l.wdl.games(); score += l.score(); }
    f.games = (int)n;
    if (lv.empty()) return f;
    double minE = lv.front()->elo, maxE = lv.front()->elo;
    for (auto* l : lv){ minE = std::min(minE, (double)l->elo); maxE = std::max(maxE, (double)l->elo); }
    const double lo = minE - 3000, hi = maxE + 3000;

    // A perfect (or zero) score has no finite maximum: give the rating at which it would
    // happen with probability 5% (an exact one-sided 95% bound).
    if (score >= n || score <= 0){
        bool above = score >= n;
        auto g = [&](double r){
            double s = 0;
            for (auto* l : lv){
                double p = ladderExpectedScore(r, l->elo);
                s += l->wdl.games() * std::log(above ? p : 1 - p);
            }
            return s - std::log(0.05);
        };
        f.status = above ? LadderStatus::Above : LadderStatus::Below;
        f.bound = bisect(g, lo, hi);
        f.elo = f.bound;
        return f;
    }

    // Maximum likelihood: sum over levels of (score - n p) = 0, decreasing in the rating.
    f.elo = bisect([&](double r){
        double s = 0;
        for (auto* l : lv) s += l->score() - l->wdl.games() * ladderExpectedScore(r, l->elo);
        return s;
    }, lo, hi);

    // Interval: the model's information, scaled by the dispersion of the pair scores
    // around the fit (a quasi-likelihood / sandwich estimate). Pairs are the unit
    // because both games of a pair start from the same opening.
    const double k = std::log(10.0) / 400.0;
    double info = 0, resid = 0, modelVar = 0, pairs = 0;
    double withinResid = 0, withinModel = 0, withinDof = 0;   // around each level's own mean (for the fit check)
    for (auto* l : lv){
        double p = ladderExpectedScore(f.elo, l->elo);
        info += l->wdl.games() * p * (1 - p);
        const PentaCounts& q = l->penta;
        double m = q.pairs();
        if (m <= 0) continue;
        const double y[5] = {0, 0.5, 1, 1.5, 2};
        const double c[5] = {(double)q.ll, (double)q.ld, (double)(q.wl + q.dd), (double)q.wd, (double)q.ww};
        double mean = 0;
        for (int i = 0; i < 5; i++) mean += c[i] * y[i];
        mean /= m;
        for (int i = 0; i < 5; i++){
            resid += c[i] * (y[i] - 2 * p) * (y[i] - 2 * p);
            withinResid += c[i] * (y[i] - mean) * (y[i] - mean);
        }
        modelVar += m * 2 * p * (1 - p);
        withinModel += (m - 1) * 2 * p * (1 - p);
        withinDof += m - 1;
        pairs += m;
    }
    if (pairs > 0 && modelVar > 0){
        double per = modelVar / pairs;
        f.dispersion = (resid + kPriorPairs * per) / (modelVar + kPriorPairs * per);
    }
    // The dispersion is estimated from the pairs (plus the prior's), so the multiplier is
    // Student's t rather than 1.96: with few pairs and few draws the interval is wider.
    const double q = tQuantile975(pairs + kPriorPairs - 1);
    f.ci95 = info > 0 ? q * std::sqrt(f.dispersion / info) / k : 1e9;

    // Do the levels agree? Pearson chi-square of the level scores against the fit, with
    // the variance from the pair scores within each level.
    if (lv.size() >= 2){
        double per = withinDof > 0 && withinModel > 0 ? withinModel / withinDof : 0.5;
        double phi = (withinResid + kPriorPairs * per) / (withinModel + kPriorPairs * per);
        double chi = 0;
        for (auto* l : lv){
            double p = ladderExpectedScore(f.elo, l->elo), g = l->wdl.games();
            double v = phi * g * p * (1 - p);
            if (v > 0) chi += (l->score() - g * p) * (l->score() - g * p) / v;
        }
        f.fitP = chiSquareP(chi, (int)lv.size() - 1);
    }

    // Likelihood-ratio limits (the quasi-likelihood's deviance, scaled by the dispersion).
    // Inside the levels they match +/- ci95; past them the likelihood is lopsided and the
    // symmetric interval is not to be trusted, so these are used there.
    auto loglik = [&](double r){
        double s = 0;
        for (auto* l : lv){
            double p = ladderExpectedScore(r, l->elo), g = l->wdl.games();
            s += l->score() * std::log(p) + (g - l->score()) * std::log(1 - p);
        }
        return s;
    };
    const double top = loglik(f.elo);
    auto limit = [&](double chi, bool up){
        auto g = [&](double r){ return 2 * (top - loglik(r)) / f.dispersion - chi; };
        return up ? bisect(g, f.elo, hi) : bisect(g, lo, f.elo);
    };
    f.low95 = limit(q * q, false);
    f.high95 = limit(q * q, true);

    f.status = LadderStatus::Ok;
    // Far past the levels the number rests on the model's curve alone: give the near end of
    // the interval as a bound (in simulations of the automatic plan the bound and the interval
    // together held the true rating 94-97% of the time, also just inside this threshold).
    if (f.elo > maxE + kMaxExtrapolation){ f.status = LadderStatus::Above; f.bound = f.low95; }
    else if (f.elo < minE - kMaxExtrapolation){ f.status = LadderStatus::Below; f.bound = f.high95; }
    else f.extrapolated = f.elo > maxE + kExtrapolationNote || f.elo < minE - kExtrapolationNote;
    return f;
}

std::string ladderHeadline(const LadderFit& f){
    switch (f.status){
    case LadderStatus::Ok:
        if (f.extrapolated) return fmtElo(f.elo) + " (" + fmtElo(f.low95) + " to " + fmtElo(f.high95) + ")";
        return fmtElo(f.elo) + " +/- " + fmtElo(f.ci95);
    case LadderStatus::Above: return "above " + fmtElo(f.bound);
    case LadderStatus::Below: return "below " + fmtElo(f.bound);
    default: return "no games";
    }
}

std::string ladderPerformance(const LadderLevel& l){
    if (l.wdl.games() < kLadderMinPerformanceGames) return "";
    return ladderHeadline(fitLadder({l}));
}

// ---------------------------------------------------------------- levels
int LadderStage::total() const{
    int t = 0;
    for (int g : games) t += g;
    return t;
}

// `pairs` colour-swapped pairs over k levels: the same number each, and the rest one
// each to the middle levels. Returns games per level.
static std::vector<int> sharePairs(int pairs, int k){
    std::vector<int> g(k, 2 * (pairs / k));
    std::vector<int> order(k);
    for (int i = 0; i < k; i++) order[i] = i;
    double mid = (k - 1) / 2.0;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b){ return std::fabs(a - mid) < std::fabs(b - mid); });
    for (int i = 0; i < pairs % k; i++) g[order[i]] += 2;
    return g;
}

LadderStage ladderFirstStage(int games, int minElo, int maxElo){
    LadderStage st;
    for (int i = 0; i < kLadderSpread; i++)
        st.levels.push_back(clampLevel(minElo + (maxElo - minElo) * (double)i / (kLadderSpread - 1), minElo, maxElo));
    st.levels = sortedUnique(st.levels);
    int k = (int)st.levels.size();
    int perLevel = std::max(1, (int)std::floor(0.3 * games / (2.0 * k)));   // pairs
    st.games.assign(k, 2 * perLevel);
    return st;
}

LadderStage ladderSecondStage(const LadderFit& first, int gamesLeft, int minElo, int maxElo){
    LadderStage st;
    int pairs = std::max(0, gamesLeft) / 2;
    if (pairs == 0) return st;
    // Past the range: every game at the end level (the only one that still tells much).
    if (first.status == LadderStatus::Above || first.status == LadderStatus::Below){
        st.levels = {first.status == LadderStatus::Above ? maxElo : minElo};
        st.games = {2 * pairs};
        return st;
    }
    double c = first.status == LadderStatus::Ok ? first.elo : 0.5 * (minElo + maxElo);
    int centre = clampLevel(c, minElo, maxElo);
    st.levels = sortedUnique({clampLevel(centre - 200, minElo, maxElo), centre, clampLevel(centre + 200, minElo, maxElo)});
    if (pairs < (int)st.levels.size()) st.levels = {centre};
    int k = (int)st.levels.size();
    st.games.assign(k, 2 * (pairs / k));
    size_t ci = std::find(st.levels.begin(), st.levels.end(), centre) - st.levels.begin();
    st.games[ci] += 2 * (pairs % k);                 // the odd pairs to the centre level
    return st;
}

LadderStage ladderGivenStage(const std::vector<int>& levels, int games){
    LadderStage st;
    st.levels = sortedUnique(levels);
    int k = (int)st.levels.size();
    if (!k) return st;
    st.games = sharePairs(std::max(k, games / 2), k);
    return st;
}

int stockfish11PickDepth(int uciElo){
    // Stockfish 11, search.cpp (tag sf_11): the level from UCI_Elo, rounded down or up at
    // random; the move is picked when the iteration at depth 1 + level is done.
    double level = std::pow(std::max(0.0, (uciElo - 1346.6) / 143.4), 1 / 0.806);
    level = std::max(0.0, std::min(20.0, level));
    return 1 + (int)std::floor(level);
}

bool ladderIsStockfish11(const std::string& id){ return id.rfind("Stockfish 11", 0) == 0; }

bool parseLadderLevels(const std::string& text, std::vector<int>& out){
    std::vector<int> v;
    std::string t = text;
    for (char& ch : t) if (ch == ',' || ch == ';') ch = ' ';
    std::istringstream in(t);
    std::string tok;
    while (in >> tok){
        char* end = nullptr;
        long x = std::strtol(tok.c_str(), &end, 10);
        if (!end || *end || x <= 0 || x > 10000) return false;
        v.push_back((int)x);
    }
    if (v.empty()) return false;
    out = sortedUnique(v);
    return true;
}

std::string LadderSettings::validate() const{
    if (engine.path.empty()) return "choose the engine to rate";
    if (reference.path.empty())
        return "no reference engine: the bundled Stockfish 11 is not installed (Resources/reference); "
               "build it with tools/build_reference_engines.py or choose another engine with UCI_Elo";
    if (tc.baseMs <= 0 || tc.incMs < 0) return "the time control must have a base time";
    if (games < 2) return "play at least 2 games";
    if (levels.empty() && games < kLadderMinAutoGames)
        return "automatic levels need at least " + std::to_string(kLadderMinAutoGames) + " games";
    for (int l : levels) if (l <= 0) return "levels must be positive Elo values";
    if (concurrency < 1) return "play at least one game at a time";
    if (bookPlies < 0) return "book plies can't be negative";
    if (timeMarginMs < 0) return "the time margin can't be negative";
    return "";
}

// ---------------------------------------------------------------- the run
std::string defaultLadderReference(){
#ifdef _WIN32
    return findResource("reference/stockfish-11.exe");
#else
    return findResource("reference/stockfish-11");
#endif
}

std::string defaultLadderBook(){ return findResource("books/openings.pgn"); }

LadderResult runEloLadder(const LadderSettings& s, const LadderOptions& o){
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    LadderResult r;
    r.settings = s;
    auto log = [&](const std::string& line){ if (o.log) o.log(line); };
    auto stopped = [&]{ return o.abort && o.abort->load(); };
    auto finish = [&]{ r.seconds = std::chrono::duration<double>(clk::now() - t0).count(); return r; };

    r.error = s.validate();
    if (!r.error.empty()) return finish();

    // What the reference engine offers.
    {
        UciEngine ref;
        ref.setCancel(o.abort);
        if (!ref.start(s.reference)){
            r.error = stopped() ? "stopped" : "could not start the reference engine " + s.reference.name + ": " + ref.lastError();
            return finish();
        }
        r.referenceId = ref.name();
        const UciOption *limit = nullptr, *elo = nullptr;
        for (auto& op : ref.options()){
            if (equalsNoCase(op.name, "UCI_LimitStrength") && op.type == "check") limit = &op;
            if (equalsNoCase(op.name, "UCI_Elo") && op.type == "spin") elo = &op;
        }
        if (!limit || !elo || elo->minV >= elo->maxV){
            r.error = (r.referenceId.empty() ? s.reference.name : r.referenceId)
                    + " has no UCI_LimitStrength / UCI_Elo options: the reference must be an engine that can "
                      "limit its strength to a given Elo, such as Stockfish";
            return finish();
        }
        r.refMinElo = (int)elo->minV; r.refMaxElo = (int)elo->maxV;
        ref.quit();
    }
    {
        UciEngine eng;
        eng.setCancel(o.abort);
        if (!eng.start(s.engine)){
            r.error = stopped() ? "stopped" : "could not start " + s.engine.name + ": " + eng.lastError();
            return finish();
        }
        r.engineId = eng.name();
        eng.quit();
    }
    for (int l : s.levels)
        if (l < r.refMinElo || l > r.refMaxElo){
            r.error = "level " + std::to_string(l) + " is outside the reference engine's UCI_Elo range ("
                    + std::to_string(r.refMinElo) + " to " + std::to_string(r.refMaxElo) + ")";
            return finish();
        }

    // Resumable state: <statePath> records which builds of the engines played the games, so a
    // rebuilt engine starts a new run instead of mixing its games with the old build's.
    if (!s.statePath.empty()){
        std::string builds = binaryId(s.engine.path) + "|" + binaryId(s.reference.path);
        std::string before;
        {
            std::ifstream f(fs::u8path(s.statePath));
            try { if (f) before = json::parse(f).value("builds", ""); } catch (...) {}
        }
        if (!before.empty() && before != builds){
            removeStateFiles(s.statePath);
            std::string w = "the engine or the reference engine was rebuilt since the stopped run: it starts again";
            r.warnings.push_back(w);
            log(w);
        }
        std::ofstream f(fs::u8path(s.statePath));
        f << json{{"version", 1}, {"builds", builds}}.dump() << "\n";
        if (!f){ r.error = "cannot write the state file " + s.statePath; return finish(); }
    }

    std::string book = s.bookPath.empty() ? defaultLadderBook() : s.bookPath;
    if (book.empty()) r.warnings.push_back("no opening book: every game starts from the initial position");
    std::string refName = !s.reference.name.empty() ? s.reference.name
                        : !r.referenceId.empty() ? r.referenceId : std::string("reference");

    const bool automatic = s.levels.empty();
    LadderStage first = automatic ? ladderFirstStage(s.games, r.refMinElo, r.refMaxElo) : ladderGivenStage(s.levels, s.games);
    const int stages = automatic ? 2 : 1;
    int planned = automatic ? s.games / 2 * 2 : first.total();
    const bool sf11 = ladderIsStockfish11(r.referenceId);
    std::map<int, LadderLevel> done;     // finished stages, by level
    int doneGames = 0;

    auto combine = [&](const MatchRunner::Snapshot& sn, const LadderStage& st){
        std::map<int, LadderLevel> all = done;
        for (auto& p : sn.pairings){
            if (p.a != 0 || p.b < 1 || p.b > (int)st.levels.size()) continue;
            LadderLevel l;
            l.elo = st.levels[p.b - 1];
            l.wdl = p.wdl; l.penta = p.penta; l.timeLosses = p.timeLosses; l.crashes = p.crashes;
            if (sf11 && p.b < (int)sn.depthCounts.size()){
                const std::vector<int>& h = sn.depthCounts[p.b];
                int pick = stockfish11PickDepth(l.elo);
                for (size_t d = 0; d < h.size(); d++){
                    l.refMoves += h[d];
                    if ((int)d < pick) l.refShallowMoves += h[d];
                }
            }
            LadderLevel& to = all[l.elo];
            to.elo = l.elo;
            addLevel(to, l);
        }
        std::vector<LadderLevel> v;
        for (auto& kv : all) v.push_back(kv.second);
        return v;
    };

    auto runStage = [&](const LadderStage& st, int index) -> bool {
        log("Stage " + std::to_string(index) + " of " + std::to_string(stages) + ": levels " + stageText(st));
        MatchSettings ms;
        ms.engines.push_back(s.engine);
        for (int lvl : st.levels){
            EngineConfig c = s.reference;
            c.name = refName + " (Elo " + std::to_string(lvl) + ")";
            c.setOption("UCI_LimitStrength", "true");
            c.setOption("UCI_Elo", std::to_string(lvl));
            ms.engines.push_back(c);
        }
        ms.mode = TournamentMode::Gauntlet;
        ms.gauntletSeeds = 1;
        ms.tc = s.tc;
        ms.timeMarginMs = s.timeMarginMs;
        ms.pairingGames = st.games;
        ms.concurrency = s.concurrency;
        ms.bookPath = book;
        ms.bookFormat = s.bookFormat;
        ms.bookPlies = s.bookPlies;
        ms.bookRandom = s.bookRandom;
        ms.seed = s.seed + (uint64_t)(index - 1) * 1000003ULL;   // stage 2 starts elsewhere in the book
        ms.pgnPath = s.pgnPath;
        ms.event = "Elo from games, stage " + std::to_string(index);
        if (!s.statePath.empty()) ms.statePath = s.statePath + ".stage" + std::to_string(index);
        int savedGames = ms.statePath.empty() ? 0 : stateGames(ms.statePath);
        MatchRunner runner;
        std::string err;
        if (!runner.start(ms, &err)){ r.error = err; return false; }
        {
            auto sn0 = runner.snapshot();
            if (index == 1) for (auto& w : sn0.warnings) r.warnings.push_back(w);
            if (sn0.gamesResumed) log("Resumed " + std::to_string(sn0.gamesResumed) + " games of stage "
                                      + std::to_string(index) + " from the state file");
            else if (savedGames > 0){
                std::string w = "the state file held " + std::to_string(savedGames) + " games of stage " + std::to_string(index)
                              + " played with other settings: the stage starts again";
                r.warnings.push_back(w);
                log(w);
            }
        }
        auto report = [&](const MatchRunner::Snapshot& sn){
            if (!o.progress) return;
            LadderProgress p;
            p.stage = index; p.stages = stages;
            p.gamesDone = doneGames + sn.gamesDone;
            p.gamesPlanned = planned;
            p.levels = combine(sn, st);
            p.fit = fitLadder(p.levels);
            for (auto& g : sn.live) if (g.active) p.live.push_back(g);
            o.progress(p);
        };
        while (runner.running()){
            if (stopped()){ runner.abort(); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(o.pollMs));
            report(runner.snapshot());
        }
        auto sn = runner.snapshot();
        report(sn);
        for (auto& l : combine(sn, st)) done[l.elo] = l;
        doneGames += sn.gamesDone;
        r.gamesReplayed += sn.gamesReplayed;
        r.stages.push_back(st);
        if (!sn.error.empty()){ r.error = sn.error; return false; }
        return !stopped();
    };

    bool ok = runStage(first, 1);
    if (ok && automatic){
        std::vector<LadderLevel> sofar;
        for (auto& kv : done) sofar.push_back(kv.second);
        LadderFit f1 = fitLadder(sofar);
        log("After stage 1: " + ladderHeadline(f1));
        LadderStage second = ladderSecondStage(f1, s.games - doneGames, r.refMinElo, r.refMaxElo);
        planned = doneGames + second.total();
        if (!second.levels.empty()) ok = runStage(second, 2);
    }

    for (auto& kv : done) r.levels.push_back(kv.second);
    r.fit = fitLadder(r.levels);
    r.gamesPlayed = r.fit.games;
    r.aborted = stopped();
    if (!r.error.empty() && r.aborted) r.error.clear();      // the user's stop isn't an error
    r.ok = r.error.empty() && !r.aborted;
    if (r.ok && !s.statePath.empty()) removeStateFiles(s.statePath);   // finished: the same command plays a new run

    int timeLosses = 0, crashes = 0;
    for (auto& l : r.levels){ timeLosses += l.timeLosses; crashes += l.crashes; }
    if (timeLosses) r.warnings.push_back(std::to_string(timeLosses) + " game(s) were lost on time (by either engine): "
                                         "check the engine's time management, or allow more --margin");
    if (crashes) r.warnings.push_back(std::to_string(crashes) + " game(s) ended with an engine crash or disconnect");
    if (r.fit.status == LadderStatus::Ok && r.fit.extrapolated)
        r.warnings.push_back("the rating is more than " + fmtElo(kExtrapolationNote) + " Elo beyond the levels played: "
                             "it is extrapolated; play levels closer to it");
    if (r.fit.status == LadderStatus::Above && r.fit.bound >= r.refMaxElo - 200)
        r.warnings.push_back("the engine is too strong for the reference's levels (strongest " + std::to_string(r.refMaxElo)
                             + "): only a lower bound is possible");
    if (r.fit.status == LadderStatus::Below && r.fit.bound <= r.refMinElo + 200)
        r.warnings.push_back("the engine is too weak for the reference's levels (weakest " + std::to_string(r.refMinElo)
                             + "): only an upper bound is possible");
    // At 60+0.6 and slower, a shortfall is no worse than in the calibration's own games (which
    // the nominal Elo already includes); faster, it makes the level weaker than its Elo.
    const bool fasterThanCalibration = s.tc.baseMs + 40LL * s.tc.incMs < kStockfish11CalibrationMs;
    for (auto& l : r.levels){
        if (!fasterThanCalibration || l.refMoves < kLadderShallowMinMoves
            || l.refShallowMoves <= kLadderShallowWarn * l.refMoves) continue;
        char b[440];
        std::snprintf(b, sizeof b, "at level %d, Stockfish 11's search stopped short of depth %d, the depth its UCI_Elo was "
                      "calibrated with, on %.0f%% of its moves (%d of %d): there it played weaker than %d, so the rating "
                      "comes out too high. Use a longer time control (the levels were fitted at 60+0.6) or lower levels",
                      l.elo, stockfish11PickDepth(l.elo), 100.0 * l.refShallowMoves / l.refMoves, l.refShallowMoves,
                      l.refMoves, l.elo);
        r.warnings.push_back(b);
    }
    if (r.levels.size() >= 3 && r.fit.fitP < 0.01){
        char b[64]; std::snprintf(b, sizeof b, "%.3f", r.fit.fitP);
        r.warnings.push_back(std::string("the scores against different levels don't fit one rating well (p = ") + b
                             + "): the engine does better against some levels than the others predict");
    }
    if (r.ok) log(">>> Rating: " + ladderHeadline(r.fit) + " (" + std::to_string(r.gamesPlayed) + " games)");
    return finish();
}

std::string ladderResultJson(const LadderResult& r){
    const LadderSettings& s = r.settings;
    json j;
    j["tool"] = "elo-games";
    j["ok"] = r.ok;
    j["aborted"] = r.aborted;
    if (!r.error.empty()) j["error"] = r.error;
    j["engine"] = {{"name", s.engine.name}, {"id", r.engineId}};
    j["reference"] = {{"name", s.reference.name}, {"id", r.referenceId},
                      {"uciEloMin", r.refMinElo}, {"uciEloMax", r.refMaxElo}};
    j["timeControl"] = s.tc.label();
    json levelsAsked = s.levels;
    j["settings"] = {{"timeControlMs", {{"base", s.tc.baseMs}, {"increment", s.tc.incMs}}}, {"marginMs", s.timeMarginMs},
                     {"games", s.games}, {"levels", s.levels.empty() ? json("auto") : levelsAsked},
                     {"concurrency", s.concurrency}, {"book", s.bookPath.empty() ? defaultLadderBook() : s.bookPath},
                     {"bookPlies", s.bookPlies}, {"bookRandom", s.bookRandom}, {"seed", s.seed},
                     {"engine", {{"path", s.engine.path}, {"args", s.engine.args}, {"options", s.engine.options}}},
                     {"reference", {{"path", s.reference.path}, {"args", s.reference.args}, {"options", s.reference.options}}}};
    j["gamesRequested"] = s.games;
    j["gamesPlayed"] = r.gamesPlayed;
    j["gamesReplayed"] = r.gamesReplayed;
    j["concurrency"] = s.concurrency;
    j["levelsMode"] = s.levels.empty() ? "automatic" : "given";
    json st = json::array();
    for (auto& x : r.stages) st.push_back({{"levels", x.levels}, {"games", x.games}});
    j["stages"] = st;
    json lv = json::array();
    for (auto& l : r.levels){
        json e = {{"level", l.elo}, {"games", l.wdl.games()}, {"wins", l.wdl.wins}, {"draws", l.wdl.draws},
                  {"losses", l.wdl.losses}, {"score", l.wdl.games() ? l.score() / l.wdl.games() : 0.0},
                  {"pairs", {l.penta.ll, l.penta.ld, l.penta.wl + l.penta.dd, l.penta.wd, l.penta.ww}},
                  {"timeLosses", l.timeLosses}, {"crashes", l.crashes}};
        if (r.fit.status == LadderStatus::Ok) e["expectedScore"] = ladderExpectedScore(r.fit.elo, l.elo);
        if (l.refMoves) e["referenceShallowShare"] = (double)l.refShallowMoves / l.refMoves;
        if (l.wdl.games() >= kLadderMinPerformanceGames){
            LadderFit pf = fitLadder({l});
            if (pf.status == LadderStatus::Ok) e["performance"] = {{"elo", pf.elo}, {"ci95", pf.ci95}};
            else e["performance"] = {{pf.status == LadderStatus::Above ? "above" : "below", pf.bound}};
        }
        lv.push_back(e);
    }
    j["levels"] = lv;
    const char* status = r.fit.status == LadderStatus::Ok ? "ok" : r.fit.status == LadderStatus::Above ? "above"
                       : r.fit.status == LadderStatus::Below ? "below" : "no-games";
    json fit = {{"status", status}, {"headline", ladderHeadline(r.fit)}, {"games", r.fit.games}};
    if (r.fit.status == LadderStatus::Ok){
        fit["elo"] = r.fit.elo; fit["ci95"] = r.fit.ci95; fit["extrapolated"] = r.fit.extrapolated;
        fit["low95"] = r.fit.low95; fit["high95"] = r.fit.high95;
    }
    if (r.fit.status == LadderStatus::Above || r.fit.status == LadderStatus::Below) fit["bound95"] = r.fit.bound;
    if (r.fit.status != LadderStatus::NoGames){ fit["dispersion"] = r.fit.dispersion; fit["fitP"] = r.fit.fitP; }
    j["fit"] = fit;
    j["warnings"] = r.warnings;
    j["seconds"] = r.seconds;
    j["note"] = "Rating on the reference engine's UCI_Elo scale, at this time control on this computer. "
                "The interval is statistical only: the error of the reference's levels is not included.";
    return j.dump(2);
}
