/* Chess-GUI-for-UCI — UCI compliance checker. GPL-2.0 */
#pragma once
#include "engine_config.hpp"
#include "uci_engine.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

enum class CheckStatus { Pass, Warn, Fail, Skip };
const char* checkStatusName(CheckStatus s);           // "PASS", "WARN", ...

struct CheckResult {
    std::string id;                  // stable identifier, e.g. "isready-during-search"
    std::string title;               // one line: what was checked
    CheckStatus status = CheckStatus::Pass;
    std::string detail;              // why it passed/failed, what the spec expects
    std::vector<UciLogLine> log;     // engine I/O behind the result
    long long ms = 0;                // time the check took
};

struct UciCheckOptions {
    bool quick = false;              // shorter time-management games
    std::vector<std::string> only;   // run just these check ids ("handshake" always runs)
    std::function<void(const CheckResult&)> progress;
    std::atomic<bool>* abort = nullptr;
};

// Check ids, in the order they run.
const std::vector<std::string>& uciCheckIds();

// Runs every check against the engine described by `cfg` (each check starts a
// fresh engine process where isolation matters) and returns the results in
// order. Never throws; an engine that crashes fails the check it crashed in.
std::vector<CheckResult> runUciChecks(const EngineConfig& cfg, const UciCheckOptions& opt);

// Summary counts.
struct CheckSummary { int pass = 0, warn = 0, fail = 0, skip = 0; };
CheckSummary summarize(const std::vector<CheckResult>& r);
// Plain-text report (one block per check, with the log excerpt of failures).
std::string checkReportText(const std::string& engineName, const std::vector<CheckResult>& r);
