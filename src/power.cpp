/* Chess-GUI-for-UCI — keeping the computer awake, and noticing when it slept. GPL-2.0 */
#include "power.hpp"
#include <atomic>
#include <ctime>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

KeepAwake::KeepAwake(const std::string& reason){
#ifdef __APPLE__
    CFStringRef r = CFStringCreateWithCString(kCFAllocatorDefault, reason.c_str(), kCFStringEncodingUTF8);
    IOPMAssertionID id = 0;
    held_ = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                        r, &id) == kIOReturnSuccess;
    id_ = id;
    CFRelease(r);
#elif defined(_WIN32)
    (void)reason;
    held_ = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
#else
    (void)reason;
#endif
}

KeepAwake::~KeepAwake(){
    if (!held_) return;
#ifdef __APPLE__
    IOPMAssertionRelease((IOPMAssertionID)id_);
#elif defined(_WIN32)
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

SuspendMark suspendMark(){
    SuspendMark m;
#ifdef __APPLE__
    // CLOCK_MONOTONIC keeps counting while asleep, CLOCK_UPTIME_RAW does not
    m.withSleepMs = (long long)(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000);
    m.withoutSleepMs = (long long)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000);
#elif defined(_WIN32)
    ULONGLONG unbiased = 0;                                   // 100 ns units, sleep excluded
    QueryUnbiasedInterruptTime(&unbiased);
    m.withSleepMs = (long long)GetTickCount64();
    m.withoutSleepMs = (long long)(unbiased / 10000);
#elif defined(CLOCK_BOOTTIME)
    timespec a{}, b{};
    clock_gettime(CLOCK_BOOTTIME, &a);                        // counts suspend
    clock_gettime(CLOCK_MONOTONIC, &b);                       // doesn't
    m.withSleepMs = a.tv_sec * 1000LL + a.tv_nsec / 1000000;
    m.withoutSleepMs = b.tv_sec * 1000LL + b.tv_nsec / 1000000;
#endif
    return m;
}

static std::atomic<long long> g_injected{0};
void injectSuspendForTests(long long ms){ g_injected = ms; }

long long suspendedMsSince(const SuspendMark& mark){
    SuspendMark now = suspendMark();
    long long d = (now.withSleepMs - mark.withSleepMs) - (now.withoutSleepMs - mark.withoutSleepMs);
    d += g_injected.exchange(0);
    return d > 0 ? d : 0;
}
