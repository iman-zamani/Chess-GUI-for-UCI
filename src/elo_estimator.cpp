/* Chess-GUI-for-UCI — position-based Elo estimator. GPL-2.0 */
#include "elo_estimator.hpp"
#include "chess.hpp"
#include "power.hpp"
#include "testing.hpp"
#include "uci_engine.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

// ---------------------------------------------------------------- model
bool EloModel::loadJson(const std::string& text, std::string* err){
    auto fail = [&](const std::string& e){ if (err) *err = e; return false; };
    json j;
    try { j = json::parse(text); } catch (const std::exception& e){ return fail(std::string("model: ") + e.what()); }
    try {
        if (j.value("format", "") != "chessgui-elo-model") return fail("not an Elo estimator model");
        version = j.at("version").get<int>();
        threshold = j.value("threshold", 0.0);
        const json& p = j.at("protocol");
        baseMovetimeMs = p.value("baseMovetimeMs", 500);
        calibNps = p.value("calibNps", 0.0);
        factorMin = p.value("factorMin", 0.25);
        factorMax = p.value("factorMax", 4.0);
        hashMb = p.value("hashMb", 64);
        moveOverheadMs = p.value("moveOverheadMs", 10);
        graceMs = p.value("graceMs", 50);
        eloIntercept = j.at("elo").value("intercept", 2500.0);
        eloSlope = j.at("elo").value("slope", 400.0);
        priorMean = j.at("prior").value("mean", 0.0);
        priorSd = j.at("prior").value("sd", 3.0);
        if (j.contains("adaptive")){
            seTargetElo = j["adaptive"].value("seTargetElo", 50.0);
            maxItems = j["adaptive"].value("maxItems", 300);
            minItems = j["adaptive"].value("minItems", 20);
        }
        errorRmse = j.value("errorRmse", 0.0);
        validationText = j.value("validationText", "");
        groupShare.clear();
        if (j.contains("groupShare")) for (auto& [k, v] : j["groupShare"].items()) groupShare[k] = v.get<double>();
        calibration.clear();
        if (j.contains("calibration"))
            for (auto& c : j["calibration"]) calibration.push_back({c.at("name").get<std::string>(), c.at("elo").get<double>()});
        items.clear();
        for (auto& it : j.at("items")){
            EloItem e;
            e.id = it.at("id").get<std::string>();
            e.fen = it.at("fen").get<std::string>();
            e.category = it.value("category", "");
            e.group = it.value("group", e.category);
            e.a = it.at("a").get<double>();
            e.b = it.at("b").get<double>();
            e.ok = it.at("ok").get<std::vector<std::string>>();
            items.push_back(e);
        }
    } catch (const std::exception& e){ return fail(std::string("model: ") + e.what()); }
    if (items.empty()) return fail("model has no positions");
    if (eloSlope <= 0 || priorSd <= 0) return fail("model: bad scale or prior");
    return true;
}

