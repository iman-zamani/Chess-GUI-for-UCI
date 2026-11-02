/* Chess-GUI-for-UCI — position-based Elo estimator. GPL-2.0 */
#include "elo_estimator.hpp"
#include "chess.hpp"
#include "cpu_placement.hpp"
#include "power.hpp"
#include "testing.hpp"
#include "uci_engine.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>

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
        errorBands.clear();
        if (j.contains("errorBands"))
            for (auto& b : j["errorBands"])
                errorBands.push_back({b.at("from").get<double>(), b.at("to").get<double>(), b.at("rmse").get<double>()});
        validationText = j.value("validationText", "");
        reliableFromElo = j.value("reliableFromElo", 0.0);
        if (j.contains("range")){
            rangeMin = j["range"].value("min", 0.0);
            rangeMax = j["range"].value("max", 0.0);
        }
        speedRefs.clear();
        if (j.contains("speed")){
            const json& s = j["speed"];
            warmupSec = s.value("warmupSec", warmupSec);
            speedPositions = s.value("positions", speedPositions);
            speedSigmaLog2 = s.value("sigmaLog2", speedSigmaLog2);
            fallbackSigmaLog2 = s.value("fallbackSigmaLog2", fallbackSigmaLog2);
            eloPerDoubling = s.value("eloPerDoubling", eloPerDoubling);
            eloPerDoublingErr = s.value("eloPerDoublingErr", eloPerDoublingErr);
            minMovetimeMs = s.value("minMovetimeMs", minMovetimeMs);
            singleReferenceSigmaLog2 = s.value("singleReferenceSigmaLog2", singleReferenceSigmaLog2);
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
            // engines' speed ratios scatter more between an ARM computer and the (x86)
            // calibration machine than between two x86 ones: measured separately
            speedSigmaLog2 = s.value("sigmaLog2Arm64", speedSigmaLog2);
            singleReferenceSigmaLog2 = s.value("singleReferenceSigmaLog2Arm64", singleReferenceSigmaLog2);
#endif
            checkEverySec = s.value("checkEverySec", checkEverySec);
            checkPositions = s.value("checkPositions", checkPositions);
            for (auto& r : s.value("references", json::array())){
                SpeedReference ref;
                ref.id = r.at("id").get<std::string>();
                ref.file = r.at("file").get<std::string>();
                ref.idName = r.value("idName", "");
                for (auto& it : r.at("items")){
                    SpeedReferenceItem x;
                    x.fen = it.at("fen").get<std::string>();
                    x.nodes = it.value("nodes", 0LL);
                    x.depth = it.value("depth", 0);
                    x.calMs = it.at("calMs").get<double>();
                    if (x.calMs > 0 && (x.nodes > 0 || x.depth > 0)) ref.items.push_back(x);
                }
                if (!ref.items.empty()) speedRefs.push_back(ref);
            }
        }
        if (j.contains("checks")){
            const json& c = j["checks"];
            driftWarn = c.value("driftWarnLog2", driftWarn);
            driftFail = c.value("driftFailLog2", driftFail);
            timeUseWarn = c.value("timeUseWarn", timeUseWarn);
            timeUseFail = c.value("timeUseFail", timeUseFail);
            overrunWarn = c.value("overrunWarn", overrunWarn);
            overrunFail = c.value("overrunFail", overrunFail);
            cpuRatioMax = c.value("cpuRatioMax", cpuRatioMax);
            cpuRatioMin = c.value("cpuRatioMin", cpuRatioMin);
            cpuRatioWarn = c.value("cpuRatioWarn", cpuRatioWarn);
            errorRateFail = c.value("errorRateFail", errorRateFail);
            idleCpuFail = c.value("idleCpuFail", idleCpuFail);
            pCoreWarn = c.value("pCoreWarn", pCoreWarn);
            pCoreFail = c.value("pCoreFail", pCoreFail);
        }
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

double eloCi95(const EloModel& m, double elo, double seElo, double speedErrorElo){
    double me = m.errorAt(elo);
    return 1.96 * std::sqrt(seElo * seElo + me * me + speedErrorElo * speedErrorElo);
}

EloRangeStatus eloRangeStatus(const EloModel& m, double elo, double ci95){
    EloRangeStatus r;
    if (m.rangeMax > 0 && elo > m.rangeMax){
        r.status = EloStatus::AboveRange;
        r.bound = std::min(m.rangeMax, (double)std::lround(elo - ci95));
    } else if (m.rangeMin > 0 && elo < m.rangeMin){
        r.status = EloStatus::BelowRange;
        r.bound = std::max(m.rangeMin, (double)std::lround(elo + ci95));
    }
    return r;
}

