/* Chess-GUI-for-UCI — position-based Elo estimator. GPL-2.0
 *
 * An engine answers test positions; each position has item parameters (a, b)
 * from a 2PL item response model fitted on calibration engines with known
 * CCRL Blitz ratings (tools/elo, docs/elo-estimator.md). The engine's ability
 * theta is the penalised maximum-likelihood estimate given which positions it
 * "solved" (its move is in the position's acceptable set). theta maps to Elo
 * linearly. This is an estimate from positions, not a rating from games: time
 * management, for one, is not measured.
 */
#pragma once
#include "engine_config.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct EloItem {
    std::string id, fen, category, group;   // group: tactics | positional | endgame
    double a = 1, b = 0;
    std::vector<std::string> ok;            // acceptable moves (UCI)
};

struct EloModel {
    int version = 0;
    double threshold = 0;                   // win-% loss that still counts as solved
    // protocol (must match the calibration runs)
    int baseMovetimeMs = 500;
    double calibNps = 0;                    // chess-cli bench on the calibration machine
    double factorMin = 0.25, factorMax = 4;
    int hashMb = 64, moveOverheadMs = 10, graceMs = 50;
    // theta -> Elo, prior, adaptive test
    double eloIntercept = 2500, eloSlope = 400;
    double priorMean = 0, priorSd = 3;
    double seTargetElo = 50;
    int maxItems = 300, minItems = 20;
    double errorRmse = 0;                   // cross-validated error, added to the CI
    struct ErrorBand { double from, to, rmse; };
    std::vector<ErrorBand> errorBands;      // the same by estimated Elo; errorRmse outside them
    std::string validationText;             // e.g. "on 32 known engines: median error 85 Elo"
    double reliableFromElo = 0;             // below this the cross-validated error is much larger (0: no limit)
    std::map<std::string, double> groupShare;
    std::vector<std::pair<std::string, double>> calibration;   // engine -> CCRL Elo (for the chart)
    std::vector<EloItem> items;

    bool load(const std::string& path, std::string* err = nullptr);
    bool loadJson(const std::string& text, std::string* err = nullptr);
    double toElo(double theta) const { return eloIntercept + eloSlope * theta; }
    double errorAt(double elo) const;       // model error for an engine estimated at this Elo
};

struct ThetaEstimate { double theta = 0, se = 0; };

// Penalised ML ability (Newton, prior N(priorMean, priorSd^2)); the same
// algorithm as tools/elo/irt.py estimate_theta. responses: (item index, solved).
ThetaEstimate estimateTheta(const EloModel& m, const std::vector<std::pair<int, bool>>& responses);
double itemInformation(const EloItem& it, double theta);

// The order positions are asked in. Adaptive: the unused position with the most
// Fisher information at the current estimate, from the category group furthest
// below its share; it stops once the Elo SE <= seTargetElo (after minItems) or
// after maxItems. Full: every position in file order.
class EloSession {
public:
    EloSession(const EloModel& m, bool adaptive);
    int next();                             // item index, or -1 when done
    void record(int item, bool solved);
    ThetaEstimate estimate() const { return est_; }
    double eloSe() const { return est_.se * model_.eloSlope; }
    const std::vector<std::pair<int, bool>>& responses() const { return resp_; }
    bool done() const;
private:
    const EloModel& model_;
    bool adaptive_;
    std::vector<bool> used_;
    std::vector<std::pair<int, bool>> resp_;
    ThetaEstimate est_;
    size_t cursor_ = 0;
};

struct EloAnswer {
    std::string id, category, move;
    bool solved = false;
    int depth = 0;
    long long nodes = 0, timeMs = 0;
};

struct EloResult {
    bool ok = false;
    std::string error, engineName;
    double elo = 0, ci95 = 0;               // CI includes the model's cross-validated error
    double seMeasurement = 0;               // Elo SE from the answers alone
    int answered = 0, solved = 0;
    bool adaptive = true;
    double userNps = 0, factor = 1;
    int movetimeMs = 0;
    std::map<std::string, std::pair<double, int>> groups;   // group -> (Elo, positions)
    std::vector<EloAnswer> answers;
};

struct EloProgress { int answered = 0, planned = 0; double elo = 0, se = 0; std::string lastId; };

struct EloOptions {
    bool adaptive = true;                   // Quick (adaptive) or Full
    double factor = 0;                      // 0 = measure (calibNps / this machine's NPS)
    std::function<void(const EloProgress&)> progress;
    std::atomic<bool>* abort = nullptr;
};

// Runs the engine on the positions with the calibration protocol: Threads=1,
// Hash, "Move Overhead"-style options set low, ucinewgame + isready before every
// position, go movetime T (T = base x factor), stop after T + grace.
EloResult runEloEstimate(const EngineConfig& engine, const EloModel& model, const EloOptions& opt);
// Scores a finished session (also used by tests).
EloResult eloResultFrom(const EloModel& model, const EloSession& s);

std::string eloResultJson(const EloResult& r, const EloModel& m);
std::string eloResultMarkdown(const EloResult& r, const EloModel& m);
// "" or a warning that the estimate lies where the model is known to be unreliable.
std::string eloRangeWarning(const EloResult& r, const EloModel& m);
