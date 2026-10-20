/* Chess-GUI-for-UCI — match statistics: Elo, LOS, SPRT. GPL-2.0
 *
 * Conventions follow fastchess (https://github.com/Disservin/fastchess,
 * v1.8.2-alpha, src/matchmaking/{elo,sprt}) so results can be compared
 * directly with it and with OpenBench:
 *  - Elo from the mean score s: -400 log10(1/s - 1); the 95% interval comes from
 *    s +/- 1.96 * sqrt(var / n), mapped through the same formula.
 *  - Pentanomial statistics count game pairs (same opening, colours swapped)
 *    by the pair's score 0, 0.5, 1, 1.5, 2 (LL, LD, WL+DD, WD, WW).
 *  - SPRT is the generalized SPRT with the exact maximum-likelihood estimates
 *    of Michel Van den Bergh: "Comparing the approximations for the generalized
 *    log likelihood ratio of a multinomial distribution" (logistic Elo) and
 *    "Comments on normalized Elo" (normalized Elo), see
 *    https://www.cantate.be/Fishtest/. Bounds: ln(beta/(1-alpha)), ln((1-beta)/alpha).
 */
#pragma once
#include <string>

struct WdlCounts {
    int wins = 0, draws = 0, losses = 0;     // from the first engine's point of view
    int games() const { return wins + draws + losses; }
};

struct PentaCounts {
    int ll = 0, ld = 0, wl = 0, dd = 0, wd = 0, ww = 0;   // pair outcomes for the first engine
    int pairs() const { return ll + ld + wl + dd + wd + ww; }
    // Adds the pair made of two game results (1 win, 0.5 draw, 0 loss).
    void addPair(double a, double b);
};

struct EloEstimate {
    bool valid = false;                      // false with no games, or a 0% / 100% score
    double score = 0;                        // mean score per game (0..1)
    double elo = 0, eloError = 0;            // logistic Elo difference, +/- 95% half-width
    double nelo = 0, neloError = 0;          // normalized Elo
    double los = 0;                          // likelihood of superiority (0..1)
};
EloEstimate eloFromWdl(const WdlCounts& c);
EloEstimate eloFromPenta(const PentaCounts& c);

enum class SprtModel { Normalized, Logistic };
enum class SprtOutcome { Continue, AcceptH0, AcceptH1 };

struct SprtConfig {
    double elo0 = 0, elo1 = 5;               // H0: elo = elo0, H1: elo = elo1
    double alpha = 0.05, beta = 0.05;
    SprtModel model = SprtModel::Normalized;
    double lowerBound() const;               // ln(beta / (1 - alpha))
    double upperBound() const;               // ln((1 - beta) / alpha)
    std::string validate() const;            // "" when usable
};

double sprtLlr(const SprtConfig& cfg, const WdlCounts& c);
double sprtLlr(const SprtConfig& cfg, const PentaCounts& c);
SprtOutcome sprtOutcome(const SprtConfig& cfg, double llr);
const char* sprtModelName(SprtModel m);      // "normalized" / "logistic"
