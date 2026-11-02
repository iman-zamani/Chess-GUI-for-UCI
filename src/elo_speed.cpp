/* Chess-GUI-for-UCI — how fast this computer runs chess engines, measured with
 * reference engines. GPL-2.0 */
#include "elo_speed.hpp"
#include "engine_config.hpp"
#include "uci_engine.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

double medianOf(std::vector<double> v){
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::string findReferenceEngine(const SpeedReference& ref){
#ifdef _WIN32
    return findResource("reference/" + ref.file + ".exe");
#else
    return findResource("reference/" + ref.file);
#endif
}

static bool startsWithNoCase(const std::string& s, const std::string& prefix){
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
    return true;
}

// One recorded search, repeated. The time is the engine's own report for the
// search (the same quantity that was recorded during calibration).
static bool replay(UciEngine& e, const SpeedReferenceItem& it, double& ms){
    e.newGame();
    e.setPosition(it.fen, {});
    int timeout = (int)std::max(10000.0, it.calMs * 20);
    std::string best = it.nodes > 0 ? e.goNodes(it.nodes, timeout) : e.goDepth(it.depth, timeout);
    if (best.empty() || e.lastSearchStopped()) return false;
    EngineInfo inf = e.lastInfo();
    if (it.nodes <= 0 && inf.depth < it.depth) return false;
    ms = inf.timeMs > 0 ? (double)inf.timeMs : (double)e.lastSearchWallMs();
    return ms > 0;
}

SpeedMeasurement measureSpeed(const std::vector<SpeedReference>& refs, const SpeedOptions& opt){
    using clk = std::chrono::steady_clock;
    auto secondsSince = [](clk::time_point t){ return std::chrono::duration<double>(clk::now() - t).count(); };
    auto aborted = [&]{ return opt.abort && *opt.abort; };
    auto log = [&](const std::string& s){ if (opt.log) opt.log(s); };
    SpeedMeasurement m;
    auto t0 = clk::now();
    bool warmedUp = false;
    for (auto& ref : refs){
        SpeedSample s;
        s.id = ref.id;
        std::string path = findReferenceEngine(ref);
        if (path.empty()){ s.error = "not installed (Resources/reference/" + ref.file + ")"; m.engines.push_back(s); continue; }
        UciEngine e;
        e.setPlacement(opt.placement);
        auto open = [&]{
            if (!e.start(path)){ s.error = "does not start: " + e.lastError(); return false; }
            if (!startsWithNoCase(e.name(), ref.idName)){
                s.error = "reports \"" + e.name() + "\", expected \"" + ref.idName + "\"";
                return false;
            }
            if (e.hasOption("Threads")) e.setOption("Threads", "1");
            if (e.hasOption("Hash")) e.setOption("Hash", "64");
            e.isReady();
            return true;
        };
        if (!open()){ m.engines.push_back(s); continue; }
        if (!warmedUp && opt.warmupSec > 0 && !ref.items.empty()){
            // warm-up in a process of its own: the measurement then starts from a fresh
            // process, like every later check (an engine that keeps history between
            // searches would otherwise search differently at the start than in the checks)
            log("Warming up the CPU core (" + std::to_string((int)opt.warmupSec) + " s)...");
            auto w = clk::now();
            for (size_t i = 0; secondsSince(w) < opt.warmupSec && !aborted() && e.alive(); i++){
                double ms;
                replay(e, ref.items[i % ref.items.size()], ms);
            }
            e.quit();
            if (!open()){ m.engines.push_back(s); continue; }
        }
        warmedUp = true;
        std::vector<double> ratios;
        double cpu0 = e.cpuSeconds(), p0 = e.pCoreSeconds();
        for (size_t i = 0; i < ref.items.size() && (int)ratios.size() < opt.positions && !aborted(); i++){
            double ms = 0;
            if (replay(e, ref.items[i], ms)){
                ratios.push_back(ref.items[i].calMs / ms);
                s.replays.push_back({(int)i, ratios.back()});
            }
        }
        double cpu1 = e.cpuSeconds(), p1 = e.pCoreSeconds();
        if (p0 >= 0 && p1 >= p0 && cpu1 - cpu0 > 0.05) s.pCoreShare = std::min(1.0, (p1 - p0) / (cpu1 - cpu0));
        s.positions = (int)ratios.size();
        if (s.positions * 2 >= std::min<int>(opt.positions, (int)ref.items.size()) && s.positions > 0)
            s.speed = medianOf(ratios);
        else s.error = "too few replays succeeded (" + std::to_string(s.positions) + ")";
        if (s.speed > 0) log(ref.id + ": " + std::to_string(s.speed).substr(0, 4) + "x the calibration machine");
        m.engines.push_back(s);
        if (aborted()) break;
    }
    double sumLog = 0, lo = 1e9, hi = 0;
    int n = 0;
    for (auto& s : m.engines) if (s.speed > 0){ sumLog += std::log(s.speed); lo = std::min(lo, s.speed); hi = std::max(hi, s.speed); n++; }
    m.ok = n > 0;
    if (m.ok){
        m.speed = std::exp(sumLog / n);
        m.spreadLog2 = std::log2(hi / lo);
    } else {
        m.error = aborted() ? "aborted" : "no reference engine could be measured";
        for (auto& s : m.engines) if (!s.error.empty()) m.error += "; " + s.id + ": " + s.error;
    }
    m.seconds = secondsSince(t0);
    return m;
}

SpeedChange speedChange(const SpeedMeasurement& from, const SpeedMeasurement& to){
    SpeedChange c;
    double sum = 0;
    int n = 0;
    for (auto& a : from.engines) for (auto& b : to.engines){
        if (a.id != b.id) continue;
        std::vector<double> d;
        for (auto& x : a.replays) for (auto& y : b.replays)
            if (x.first == y.first && x.second > 0 && y.second > 0) d.push_back(std::log2(y.second / x.second));
        if (d.empty()) continue;
        sum += medianOf(d);
        n++;
        c.positions += (int)d.size();
    }
    c.ok = n > 0;
    if (c.ok) c.log2 = sum / n;
    return c;
}