bool EloModel::load(const std::string& path, std::string* err){
    std::ifstream f(path, std::ios::binary);
    if (!f){ if (err) *err = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return loadJson(ss.str(), err);
}

// ---------------------------------------------------------------- IRT
static double sigmoid(double z){
    z = std::max(-35.0, std::min(35.0, z));                  // as numpy.clip in irt.py
    return 1.0 / (1.0 + std::exp(-z));
}

double itemInformation(const EloItem& it, double theta){
    double p = sigmoid(it.a * (theta - it.b));
    return it.a * it.a * p * (1 - p);
}

ThetaEstimate estimateTheta(const EloModel& m, const std::vector<std::pair<int, bool>>& resp){
    double th = m.priorMean, prec = 1.0 / (m.priorSd * m.priorSd);
    for (int iter = 0; iter < 50; iter++){
        double g = -prec * (th - m.priorMean), h = prec;
        for (auto& [i, y] : resp){
            const EloItem& it = m.items[i];
            double p = sigmoid(it.a * (th - it.b));
            g += it.a * ((y ? 1.0 : 0.0) - p);
            h += it.a * it.a * p * (1 - p);
        }
        double step = std::max(-2.0, std::min(2.0, g / h));
        th += step;
        if (std::fabs(step) < 1e-10) break;
    }
    double info = prec;
    for (auto& r : resp) info += itemInformation(m.items[r.first], th);
    return {th, 1.0 / std::sqrt(info)};
}

// ---------------------------------------------------------------- session
EloSession::EloSession(const EloModel& m, bool adaptive) : model_(m), adaptive_(adaptive), used_(m.items.size(), false) {
    est_ = estimateTheta(m, {});
}

bool EloSession::done() const{
    if (resp_.size() >= model_.items.size()) return true;
    if (!adaptive_) return false;
    if ((int)resp_.size() >= model_.maxItems) return true;
    return (int)resp_.size() >= model_.minItems && eloSe() <= model_.seTargetElo;
}

int EloSession::next(){
    if (done()) return -1;
    if (!adaptive_){
        while (cursor_ < used_.size() && used_[cursor_]) cursor_++;
        return cursor_ < used_.size() ? (int)cursor_ : -1;
    }
    // the category group furthest below its share, then the most informative position in it
    std::map<std::string, int> count;
    for (auto& r : resp_) count[model_.items[r.first].group]++;
    std::string want;
    double worst = 1e9;
    for (auto& [g, share] : model_.groupShare){
        double have = resp_.empty() ? 0 : (double)count[g] / resp_.size();
        bool any = false;
        for (size_t i = 0; i < used_.size() && !any; i++) any = !used_[i] && model_.items[i].group == g;
        if (any && have - share < worst){ worst = have - share; want = g; }
    }
    int best = -1;
    double bestInfo = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++)          // pass 1: any group
        for (size_t i = 0; i < used_.size(); i++){
            if (used_[i] || (pass == 0 && !want.empty() && model_.items[i].group != want)) continue;
            double inf = itemInformation(model_.items[i], est_.theta);
            if (inf > bestInfo){ bestInfo = inf; best = (int)i; }
        }
    return best;
}

void EloSession::record(int item, bool solved){
    if (item < 0 || item >= (int)used_.size() || used_[item]) return;
    used_[item] = true;
    resp_.push_back({item, solved});
    est_ = estimateTheta(model_, resp_);
}

// ---------------------------------------------------------------- results
EloResult eloResultFrom(const EloModel& m, const EloSession& s){
    EloResult r;
    r.ok = !s.responses().empty();
    ThetaEstimate t = s.estimate();
    r.elo = m.toElo(t.theta);
    r.seMeasurement = t.se * m.eloSlope;
    r.ci95 = 1.96 * std::sqrt(r.seMeasurement * r.seMeasurement + m.errorRmse * m.errorRmse);
    r.answered = (int)s.responses().size();
    std::map<std::string, std::vector<std::pair<int, bool>>> byGroup;
    for (auto& x : s.responses()){ r.solved += x.second; byGroup[m.items[x.first].group].push_back(x); }
    for (auto& [g, v] : byGroup) r.groups[g] = {m.toElo(estimateTheta(m, v).theta), (int)v.size()};
    return r;
}

static std::string normalised(const std::string& s){
    std::string o;
    for (char c : s) if (c != ' ' && c != '_') o += (char)std::tolower((unsigned char)c);
    return o;
}

static void configure(UciEngine& e, const EloModel& m){
    if (e.hasOption("Threads")) e.setOption("Threads", "1");
    if (e.hasOption("Hash")) e.setOption("Hash", std::to_string(m.hashMb));
    for (auto& o : e.options())                              // safety margins meant for laggy GUIs
        if (normalised(o.name) == "moveoverhead" && o.type == "spin")
            e.setOption(o.name, std::to_string(std::max(o.minV, std::min<long long>(m.moveOverheadMs, o.maxV))));
    e.isReady();
}

