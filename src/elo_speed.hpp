/* Chess-GUI-for-UCI — how fast this computer runs chess engines, measured with
 * reference engines. GPL-2.0
 *
 * The Elo estimator gives every engine the move time the calibration engines
 * had on the calibration machine, scaled by how much faster this computer is.
 * That ratio is measured by replaying recorded searches of reference engines
 * that ship with the program (Stockfish 11 and Blunder 8.0.0, both calibration
 * engines): for each recorded position the reference engine searches exactly
 * what it searched during calibration ("go nodes N" or "go depth d"), and the
 * time it reports is compared with the time it took then. Speed = calibration
 * time / time here (median over positions, geometric mean over engines).
 *
 * The reference engines run with the same CPU placement as the engine under
 * test, after a warm-up that brings the core to a steady clock.
 */
#pragma once
#include "cpu_placement.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct SpeedReferenceItem {
    std::string fen;
    long long nodes = 0;          // replay "go nodes" (if > 0) ...
    int depth = 0;                // ... else "go depth"
    double calMs = 0;             // the engine's reported time for it on the calibration machine
};

struct SpeedReference {
    std::string id;               // e.g. "stockfish-11"
    std::string file;             // file name in Resources/reference/ (".exe" added on Windows)
    std::string idName;           // expected start of the engine's "id name"
    std::vector<SpeedReferenceItem> items;
};

struct SpeedSample {
    std::string id;
    double speed = 0;             // calibration time / time here (median); 0 = no measurement
    int positions = 0;            // replays that counted
    std::vector<std::pair<int, double>> replays;   // (item index, calibration time / time here)
    double pCoreShare = -1;       // Apple silicon: share of its CPU time on performance cores (-1: unknown)
    std::string error;            // why there is no measurement
};

struct SpeedMeasurement {
    bool ok = false;              // at least one reference engine measured
    std::vector<SpeedSample> engines;
    double speed = 0;             // geometric mean of the engines' speeds
    double spreadLog2 = 0;        // |log2| of the ratio between the fastest and slowest engine
    double seconds = 0;           // time the measurement took (warm-up included)
    std::string error;
};

struct SpeedOptions {
    CpuPlacement placement;
    double warmupSec = 30;        // replays before measuring, discarded (brings the core to its steady clock;
                                  // in a process of its own, so every measurement starts from a fresh one)
    int positions = 24;           // replays measured per engine
    std::atomic<bool>* abort = nullptr;
    std::function<void(const std::string&)> log;   // progress lines
};

// The reference engine's executable, or "" if it is not installed.
// Looked up as Resources/reference/<file> (see findResource).
std::string findReferenceEngine(const SpeedReference& ref);

// Replays the reference engines' recorded searches; see the file comment.
SpeedMeasurement measureSpeed(const std::vector<SpeedReference>& refs, const SpeedOptions& opt);

// How much the speed changed from one measurement to a later one, in log2
// units (negative = slower): per reference engine the median over the replays
// of the same positions in both, then the mean over the engines. Comparing the
// same positions matters: recorded times differ from position to position.
// ok = false when no engine has a position in common.
struct SpeedChange { bool ok = false; double log2 = 0; int positions = 0; };
SpeedChange speedChange(const SpeedMeasurement& from, const SpeedMeasurement& to);

// Median of a list (0 if empty). Exposed for tests.
double medianOf(std::vector<double> v);
