/* Chess-GUI-for-UCI — running engines on a fixed kind of CPU core. GPL-2.0
 *
 * Timed measurements (the Elo estimator) need the engine and the speed check
 * that sets its move time to run on the same kind of core, at a steady clock:
 *  - Linux, Windows: the process is pinned to one logical CPU of the fastest
 *    class: the least busy one (counting its hyperthread siblings), never on
 *    the core of CPU 0, which takes most interrupts (unless it is the only
 *    one). A lock file per physical core keeps concurrent timed runs (two
 *    estimates at once) on different cores, not on two hyperthreads of one
 *    core. On Windows the engine and this program are also opted out of power
 *    throttling ("efficiency mode"), and on machines with more than 64 logical
 *    CPUs the CPU is chosen in this program's processor group.
 *  - macOS: threads can't be pinned, and the scheduler keeps a single busy
 *    thread on a performance core: placement is left to it, and the share of a
 *    process's CPU time spent on performance cores is measured instead
 *    (processPCoreSeconds). (The background band, which keeps a process on the
 *    efficiency cores, slowed engines down by 40-66% on an Apple M4, while the
 *    default stayed within 2%; it is available as CHESSGUI_CPU=background for
 *    tests only.)
 * The speed is checked before, during and after every timed run, so a change of
 * core or clock is detected rather than assumed away.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct CpuPlacement {
    int cpu = -1;                     // logical CPU to pin to (-1: none); Windows: index within `group`
    int group = 0;                    // Windows processor group of `cpu`
    bool darwinBackground = false;    // macOS: background band (efficiency cores)
    bool noPowerThrottling = false;   // Windows: opt out of EcoQoS
    bool shared = false;              // every suitable CPU was taken by another timed run
    std::string description;          // for reports, e.g. "pinned to CPU 7 (fastest core class, least busy)"
    std::shared_ptr<void> lock;       // held while any copy lives: other timed runs choose another CPU
    bool active() const { return cpu >= 0 || darwinBackground || noPowerThrottling; }
};

// The placement for timed engine runs on this machine (see above). Takes about
// 0.2 s (it measures how busy each CPU is).
// CHESSGUI_CPU=n pins to logical CPU n instead (Windows: counted over all
// processor groups), CHESSGUI_CPU=none turns placement off,
// CHESSGUI_CPU=background (macOS) uses the background band.
CpuPlacement timedRunPlacement();
// No placement at all (engines run wherever the OS puts them).
CpuPlacement noPlacement();

// One logical CPU a timed run may be pinned to (filled from the OS by
// timedRunPlacement; separate so the choice can be tested on any topology).
struct CpuCandidate {
    int cpu = 0, group = 0;           // logical CPU (Windows: index within its processor group)
    int core = 0;                     // its physical core: the lowest logical CPU on that core
    double speedClass = 0;            // higher = faster core class (P-core over E-core)
    double busy = 0, coreBusy = 0;    // over ~0.2 s: this CPU, and its whole core (hyperthread siblings)
    bool onCpu0Core = false;          // on the core of logical CPU 0, which takes most interrupts
};
// lock(group, core, taken): the lock of one physical core, held while the
// returned pointer lives; taken = another timed run holds it.
using CoreLock = std::function<std::shared_ptr<void>(int group, int core, bool& taken)>;
// The fastest class, not the core of CPU 0 (unless nothing else is left), then
// the least busy core and CPU (ties: the highest number), on a core no other
// timed run holds; sets p.cpu, p.group, p.lock, and p.shared when every such
// core is held (it then shares the least busy one).
void chooseCpu(std::vector<CpuCandidate> c, const CoreLock& lock, CpuPlacement& p);

// Applies a placement to the calling thread (in-process measurements).
bool applyToCurrentThread(const CpuPlacement& p);
// The logical CPU the calling thread runs on now (-1 where the OS can't tell).
int currentCpu();

// Total CPU time (user + system) of a child process in seconds, or -1 if
// unknown. POSIX: a process id. Windows: a process HANDLE.
double processCpuSeconds(long long pid);
double processCpuSecondsHandle(void* processHandle);
// macOS on Apple silicon: the part of processCpuSeconds spent on performance
// cores; -1 elsewhere (and on Macs with one kind of core).
double processPCoreSeconds(long long pid);
// Windows: opts this program's own process out of power throttling (it times the
// engine's searches); does nothing elsewhere.
void keepThisProcessUnthrottled();
