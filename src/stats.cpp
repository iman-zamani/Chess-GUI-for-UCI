/* Chess-GUI-for-UCI — match statistics: Elo, LOS, SPRT. GPL-2.0
 * See stats.hpp for the conventions and the references. */
#include "stats.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

static const double kZ95 = 1.959963984540054;
static const double kNeloScale = 800.0 / std::log(10.0);

void PentaCounts::addPair(double a, double b){
    double s = a + b;
    if (s < 0.25) ll++;
    else if (s < 0.75) ld++;
    else if (s < 1.25){ if (a == 0.5) dd++; else wl++; }
    else if (s < 1.75) wd++;
    else ww++;
}

static double scoreToElo(double s){ return -400.0 * std::log10(1.0 / s - 1.0); }

// Shared by the per-game and per-pair estimates: `n` samples with mean `s` and
// variance `var`; `pairFactor` is 2 for pairs (normalized Elo uses the
// per-game standard deviation).
static EloEstimate estimate(double n, double s, double var, double pairFactor){
    EloEstimate e;
    e.score = s;
    if (n <= 0 || s <= 0 || s >= 1 || var <= 0) return e;
    double perN = var / n;
    double hi = s + kZ95 * std::sqrt(perN), lo = s - kZ95 * std::sqrt(perN);
    e.elo = scoreToElo(s);
    e.eloError = (scoreToElo(std::min(hi, 1 - 1e-12)) - scoreToElo(std::max(lo, 1e-12))) / 2.0;
    auto nelo = [&](double x){ return (x - 0.5) / std::sqrt(pairFactor * var) * kNeloScale; };
    e.nelo = nelo(s);
    e.neloError = (nelo(hi) - nelo(lo)) / 2.0;
    e.los = (1 - std::erf(-(s - 0.5) / std::sqrt(2.0 * perN))) / 2.0;
    e.valid = true;
    return e;
}

EloEstimate eloFromWdl(const WdlCounts& c){
    double n = c.games();
    if (n <= 0) return EloEstimate{};
    double W = c.wins / n, D = c.draws / n, L = c.losses / n;
    double s = W + 0.5 * D;
    double var = W * (1 - s) * (1 - s) + D * (0.5 - s) * (0.5 - s) + L * s * s;
    return estimate(n, s, var, 1.0);
}

EloEstimate eloFromPenta(const PentaCounts& c){
    double n = c.pairs();
    if (n <= 0) return EloEstimate{};
    double WW = c.ww / n, WD = c.wd / n, WLDD = (c.wl + c.dd) / n, LD = c.ld / n, LL = c.ll / n;
    double s = WW + 0.75 * WD + 0.5 * WLDD + 0.25 * LD;
    double var = WW * (1 - s) * (1 - s) + WD * (0.75 - s) * (0.75 - s) + WLDD * (0.5 - s) * (0.5 - s)
               + LD * (0.25 - s) * (0.25 - s) + LL * s * s;
    return estimate(n, s, var, 2.0);
}

// ---------------------------------------------------------------- SPRT
double SprtConfig::lowerBound() const{ return std::log(beta / (1 - alpha)); }
double SprtConfig::upperBound() const{ return std::log((1 - beta) / alpha); }
std::string SprtConfig::validate() const{
    if (!(elo0 < elo1)) return "elo0 must be less than elo1";
    if (!(alpha > 0 && alpha < 1)) return "alpha must be between 0 and 1";
    if (!(beta > 0 && beta < 1)) return "beta must be between 0 and 1";
    if (alpha + beta >= 1) return "alpha + beta must be less than 1";
    return "";
}
const char* sprtModelName(SprtModel m){ return m == SprtModel::Logistic ? "logistic" : "normalized"; }

SprtOutcome sprtOutcome(const SprtConfig& cfg, double llr){
    if (llr >= cfg.upperBound()) return SprtOutcome::AcceptH1;
    if (llr <= cfg.lowerBound()) return SprtOutcome::AcceptH0;
    return SprtOutcome::Continue;
}

// ITP root finder: I. F. D. Oliveira and R. H. C. Takahashi, "An Enhancement
// of the Bisection Method Average Performance Preserving Minmax Optimality",
// ACM TOMS 47(1), 2021. Same parameters as fastchess, so the LLR values agree.
template <class F>
static double itpRoot(F f, double a, double b, double fa, double fb, double eps){
    const double k1 = 0.1, k2 = 2.0, n0 = 0.99;
    if (fa > 0){ std::swap(a, b); std::swap(fa, fb); }
    double nHalf = std::ceil(std::log2(std::abs(b - a) / (2.0 * eps)));
    double nMax = nHalf + n0;
    for (int i = 0; std::abs(b - a) > 2.0 * eps; i++){
        double xHalf = (a + b) / 2.0;
        double r = eps * std::pow(2.0, nMax - i) - (b - a) / 2.0;
        double delta = k1 * std::pow(b - a, k2);
        double xf = (fb * a - fa * b) / (fb - fa);
        double sigma = (xHalf - xf) / std::abs(xHalf - xf);
        double xt = delta <= std::abs(xHalf - xf) ? xf + sigma * delta : xHalf;
        double x = std::abs(xt - xHalf) <= r ? xt : xHalf - sigma * r;
        double fx = f(x);
        if (fx == 0){ a = b = x; }
        else if (fx < 0){ a = x; fa = fx; }
        else { b = x; fb = fx; }
    }
    return (a + b) / 2.0;
}

