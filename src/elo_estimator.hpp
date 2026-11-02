/* Chess-GUI-for-UCI — position-based Elo estimator. GPL-2.0
 *
 * An engine answers test positions; each position has item parameters (a, b)
 * from a 2PL item response model fitted on calibration engines with known
 * CCRL Blitz ratings (tools/elo, docs/elo-estimator.md). The engine's ability
 * theta is the penalised maximum-likelihood estimate given which positions it
 * "solved" (its move is in the position's acceptable set). theta maps to Elo
 * linearly. This is an estimate from positions, not a rating from games: time
 * management, for one, is not measured.
 *
 * Every engine gets the move time the calibration engines had, scaled by this
 * computer's speed as measured with reference engines (elo_speed.hpp), and runs
 * on the same CPU placement as that measurement (cpu_placement.hpp). Results
 * outside the calibrated rating range, or from a run whose conditions failed a
 * check (speed drift, an engine that ignores the move time, ...), come without
 * a point estimate.
 */
#pragma once
#include "elo_speed.hpp"
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
    double calibNps = 0;                    // chess-cli bench on the calibration machine (fallback only)
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
    // the calibrated range: estimates outside it are reported as a bound (0: no limit)
    double rangeMin = 0, rangeMax = 0;
    // speed: reference engines and how well their speed stands for another engine's
    std::vector<SpeedReference> speedRefs;
    double warmupSec = 30;                  // reference replays before measuring
    int speedPositions = 24;                // replays measured per reference engine
    double speedSigmaLog2 = 0.35;           // spread of other engines' speed ratios around the references' (log2;
                                            // on ARM computers the model's sigmaLog2Arm64)
    double fallbackSigmaLog2 = 0.6;         // the same for the perft benchmark (no reference engines)
    double singleReferenceSigmaLog2 = 0.41; // the same when only one reference engine could be measured
    double eloPerDoubling = 155;            // estimate change per doubling of search time
    double eloPerDoublingErr = 33;          // its standard error
    int minMovetimeMs = 150;                // a faster machine still gets at least this (fixed overheads)
    double checkEverySec = 60;              // speed checks during the test (reference engines and the engine's own probe)
    int checkPositions = 4;                 // reference replays per engine at each check
    // checks on the run (see EloChecks)
    double driftWarn = 0.10, driftFail = 0.25;      // largest |log2| change of the speed during the test
    double timeUseWarn = 0.80, timeUseFail = 0.50;  // median (wall time / move time)
    double overrunWarn = 1.10, overrunFail = 1.25;  // the same, from above: it searches longer than it is told
    double cpuRatioMax = 1.35;              // engine CPU time / search wall time (more: it isn't single-threaded)
    double cpuRatioWarn = 0.80, cpuRatioMin = 0.60; // (less: it didn't get the CPU; warning, no number)
    double errorRateFail = 0.05;            // illegal moves, no move, crashes (calibration engines: none)
    double idleCpuFail = 0.20;              // engine CPU use while it waits for a command (busy-waiting)
    double pCoreWarn = 0.90, pCoreFail = 0.75;      // Apple silicon: share of the CPU time on performance cores
    std::map<std::string, double> groupShare;
    std::vector<std::pair<std::string, double>> calibration;   // engine -> CCRL Elo (for the chart)
    std::vector<EloItem> items;

    bool load(const std::string& path, std::string* err = nullptr);
    bool loadJson(const std::string& text, std::string* err = nullptr);
    double toElo(double theta) const { return eloIntercept + eloSlope * theta; }
    double errorAt(double elo) const;       // model error for an engine estimated at this Elo
};

struct ThetaEstimate { double theta = 0, se = 0; };

// Half-width of the 95% interval: the measurement SE, the model's error at this
// Elo and the speed term, combined.
double eloCi95(const EloModel& m, double elo, double seElo, double speedErrorElo);

enum class EloStatus { Ok, AboveRange, BelowRange, Unreliable };
// Where an estimate with this 95% half-width stands against the calibrated
// range, the one rule for the result, the early stop and the running estimate:
// an estimate past an end of the range is a bound, not a number (the model
// extrapolates there): "at least" the lower end of its interval, but never
// more than the range's top end (and "at most" the upper end, never less
// than the bottom end).
struct EloRangeStatus { EloStatus status = EloStatus::Ok; double bound = 0; };
EloRangeStatus eloRangeStatus(const EloModel& m, double elo, double ci95);

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
    // the speed term of the interval, and a correction of the estimate (the move time
    // clamp), as known so far: the early stop uses the same interval as the result
    void setSpeed(double errorElo, double offsetElo){ speedErrorElo_ = errorElo; offsetElo_ = offsetElo; }
    double speedErrorElo() const { return speedErrorElo_; }
    double offsetElo() const { return offsetElo_; }
    double elo() const { return model_.toElo(est_.theta) + offsetElo_; }
    bool done() const;
private:
    const EloModel& model_;
    bool adaptive_;
    double speedErrorElo_ = 0, offsetElo_ = 0;
    std::vector<bool> used_;
    std::vector<std::pair<int, bool>> resp_;
    ThetaEstimate est_;
    size_t cursor_ = 0;
};

struct EloAnswer {
    std::string id, category, move;
    bool solved = false;
    int depth = 0;
    long long nodes = 0, timeMs = 0;        // the engine's last info line
    long long wallMs = 0;                   // go -> bestmove
    bool stopped = false;                   // needed a "stop"
    std::string error;                      // "illegal move", "no move", "crash"
};