EloResult runEloEstimate(const EngineConfig& cfg, const EloModel& m, const EloOptions& opt){
    EloResult fail;
    fail.adaptive = opt.adaptive;
    double factor = opt.factor;
    double nps = 0;
    if (factor <= 0){
        nps = runPerftBenchmark(3).nps;
        factor = m.calibNps > 0 && nps > 0 ? m.calibNps / nps : 1.0;
    }
    factor = std::max(m.factorMin, std::min(m.factorMax, factor));
    int movetime = (int)std::lround(m.baseMovetimeMs * factor);
    UciEngine e;
    if (!e.start(cfg)){ fail.error = "cannot start the engine: " + e.lastError(); return fail; }
    configure(e, m);
    EloSession s(m, opt.adaptive);
    std::vector<EloAnswer> answers;
    int planned = opt.adaptive ? std::min<int>(m.maxItems, (int)m.items.size()) : (int)m.items.size();
    int idx;
    while ((idx = s.next()) >= 0){
        if (opt.abort && *opt.abort) break;
        const EloItem& it = m.items[idx];
        EloAnswer a;
        a.id = it.id; a.category = it.category;
        std::string best;
        for (int attempt = 0; attempt < 4; attempt++){
            if (!e.alive()){                                  // restart after a crash
                e.quit();
                if (!e.start(cfg)) break;
                configure(e, m);
            }
            SuspendMark sm = suspendMark();
            e.newGame();
            e.setPosition(it.fen, {});
            best = e.goMovetimeStrict(movetime, m.graceMs);
            if (suspendedMsSince(sm) > 500) continue;          // the computer slept: ask again
            break;
        }
        EngineInfo inf = e.lastInfo();
        a.depth = inf.depth; a.nodes = inf.nodes; a.timeMs = inf.timeMs;
        Position p = Position::fromFEN(it.fen);
        Move mv;
        if (!best.empty() && p.uciToMove(best, mv)){
            a.move = p.moveToUCI(mv);                          // one castling notation
            a.solved = std::find(it.ok.begin(), it.ok.end(), a.move) != it.ok.end();
        } else a.move = best;
        answers.push_back(a);
        s.record(idx, a.solved);
        if (opt.progress){
            EloProgress pr;
            pr.answered = (int)s.responses().size(); pr.planned = planned;
            pr.elo = m.toElo(s.estimate().theta); pr.se = s.eloSe(); pr.lastId = it.id;
            opt.progress(pr);
        }
    }
    std::string name = e.name();
    e.quit();
    if (s.responses().empty()){ fail.error = (opt.abort && *opt.abort) ? "aborted" : "the engine answered no position"; return fail; }
    EloResult r = eloResultFrom(m, s);
    r.adaptive = opt.adaptive;
    r.engineName = name;
    r.userNps = nps; r.factor = factor; r.movetimeMs = movetime;
    r.answers = answers;
    return r;
}

// ---------------------------------------------------------------- reports
std::string eloResultJson(const EloResult& r, const EloModel& m){
    json groups = json::object();
    for (auto& [g, v] : r.groups) groups[g] = {{"elo", std::lround(v.first)}, {"positions", v.second}};
    json ans = json::array();
    for (auto& a : r.answers)
        ans.push_back({{"id", a.id}, {"move", a.move}, {"solved", a.solved}, {"depth", a.depth}, {"nodes", a.nodes}});
    json j = {{"engine", r.engineName}, {"elo", std::lround(r.elo)}, {"ci95", std::lround(r.ci95)},
              {"seMeasurement", std::lround(r.seMeasurement)}, {"scale", "CCRL Blitz equivalent"},
              {"mode", r.adaptive ? "quick" : "full"}, {"positions", r.answered}, {"solved", r.solved},
              {"movetimeMs", r.movetimeMs}, {"hardwareFactor", r.factor}, {"nps", r.userNps},
              {"modelVersion", m.version}, {"modelError", m.validationText}, {"groups", groups},
              {"note", "estimated from test positions, not from games; time management is not measured"},
              {"answers", ans}};
    return j.dump(2);
}

std::string eloResultMarkdown(const EloResult& r, const EloModel& m){
    std::ostringstream o;
    o << "# Elo estimate: " << r.engineName << "\n\n"
      << "**" << std::lround(r.elo) << " +/- " << std::lround(r.ci95) << " (95%), CCRL-Blitz-equivalent**\n\n"
      << "This is an estimate from " << r.answered << " test positions (" << (r.adaptive ? "quick, adaptive" : "full")
      << " mode), not a rating from games: time management and other game skills are not measured.\n\n"
      << "| Category | Elo | Positions |\n|---|---|---|\n";
    for (auto& [g, v] : r.groups) o << "| " << g << " | " << std::lround(v.first) << " | " << v.second << " |\n";
    o << "\nSolved " << r.solved << " of " << r.answered << ". Move time " << r.movetimeMs << " ms per position"
      << " (hardware factor " << std::fixed;
    o.precision(2);
    o << r.factor << ").\n";
    if (!m.validationText.empty()) o << "\nModel accuracy: " << m.validationText << ".\n";
    return o.str();
}