template <size_t N>
using Arr = std::array<double, N>;

template <size_t N>
static double meanOf(const Arr<N>& x, const Arr<N>& p){
    double m = 0; for (size_t i = 0; i < N; i++) m += x[i] * p[i]; return m;
}

// Logistic Elo: the ML distribution with expected score s, given the
// empirical one (Van den Bergh, comparing_approximations.pdf, Prop. 1.1).
template <size_t N>
static double llrLogistic(double total, const Arr<N>& a, const Arr<N>& ph, double s0, double s1){
    auto mle = [&](double s){
        double lo = -1.0 / (a[N - 1] - s), hi = -1.0 / (a[0] - s);
        double theta = itpRoot([&](double x){
            double r = 0; for (size_t i = 0; i < N; i++) r += ph[i] * (a[i] - s) / (1.0 + x * (a[i] - s)); return r;
        }, lo, hi, INFINITY, -INFINITY, 1e-3);
        Arr<N> p; for (size_t i = 0; i < N; i++) p[i] = ph[i] / (1 + theta * (a[i] - s));
        return p;
    };
    Arr<N> p0 = mle(s0), p1 = mle(s1), lpr;
    for (size_t i = 0; i < N; i++) lpr[i] = std::log(p1[i]) - std::log(p0[i]);
    return total * meanOf(lpr, ph);
}

// Normalized Elo: the ML distribution with t = (mu - 0.5) / sigma
// (Van den Bergh, normalized_elo_practical.pdf, section 4.1), by iteration
// from the uniform distribution.
template <size_t N>
static double llrNormalized(double total, const Arr<N>& a, const Arr<N>& ph, double t0, double t1){
    auto mle = [&](double muRef, double t){
        Arr<N> p; p.fill(1.0 / N);
        for (int it = 0; it < 10; it++){
            double mu = meanOf(a, p), var = 0;
            for (size_t i = 0; i < N; i++) var += p[i] * (a[i] - mu) * (a[i] - mu);
            double sd = std::sqrt(var);
            Arr<N> phi;
            for (size_t i = 0; i < N; i++)
                phi[i] = a[i] - muRef - 0.5 * t * sd * (1.0 + ((a[i] - mu) / sd) * ((a[i] - mu) / sd));
            double u = *std::min_element(phi.begin(), phi.end()), v = *std::max_element(phi.begin(), phi.end());
            double theta = itpRoot([&](double x){
                double r = 0; for (size_t i = 0; i < N; i++) r += ph[i] * phi[i] / (1.0 + x * phi[i]); return r;
            }, -1.0 / v, -1.0 / u, INFINITY, -INFINITY, 1e-7);
            double maxDiff = 0;
            for (size_t i = 0; i < N; i++){
                double np = ph[i] / (1.0 + theta * phi[i]);
                maxDiff = std::max(maxDiff, std::abs(np - p[i]));
                p[i] = np;
            }
            if (maxDiff < 1e-4) break;
        }
        return p;
    };
    Arr<N> p0 = mle(0.5, t0), p1 = mle(0.5, t1), lpr;
    for (size_t i = 0; i < N; i++) lpr[i] = std::log(p1[i]) - std::log(p0[i]);
    return total * meanOf(lpr, ph);
}

static double eloToScore(double elo){ return 1 / (1 + std::pow(10, -elo / 400)); }
static double regularize(int v){ return v == 0 ? 1e-3 : (double)v; }   // empty cells would break the MLE

double sprtLlr(const SprtConfig& cfg, const WdlCounts& c){
    if (c.games() == 0) return 0;
    double L = regularize(c.losses), D = regularize(c.draws), W = regularize(c.wins), t = L + D + W;
    Arr<3> scores{0.0, 0.5, 1.0}, ph{L / t, D / t, W / t};
    if (cfg.model == SprtModel::Normalized)
        return llrNormalized(t, scores, ph, cfg.elo0 / kNeloScale, cfg.elo1 / kNeloScale);
    return llrLogistic(t, scores, ph, eloToScore(cfg.elo0), eloToScore(cfg.elo1));
}

double sprtLlr(const SprtConfig& cfg, const PentaCounts& c){
    if (c.pairs() == 0) return 0;
    double LL = regularize(c.ll), LD = regularize(c.ld), M = regularize(c.wl + c.dd), WD = regularize(c.wd),
           WW = regularize(c.ww), t = LL + LD + M + WD + WW;
    Arr<5> scores{0.0, 0.25, 0.5, 0.75, 1.0}, ph{LL / t, LD / t, M / t, WD / t, WW / t};
    if (cfg.model == SprtModel::Normalized)
        return llrNormalized(t, scores, ph, std::sqrt(2.0) * cfg.elo0 / kNeloScale, std::sqrt(2.0) * cfg.elo1 / kNeloScale);
    return llrLogistic(t, scores, ph, eloToScore(cfg.elo0), eloToScore(cfg.elo1));
}