// A speed check during or after the test, against the start: the reference
// engines replaying the same positions, and the engine itself repeating one
// fixed search (its own speed, on whatever core it runs).
struct EloSpeedCheck {
    double atSec = 0;                       // seconds into the test
    bool referenceOk = false, ownOk = false;
    double referenceLog2 = 0, ownLog2 = 0;  // log2(speed now / speed at the start)
};

// How the run went: the conditions the estimate depends on, and the checks on them.
struct EloDiagnostics {
    std::string placement;                  // CpuPlacement::description
    bool placementApplied = true;
    SpeedMeasurement speedStart, speedEnd;  // reference engines before and after the test
    bool referenceSpeed = false;            // false: perft benchmark fallback (approximate)
    std::vector<EloSpeedCheck> speedChecks; // during the test and at the end
    std::string ownProbe;                   // the engine's own fixed search ("go nodes N" / "go depth d"), "" if none
    double driftLog2 = 0;                   // the largest change of speed seen by the checks (log2, signed)
    double driftRmsLog2 = 0;                // RMS of the changes (part of the speed term)
    double speedErrorElo = 0;               // the speed term in the interval
    double timeUse = 0;                     // median wall time / move time
    double cpuRatio = -1;                   // engine CPU time / search wall time (-1: unknown)
    double idleCpu = -1;                    // engine CPU time / wall time while it waited (reference checks; -1: unknown)
    double pCoreShare = -1;                 // Apple silicon: share of the engine's search CPU time on performance cores
    double clampLog2 = 0;                   // log2(move time / the one this computer's speed calls for)
    double clampCorrectionElo = 0;          // added to the estimate for it (eloPerDoubling x clampLog2, negated)
    int errors = 0, stops = 0;
    std::vector<std::string> warnings;      // shown with the result
    std::vector<std::string> failures;      // why there is no number (status Unreliable)
};

struct EloResult {
    bool ok = false;                        // the test ran (see status for whether there is a number)
    std::string error, engineName;
    EloStatus status = EloStatus::Ok;
    double elo = 0, ci95 = 0;               // CI includes the model's error and the speed error
    double bound = 0;                       // AboveRange: "at least", BelowRange: "at most" (see eloRangeStatus)
    double rangeMin = 0, rangeMax = 0;      // the model's calibrated range
    double seMeasurement = 0;               // Elo SE from the answers alone
    double modelErrorElo = 0;               // the model's cross-validated error at this estimate
    int answered = 0, solved = 0;
    bool adaptive = true;
    double userNps = 0, factor = 1;         // factor = move time / base move time
    int movetimeMs = 0;
    std::map<std::string, std::pair<double, int>> groups;   // group -> (Elo, positions)
    std::vector<EloAnswer> answers;
    EloDiagnostics diag;
    bool hasNumber() const { return ok && status == EloStatus::Ok; }
};

struct EloProgress { int answered = 0, planned = 0; double elo = 0, se = 0, speedErrorElo = 0; std::string lastId; };

struct EloOptions {
    bool adaptive = true;                   // Quick (adaptive) or Full
    double factor = 0;                      // 0 = measure; else the move-time factor, no speed measurement
    bool placement = true;                  // run on timedRunPlacement() (see cpu_placement.hpp)
    std::function<void(const EloProgress&)> progress;
    std::function<void(const std::string&)> log;     // status lines (speed measurement, checks)
    std::atomic<bool>* abort = nullptr;
};

// Runs the engine on the positions with the calibration protocol: Threads=1,
// Hash, "Move Overhead"-style options set low, ucinewgame + isready before every
// position, go movetime T, stop after T + grace. T = base / speed, the speed
// measured with the reference engines on the same CPU placement before the
// test (and checked again after it).
EloResult runEloEstimate(const EngineConfig& engine, const EloModel& model, const EloOptions& opt);
// Scores a finished session (also used by tests): estimate, interval, range
// status. Checks on the run (applyChecks) come on top.
EloResult eloResultFrom(const EloModel& model, const EloSession& s);
EloResult eloResultFrom(const EloModel& model, const EloSession& s, const EloDiagnostics& diag);
// The checks on a finished run: fills diag.warnings / diag.failures from the
// answers and the speed measurements and sets status Unreliable on a failure.
void applyChecks(const EloModel& model, EloResult& r);
std::string eloStatusName(EloStatus s);     // "ok", "above_range", "below_range", "unreliable"
// One line for the result: "2877 +/- 173", "above 3700", "unreliable: ..."
std::string eloHeadline(const EloResult& r);
// Where the move time came from: "speed measured with the reference engines", "speed from the
// internal benchmark" or "move time set by hand".
std::string eloSpeedSource(const EloResult& r);
// The running estimate while the test goes on, e.g. "2950 +/- 260", or the bound
// (eloRangeStatus) once it is past an end of the range: "at least 3487".
std::string eloProgressText(const EloProgress& p, const EloModel& m);

std::string eloResultJson(const EloResult& r, const EloModel& m);
std::string eloResultMarkdown(const EloResult& r, const EloModel& m);
// "" or a warning that the estimate lies where the model is known to be unreliable.
std::string eloRangeWarning(const EloResult& r, const EloModel& m);