bool EloSession::done() const{
    if (resp_.size() >= model_.items.size()) return true;
    if (!adaptive_) return false;
    if ((int)resp_.size() >= model_.maxItems) return true;
    if ((int)resp_.size() < model_.minItems) return false;
    if (eloSe() <= model_.seTargetElo) return true;
    // clearly outside the calibrated range (the whole interval): more positions can't give a number
    double e = elo(), half = eloCi95(model_, e, eloSe(), speedErrorElo_);
    if (model_.rangeMax > 0 && e - half > model_.rangeMax) return true;
    if (model_.rangeMin > 0 && e + half < model_.rangeMin) return true;
    return false;
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

double EloModel::errorAt(double elo) const {
    for (auto& b : errorBands) if (elo >= b.from && elo < b.to) return b.rmse;
    return errorRmse;
}

// ---------------------------------------------------------------- results
std::string eloStatusName(EloStatus s){
    switch (s){
        case EloStatus::Ok: return "ok";
        case EloStatus::AboveRange: return "above_range";
        case EloStatus::BelowRange: return "below_range";
        case EloStatus::Unreliable: return "unreliable";
    }
    return "ok";
}

EloResult eloResultFrom(const EloModel& m, const EloSession& s){
    return eloResultFrom(m, s, EloDiagnostics{});
}

EloResult eloResultFrom(const EloModel& m, const EloSession& s, const EloDiagnostics& diag){
    EloResult r;
    r.ok = !s.responses().empty();
    r.diag = diag;
    ThetaEstimate t = s.estimate();
    r.elo = s.elo();
    r.seMeasurement = t.se * m.eloSlope;
    r.modelErrorElo = m.errorAt(r.elo);
    r.diag.speedErrorElo = s.speedErrorElo();                  // the session's: the early stop used the same
    r.ci95 = eloCi95(m, r.elo, r.seMeasurement, s.speedErrorElo());
    r.answered = (int)s.responses().size();
    std::map<std::string, std::vector<std::pair<int, bool>>> byGroup;
    for (auto& x : s.responses()){ r.solved += x.second; byGroup[m.items[x.first].group].push_back(x); }
    for (auto& [g, v] : byGroup) r.groups[g] = {m.toElo(estimateTheta(m, v).theta) + s.offsetElo(), (int)v.size()};
    // past an end of the calibrated range the model extrapolates (cross-validated errors
    // there: -213 to +1148), so there is no number, only a bound (eloRangeStatus)
    EloRangeStatus rs = eloRangeStatus(m, r.elo, r.ci95);
    r.status = rs.status;
    r.bound = rs.bound;
    r.rangeMin = m.rangeMin; r.rangeMax = m.rangeMax;
    return r;
}

void applyChecks(const EloModel& m, EloResult& r){
    EloDiagnostics& d = r.diag;
    auto pct = [](double x){ return std::to_string((int)std::lround(x * 100)) + "%"; };
    // the engine must use the time it is given: the model assumes it did
    std::vector<double> use;
    for (auto& a : r.answers) if (a.error.empty() && r.movetimeMs > 0) use.push_back((double)a.wallMs / r.movetimeMs);
    d.timeUse = medianOf(use);
    if (!use.empty() && d.timeUse < m.timeUseFail)
        d.failures.push_back("the engine used a median " + pct(d.timeUse) + " of its move time: it doesn't search for the time "
                             "it is given (\"go movetime\"), so its answers don't reflect its strength");
    else if (!use.empty() && d.timeUse < m.timeUseWarn)
        d.warnings.push_back("the engine used a median " + pct(d.timeUse) + " of its move time (calibration engines used 90-100%): "
                             "the estimate is somewhat low if it keeps a large safety margin");
    else if (!use.empty() && d.timeUse > m.overrunFail)
        d.failures.push_back("the engine searched a median " + pct(d.timeUse) + " of its move time: it doesn't stop at the "
                             "time it is given (\"go movetime\") and needed a \"stop\", so it got more search than the "
                             "calibration engines and its estimate would be too high");
    else if (!use.empty() && d.timeUse > m.overrunWarn)
        d.warnings.push_back("the engine searched a median " + pct(d.timeUse) + " of its move time: the estimate is somewhat high");
    d.errors = d.stops = 0;
    for (auto& a : r.answers){ d.errors += !a.error.empty(); d.stops += a.stopped; }
    if (!r.answers.empty() && d.errors > m.errorRateFail * r.answers.size())
        d.failures.push_back(std::to_string(d.errors) + " of " + std::to_string(r.answers.size())
                             + " answers were illegal moves, no move or a crash");
    else if (d.errors > 0)
        d.warnings.push_back(std::to_string(d.errors) + " answers were illegal moves, no move or a crash (counted as not solved)");
    if (!r.answers.empty() && d.stops > 0.05 * r.answers.size())
        d.warnings.push_back("the engine needed a \"stop\" on " + std::to_string(d.stops) + " positions (it overran the move time)");
    if (d.idleCpu > m.idleCpuFail)
        d.failures.push_back("the engine kept " + pct(d.idleCpu) + " of a CPU core busy while it was waiting for a command "
                             "(busy-waiting for its input): that takes the core from the speed checks and from its own "
                             "search, so the measurement doesn't hold");
    if (d.cpuRatio > m.cpuRatioMax)
        d.failures.push_back("the engine used " + std::to_string(d.cpuRatio).substr(0, 4) + "x as much CPU time as wall time: "
                             "it searches with more than one thread although Threads=1 was set (or a runtime of its own, "
                             "such as a garbage collector, keeps another core busy)");
    else if (d.cpuRatio >= 0 && d.cpuRatio < m.cpuRatioMin)
        d.failures.push_back("the engine got only " + pct(d.cpuRatio) + " of a CPU core while searching: another program "
                             "was using its core, so it got less search than measured; run it again with less load");
    else if (d.cpuRatio >= 0 && d.cpuRatio < m.cpuRatioWarn)
        d.warnings.push_back("the engine got only " + pct(d.cpuRatio) + " of a CPU core while searching (other load on this "
                             "computer?): the estimate is probably low");
    if (std::fabs(d.driftLog2) > m.driftFail)
        d.failures.push_back("the computer's speed changed by " + pct(std::exp2(std::fabs(d.driftLog2)) - 1)
                             + " during the test (throttling, power saving or other load): run it again on an idle, plugged-in computer");
    else if (std::fabs(d.driftLog2) > m.driftWarn)
        d.warnings.push_back("the computer's speed changed by " + pct(std::exp2(std::fabs(d.driftLog2)) - 1) + " during the test");
    // Apple silicon: the engine, or the reference engines that set its move time, ran
    // partly on efficiency cores (much slower) instead of performance cores
    if (d.pCoreShare >= 0 && d.pCoreShare < m.pCoreFail)
        d.failures.push_back("the engine ran " + pct(1 - d.pCoreShare) + " of its search time on efficiency cores (slower "
                             "than the performance cores the speed was measured on): close other programs, plug in the "
                             "computer and run it again");
    else if (d.pCoreShare >= 0 && d.pCoreShare < m.pCoreWarn)
        d.warnings.push_back("the engine ran " + pct(1 - d.pCoreShare) + " of its search time on efficiency cores: the "
                             "estimate is probably low");
    double refP = 1;
    for (auto& x : d.speedStart.engines) if (x.pCoreShare >= 0) refP = std::min(refP, x.pCoreShare);
    if (refP < m.pCoreFail)
        d.failures.push_back("the speed measurement ran " + pct(1 - refP) + " on efficiency cores, so the move time is too "
                             "long: close other programs and run it again");
    else if (refP < m.pCoreWarn)
        d.warnings.push_back("the speed measurement ran " + pct(1 - refP) + " on efficiency cores: the estimate is "
                             "probably high");
    if (!d.placementApplied)
        d.warnings.push_back("the engine could not be given its CPU placement (" + d.placement + "): it may have run on "
                             "a different kind of core than the speed measurement");
    if (!d.failures.empty() && r.ok) r.status = EloStatus::Unreliable;
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

// The engine's own speed: one fixed search on one position, repeated. A node
// count when the engine stops at one, else the depth it reached; its speed is
// the nodes it reports per millisecond of wall time (per search when it reports
// no nodes), so an engine whose depth-d search varies a little in size (history
// kept between searches) still gives a usable reading. Compared over the run,
// it shows the engine getting slower or faster on whatever core it runs on.
static const double kMinProbeMs = 20;                     // shorter searches can't be timed well enough
static const double kProbeSpread = 1.10;                  // the repeats at the start must agree this well
static std::string changeText(double log2){
    return std::to_string((int)std::lround((std::exp2(std::fabs(log2)) - 1) * 100)) + "%";
}
struct OwnProbe { std::string fen, go; long long nodes = 0; int depth = 0; int timeoutMs = 10000; double speed = 0; };

// speed of one search (> 0), or -1; ms is its wall time
static double probeOnce(UciEngine& e, const OwnProbe& p, double& ms){
    e.newGame();
    e.setPosition(p.fen, {});
    std::string best = p.nodes > 0 ? e.goNodes(p.nodes, p.timeoutMs) : e.goDepth(p.depth, p.timeoutMs);
    if (best.empty() || e.lastSearchStopped()) return -1;
    if (p.nodes <= 0 && e.lastInfo().depth < p.depth) return -1;
    ms = (double)e.lastSearchWallMs();
    if (ms < kMinProbeMs) return -1;
    long long n = e.lastInfo().nodes;
    return n > 0 ? n / ms : 1000.0 / ms;
}

// median speed of three searches (-1 unless at least two worked); spread: largest / smallest
static double probeMedian(UciEngine& e, const OwnProbe& p, double* spread = nullptr){
    std::vector<double> v;
    for (int i = 0; i < 3; i++){ double ms, x = probeOnce(e, p, ms); if (x > 0) v.push_back(x); }
    if (v.size() < 2) return -1;
    if (spread) *spread = *std::max_element(v.begin(), v.end()) / *std::min_element(v.begin(), v.end());
    return medianOf(v);
}

static OwnProbe setupProbe(UciEngine& e, const EloModel& m, int movetime){
    OwnProbe p;
    p.fen = m.items[0].fen;
    p.timeoutMs = std::max(2000, 10 * movetime);
    e.newGame();
    e.setPosition(p.fen, {});
    if (e.goMovetimeStrict(std::max(100, movetime / 2), m.graceMs).empty()) return p;
    EngineInfo inf = e.lastInfo();
    auto usable = [&](OwnProbe& t){
        double spread = 0;
        t.speed = probeMedian(e, t, &spread);
        return t.speed > 0 && spread <= kProbeSpread;
    };
    if (inf.nodes > 1000){
        OwnProbe t = p;
        t.nodes = inf.nodes;
        e.newGame();
        e.setPosition(t.fen, {});
        bool ok = !e.goNodes(t.nodes, t.timeoutMs).empty() && !e.lastSearchStopped();
        long long got = e.lastInfo().nodes;
        if (ok && got >= t.nodes * 9 / 10 && got <= t.nodes * 11 / 10){
            t.go = "go nodes " + std::to_string(t.nodes);
            if (usable(t)) return t;
        }
    }
    if (inf.depth > 1){
        OwnProbe t = p;
        t.depth = inf.depth;
        t.go = "go depth " + std::to_string(t.depth);
        if (usable(t)) return t;
    }
    return p;
}

EloResult runEloEstimate(const EngineConfig& cfg, const EloModel& m, const EloOptions& opt){
    using clk = std::chrono::steady_clock;
    auto secondsSince = [](clk::time_point t){ return std::chrono::duration<double>(clk::now() - t).count(); };
    EloResult fail;
    fail.adaptive = opt.adaptive;
    auto log = [&](const std::string& s){ if (opt.log) opt.log(s); };
    auto aborted = [&]{ return opt.abort && *opt.abort; };
    KeepAwake awake("Elo estimate");
    keepThisProcessUnthrottled();                                // it times the engine's searches
    EloDiagnostics d;
    CpuPlacement pl = opt.placement ? timedRunPlacement() : noPlacement();
    d.placement = pl.description;
    log("CPU placement: " + pl.description);
    SpeedOptions so;
    so.placement = pl; so.warmupSec = m.warmupSec; so.positions = m.speedPositions; so.abort = opt.abort; so.log = opt.log;
    double factor = opt.factor, nps = 0, sigmaLog2 = 0;
    if (factor <= 0){
        if (!m.speedRefs.empty()){
            log("Measuring this computer's speed with the reference engines...");
            d.speedStart = measureSpeed(m.speedRefs, so);
            if (aborted()){ fail.error = "aborted"; return fail; }
            if (d.speedStart.ok){ d.referenceSpeed = true; factor = 1.0 / d.speedStart.speed; }
            else log("Reference engines: " + d.speedStart.error);
        }
        if (d.referenceSpeed){
            // with one reference engine the speed stands less well for other engines'
            std::string missing;
            for (auto& x : d.speedStart.engines) if (x.speed <= 0) missing += (missing.empty() ? "" : "; ") + x.id + ": " + x.error;
            sigmaLog2 = missing.empty() ? m.speedSigmaLog2 : m.singleReferenceSigmaLog2;
            if (!missing.empty())
                d.warnings.push_back("only one reference engine could be measured (" + missing + "): the speed is less "
                                     "certain, so the interval is wider");
        } else {
            // fallback: the internal perft benchmark, on the same placement
            std::thread t([&]{ applyToCurrentThread(pl); nps = runPerftBenchmark(3).nps; });
            t.join();
            factor = m.calibNps > 0 && nps > 0 ? m.calibNps / nps : 1.0;
            sigmaLog2 = m.fallbackSigmaLog2;
            d.warnings.push_back("the reference engines are not available (" + (m.speedRefs.empty() ? std::string("not in the model")
                                 : d.speedStart.error) + "): the move time comes from the internal benchmark, which can be "
                                 "off by 2x for some engines, so the interval is wider");
        }
    } else {
        sigmaLog2 = m.speedSigmaLog2;                           // at least as uncertain as a measured speed
        d.warnings.push_back("the move time was set by hand (factor " + std::to_string(factor).substr(0, 4) + "), not measured");
    }
    double wanted = m.baseMovetimeMs * factor;
    double clamped = std::max(m.baseMovetimeMs * m.factorMin, std::min(m.baseMovetimeMs * m.factorMax, wanted));
    int movetime = (int)std::lround(std::max<double>(m.minMovetimeMs, clamped));
    double clampLog2 = std::log2(movetime / wanted);
    if (std::fabs(clampLog2) > 0.05){
        // the engine gets more (or less) search than the calibration engines had: the
        // estimate is corrected by the measured change per doubling of search time
        d.clampLog2 = clampLog2;
        d.clampCorrectionElo = -m.eloPerDoubling * clampLog2;
        d.warnings.push_back("the move time had to be " + std::string(movetime > wanted ? "raised" : "lowered") + " to "
                             + std::to_string(movetime) + " ms from the " + std::to_string((int)std::lround(wanted))
                             + " ms this computer's speed calls for: the estimate was corrected by "
                             + std::to_string((int)std::lround(d.clampCorrectionElo)) + " Elo for it");
    }
    log("Move time " + std::to_string(movetime) + " ms per position");

    UciEngine e;
    e.setPlacement(pl);
    if (!e.start(cfg)){ fail.error = "cannot start the engine: " + e.lastError(); return fail; }
    d.placementApplied = e.placementApplied();
    if (!e.powerThrottlingOff())
        d.warnings.push_back("Windows did not accept running the engine without power throttling (Windows 10 before "
                             "1709?): keep this program's window in front during the test");
    if (pl.shared)
        d.warnings.push_back("another timed run was using every suitable CPU core, so this one shares a core with it: run "
                             "estimates one after another");
    configure(e, m);
    OwnProbe probe = setupProbe(e, m, movetime);
    d.ownProbe = probe.speed > 0 ? probe.go : "";
    if (probe.speed <= 0)
        d.warnings.push_back("the engine's own speed could not be followed during the test (its searches with a fixed "
                             "node count or depth don't repeat well enough); only the reference engines checked the speed");
    EloSession s(m, opt.adaptive);
    // the speed term of the interval as known so far: the speed measurement, the
    // reference engines' agreement and the changes of speed seen by the checks, and
    // the uncertainty of the clamp correction. The early stop, the running estimate
    // and the result all use it, so a test that stops "outside the range" ends there.
    double spread = d.referenceSpeed ? d.speedStart.spreadLog2 / 2 : 0;
    auto updateSpeedTerm = [&]{
        double sumSq = 0;
        int n = 0;
        d.driftLog2 = 0;
        for (auto& c : d.speedChecks)
            for (auto [ok, v] : {std::make_pair(c.referenceOk, c.referenceLog2), std::make_pair(c.ownOk, c.ownLog2)})
                if (ok){
                    if (std::fabs(v) > std::fabs(d.driftLog2)) d.driftLog2 = v;
                    sumSq += v * v; n++;
                }
        d.driftRmsLog2 = n ? std::sqrt(sumSq / n) : 0;
        double sig = std::sqrt(sigmaLog2 * sigmaLog2 + spread * spread + d.driftRmsLog2 * d.driftRmsLog2);
        double fromSpeed = m.eloPerDoubling * sig, fromClamp = m.eloPerDoublingErr * clampLog2;
        d.speedErrorElo = std::sqrt(fromSpeed * fromSpeed + fromClamp * fromClamp);
        s.setSpeed(d.speedErrorElo, d.clampCorrectionElo);
    };
    updateSpeedTerm();
    std::vector<EloAnswer> answers;
    int planned = opt.adaptive ? std::min<int>(m.maxItems, (int)m.items.size()) : (int)m.items.size();
    // a speed check: the engine's own probe, then the reference engines replaying the
    // positions they replayed at the start (at the end: more of them). A reading past
    // the warning level is taken again at once and the smaller change kept: a change
    // that lasts shows twice, a passing one (a burst of other load, one slow search)
    // doesn't fail the run on its own. The engine's CPU use while it waits for the
    // reference engines shows an engine that busy-waits for its input.
    auto t0 = clk::now();
    double idleCpu = 0, idleWall = 0;
    auto check = [&](bool atEnd){
        EloSpeedCheck c;
        c.atSec = secondsSince(t0);
        if (probe.speed > 0 && e.alive()){
            auto own = [&]{ double now = probeMedian(e, probe); return now > 0 ? std::log2(now / probe.speed) : NAN; };
            double v = own();
            if (std::fabs(v) > m.driftWarn){
                log("The engine's speed reading changed by " + changeText(v) + ": taking it again");
                double again = own();
                if (std::fabs(again) < std::fabs(v)) v = again;
            }
            if (!std::isnan(v)){ c.ownOk = true; c.ownLog2 = v; }
        }
        if (d.referenceSpeed && !aborted()){
            SpeedOptions so2 = so;
            so2.warmupSec = 0;
            so2.positions = atEnd ? std::max(8, m.speedPositions / 2) : m.checkPositions;
            so2.log = nullptr;
            double cpuA = e.alive() ? e.cpuSeconds() : -1;
            auto wallA = clk::now();
            SpeedMeasurement sm = measureSpeed(m.speedRefs, so2);
            SpeedChange ch = speedChange(d.speedStart, sm);
            if (ch.ok && std::fabs(ch.log2) > m.driftWarn && !aborted()){
                log("The reference engines' speed changed by " + changeText(ch.log2) + ": measuring again");
                SpeedMeasurement sm2 = measureSpeed(m.speedRefs, so2);
                SpeedChange ch2 = speedChange(d.speedStart, sm2);
                if (ch2.ok && std::fabs(ch2.log2) < std::fabs(ch.log2)){ sm = sm2; ch = ch2; }
            }
            double cpuB = e.alive() ? e.cpuSeconds() : -1;
            if (cpuA >= 0 && cpuB >= cpuA){ idleCpu += cpuB - cpuA; idleWall += secondsSince(wallA); }
            c.referenceOk = ch.ok; c.referenceLog2 = ch.log2;
            if (atEnd) d.speedEnd = sm;
        }
        d.speedChecks.push_back(c);
        updateSpeedTerm();
    };
    // CPU time over search time, counted around the searches only (and, on Apple
    // silicon, the part of it on performance cores)
    double cpuUsed = 0, wallUsed = 0, pUsed = 0, pCpuUsed = 0;
    auto lastCheck = clk::now();
    std::string name;
    for (;;){
        size_t askedBefore = s.responses().size();
        int idx;
        while ((idx = s.next()) >= 0){
            if (aborted()) break;
            if (secondsSince(lastCheck) >= m.checkEverySec){
                log("Checking the speed...");
                check(false);
                lastCheck = clk::now();
                if (s.done()) break;                               // the interval may have changed
            }
            const EloItem& it = m.items[idx];
            EloAnswer a;
            a.id = it.id; a.category = it.category;
            std::string best;
            bool crashed = false;
            double cpu0 = -1, cpu1 = -1, p0 = -1, p1 = -1;
            for (int attempt = 0; attempt < 4; attempt++){
                if (!e.alive()){                                  // restart after a crash
                    crashed = true;
                    e.quit();
                    if (!e.start(cfg)) break;
                    d.placementApplied = d.placementApplied && e.placementApplied();
                    configure(e, m);
                }
                SuspendMark sm = suspendMark();
                e.newGame();
                e.setPosition(it.fen, {});
                cpu0 = e.cpuSeconds(); p0 = e.pCoreSeconds();
                best = e.goMovetimeStrict(movetime, m.graceMs);
                bool alive = e.alive();                           // a search the engine died in isn't counted
                cpu1 = alive ? e.cpuSeconds() : -1; p1 = alive ? e.pCoreSeconds() : -1;
                if (suspendedMsSince(sm) > 500) continue;          // the computer slept: ask again
                break;
            }
            EngineInfo inf = e.lastInfo();
            a.depth = inf.depth; a.nodes = inf.nodes; a.timeMs = inf.timeMs;
            a.wallMs = e.lastSearchWallMs(); a.stopped = e.lastSearchStopped();
            if (cpu0 >= 0 && cpu1 >= cpu0){
                cpuUsed += cpu1 - cpu0; wallUsed += a.wallMs / 1000.0;
                if (p0 >= 0 && p1 >= p0){ pUsed += p1 - p0; pCpuUsed += cpu1 - cpu0; }
            }
            Position p = Position::fromFEN(it.fen);
            Move mv;
            if (!best.empty() && p.uciToMove(best, mv)){
                a.move = p.moveToUCI(mv);                          // one castling notation
                a.solved = std::find(it.ok.begin(), it.ok.end(), a.move) != it.ok.end();
            } else {
                a.move = best;
                a.error = best.empty() ? (crashed || !e.alive() ? "crash" : "no move") : "illegal move";
            }
            answers.push_back(a);
            s.record(idx, a.solved);
            if (opt.progress){
                EloProgress pr;
                pr.answered = (int)s.responses().size(); pr.planned = planned;
                pr.elo = s.elo(); pr.se = s.eloSe(); pr.speedErrorElo = s.speedErrorElo(); pr.lastId = it.id;
                opt.progress(pr);
            }
        }
        if (aborted()){ e.quit(); fail.error = "aborted"; return fail; }
        if (s.responses().empty()){ e.quit(); fail.error = "the engine answered no position"; return fail; }
        // the speed again, to catch throttling or other load that started during the test
        log("Checking the speed again...");
        if (!e.alive() && e.start(cfg)){ configure(e, m); d.placementApplied = d.placementApplied && e.placementApplied(); }
        check(true);
        lastCheck = clk::now();
        // with the final speed term the test may no longer be over (it stopped "outside the
        // range" with an interval that has since grown): then it goes on
        if (s.done() || aborted() || s.responses().size() == askedBefore) break;
        log("The interval grew with the last speed check: more positions...");
    }
    name = e.name();
    e.quit();
    if (aborted()){ fail.error = "aborted"; return fail; }
    if (wallUsed > 1) d.cpuRatio = cpuUsed / wallUsed;
    if (pCpuUsed > 1) d.pCoreShare = std::min(1.0, pUsed / pCpuUsed);
    if (idleWall > 0.5) d.idleCpu = idleCpu / idleWall;
    const EloSpeedCheck& last = d.speedChecks.back();
    if (!last.ownOk && !last.referenceOk)
        d.warnings.push_back("the speed could not be checked after the test, so a change of speed during it may have gone unseen");
    EloResult r = eloResultFrom(m, s, d);
    r.adaptive = opt.adaptive;
    r.engineName = name;
    r.userNps = nps; r.factor = (double)movetime / m.baseMovetimeMs; r.movetimeMs = movetime;
    r.answers = answers;
    applyChecks(m, r);
    return r;
}

// ---------------------------------------------------------------- reports
std::string eloRangeWarning(const EloResult& r, const EloModel& m){
    if (!r.hasNumber() || m.reliableFromElo <= 0 || r.elo >= m.reliableFromElo) return "";
    return "Below about " + std::to_string(std::lround(m.reliableFromElo)) + " Elo this model is less accurate "
           "(fewer calibration engines that weak): the interval is wider there.";
}

// "above the calibrated range: at least 3487", or, when the estimate is past the end
// but its interval reaches back into the range, "past the top of the calibrated
// range (3487): at least 3371"
static std::string boundText(double rangeMin, double rangeMax, EloStatus st, double bound){
    auto f0 = [](double x){ return std::to_string(std::lround(x)); };
    if (st == EloStatus::AboveRange)
        return bound >= rangeMax ? "above the calibrated range: at least " + f0(bound)
                                 : "past the top of the calibrated range (" + f0(rangeMax) + "): at least " + f0(bound);
    return bound <= rangeMin ? "below the calibrated range: at most " + f0(bound)
                             : "past the bottom of the calibrated range (" + f0(rangeMin) + "): at most " + f0(bound);
}

std::string eloProgressText(const EloProgress& p, const EloModel& m){
    double half = eloCi95(m, p.elo, p.se, p.speedErrorElo);
    EloRangeStatus rs = eloRangeStatus(m, p.elo, half);
    if (rs.status != EloStatus::Ok) return boundText(m.rangeMin, m.rangeMax, rs.status, rs.bound);
    return std::to_string(std::lround(p.elo)) + " +/- " + std::to_string(std::lround(half));
}

std::string eloSpeedSource(const EloResult& r){
    if (r.diag.referenceSpeed) return "speed measured with the reference engines";
    if (r.userNps > 0) return "speed from the internal benchmark";
    return "move time set by hand";
}

std::string eloHeadline(const EloResult& r){
    if (!r.ok) return r.error.empty() ? "no result" : r.error;
    switch (r.status){
        case EloStatus::Ok:
            return std::to_string(std::lround(r.elo)) + " +/- " + std::to_string(std::lround(r.ci95)) + " (95%)";
        case EloStatus::AboveRange:
        case EloStatus::BelowRange:
            return boundText(r.rangeMin, r.rangeMax, r.status, r.bound);
        case EloStatus::Unreliable:
            return "no estimate: " + (r.diag.failures.empty() ? std::string("the run failed a check") : r.diag.failures[0]);
    }
    return "";
}

static nlohmann::json speedJson(const SpeedMeasurement& s){
    json eng = json::array();
    for (auto& e : s.engines){
        json x = {{"id", e.id}, {"speed", e.speed}, {"positions", e.positions}};
        if (e.pCoreShare >= 0) x["pCoreShare"] = e.pCoreShare;
        if (!e.error.empty()) x["error"] = e.error;
        eng.push_back(x);
    }
    return {{"ok", s.ok}, {"speed", s.speed}, {"spreadLog2", s.spreadLog2}, {"seconds", s.seconds}, {"engines", eng}};
}

std::string eloResultJson(const EloResult& r, const EloModel& m){
    bool num = r.hasNumber();
    json groups = json::object();
    if (num) for (auto& [g, v] : r.groups) groups[g] = {{"elo", std::lround(v.first)}, {"positions", v.second}};
    json ans = json::array();
    for (auto& a : r.answers){
        json x = {{"id", a.id}, {"move", a.move}, {"solved", a.solved}, {"depth", a.depth}, {"nodes", a.nodes},
                  {"timeMs", a.timeMs}, {"wallMs", a.wallMs}};
        if (a.stopped) x["stopped"] = true;
        if (!a.error.empty()) x["error"] = a.error;
        ans.push_back(x);
    }
    const EloDiagnostics& d = r.diag;
    json checks = json::array();
    for (auto& c : d.speedChecks){
        json x = {{"atSec", std::lround(c.atSec)}};
        x["reference"] = c.referenceOk ? json(c.referenceLog2) : json(nullptr);
        x["own"] = c.ownOk ? json(c.ownLog2) : json(nullptr);
        checks.push_back(x);
    }
    json diag = {{"placement", d.placement}, {"placementApplied", d.placementApplied},
                 {"referenceSpeed", d.referenceSpeed}, {"speedStart", speedJson(d.speedStart)},
                 {"speedEnd", speedJson(d.speedEnd)}, {"speedChecks", checks}, {"ownProbe", d.ownProbe},
                 {"driftLog2", d.driftLog2}, {"driftRmsLog2", d.driftRmsLog2}, {"speedErrorElo", std::lround(d.speedErrorElo)},
                 {"timeUse", d.timeUse}, {"cpuRatio", d.cpuRatio}, {"idleCpu", d.idleCpu}, {"pCoreShare", d.pCoreShare},
                 {"clampLog2", d.clampLog2}, {"clampCorrectionElo", std::lround(d.clampCorrectionElo)},
                 {"errors", d.errors}, {"stops", d.stops},
                 {"warnings", d.warnings}, {"failures", d.failures}, {"eloUnclamped", std::lround(r.elo)},
                 {"ci95Unclamped", std::lround(r.ci95)}};
    json j = {{"schema", 2}, {"engine", r.engineName}, {"status", eloStatusName(r.status)}, {"headline", eloHeadline(r)},
              {"elo", num ? json(std::lround(r.elo)) : json(nullptr)}, {"ci95", num ? json(std::lround(r.ci95)) : json(nullptr)},
              {"bound", (r.status == EloStatus::AboveRange || r.status == EloStatus::BelowRange) ? json(std::lround(r.bound)) : json(nullptr)},
              {"range", {{"min", m.rangeMin}, {"max", m.rangeMax}}},
              {"seMeasurement", std::lround(r.seMeasurement)}, {"modelErrorElo", std::lround(r.modelErrorElo)},
              {"scale", "CCRL Blitz (single CPU) equivalent"},
              {"mode", r.adaptive ? "quick" : "full"}, {"positions", r.answered}, {"solved", r.solved},
              {"movetimeMs", r.movetimeMs}, {"hardwareFactor", r.factor}, {"nps", r.userNps},
              {"modelVersion", m.version}, {"modelError", m.validationText}, {"warning", eloRangeWarning(r, m)},
              {"groups", groups}, {"diagnostics", diag},
              {"note", "estimated from test positions, not from games; time management is not measured"},
              {"answers", ans}};
    return j.dump(2);
}

std::string eloResultMarkdown(const EloResult& r, const EloModel& m){
    std::ostringstream o;
    o << "# Elo estimate: " << r.engineName << "\n\n**" << eloHeadline(r) << "**";
    if (r.hasNumber()) o << ", CCRL Blitz (single CPU) equivalent";
    o << "\n\nThis is an estimate from " << r.answered << " test positions (" << (r.adaptive ? "quick, adaptive" : "full")
      << " mode), not a rating from games: time management and other game skills are not measured.\n\n";
    if (r.hasNumber()){
        o << "| Category | Elo | Positions |\n|---|---|---|\n";
        for (auto& [g, v] : r.groups) o << "| " << g << " | " << std::lround(v.first) << " | " << v.second << " |\n";
        o << "\n";
    }
    if (r.status == EloStatus::AboveRange || r.status == EloStatus::BelowRange)
        o << "The calibrated range of this model is " << std::lround(m.rangeMin) << "-" << std::lround(m.rangeMax)
          << ": outside it the test can tell that an engine is stronger (or weaker), not by how much.\n\n";
    o << "Solved " << r.solved << " of " << r.answered << ". Move time " << r.movetimeMs << " ms per position ("
      << eloSpeedSource(r)
      << "; " << r.diag.placement << ").\n";
    for (auto& f : r.diag.failures) o << "\n**Failed check:** " << f << "\n";
    for (auto& w : r.diag.warnings) o << "\n**Warning:** " << w << "\n";
    std::string warn = eloRangeWarning(r, m);
    if (!warn.empty()) o << "\n**Warning:** " << warn << "\n";
    if (!m.validationText.empty()) o << "\nModel accuracy: " << m.validationText << ".\n";
    return o.str();
}
