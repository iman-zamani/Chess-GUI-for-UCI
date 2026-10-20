/* Chess-GUI-for-UCI — keeping the computer awake, and noticing when it slept. GPL-2.0 */
#pragma once
#include <string>

// While alive, asks the OS not to idle-sleep the system (macOS power
// assertion, Windows SetThreadExecutionState; nothing on other systems).
// A closed laptop lid can still put the machine to sleep.
class KeepAwake {
public:
    explicit KeepAwake(const std::string& reason);
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
private:
    unsigned id_ = 0;
    bool held_ = false;
};

// Milliseconds the system spent suspended since `mark` (from suspendMark()):
// the difference between a clock that counts sleep and one that doesn't.
// 0 where the OS offers no such pair.
struct SuspendMark { long long withSleepMs = 0, withoutSleepMs = 0; };
SuspendMark suspendMark();
long long suspendedMsSince(const SuspendMark& mark);
// Tests: the next suspendedMsSince() call reports `ms` more than it measured.
void injectSuspendForTests(long long ms);
