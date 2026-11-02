/* Chess-GUI-for-UCI — running engines on a fixed kind of CPU core. GPL-2.0 */
#include "cpu_placement.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <cctype>
#include <fstream>
#include <map>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <unistd.h>
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

CpuPlacement noPlacement(){
    CpuPlacement p;
    p.description = "no CPU placement";
    return p;
}

namespace {

// The lock file of one physical core, held while the placement lives (flock /
// LockFileEx; the OS releases it if the program dies). Null and taken = false
// when the file can't be used at all (locking then just doesn't happen).
std::shared_ptr<void> lockCore(int group, int core, bool& taken){
    taken = false;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) return nullptr;
    std::filesystem::path f = dir / ("chessgui-core-" + std::to_string(group) + "-" + std::to_string(core) + ".lock");
#ifdef _WIN32
    HANDLE h = CreateFileW(f.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);   // not inheritable: engines never hold it
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    OVERLAPPED ov{};
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)){
        taken = GetLastError() == ERROR_LOCK_VIOLATION;
        CloseHandle(h);
        return nullptr;
    }
    return std::shared_ptr<void>(h, [](void* x){ CloseHandle((HANDLE)x); });
#else
    int fd = open(f.string().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) return nullptr;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0){
        taken = errno == EWOULDBLOCK;
        close(fd);
        return nullptr;
    }
    return std::shared_ptr<void>(new int(fd), [](void* x){ int* p = (int*)x; close(*p); delete p; });
#endif
}

} // namespace

void chooseCpu(std::vector<CpuCandidate> c, const CoreLock& lock, CpuPlacement& p){
    if (c.empty()) return;
    double top = 0;
    for (auto& x : c) top = std::max(top, x.speedClass);
    c.erase(std::remove_if(c.begin(), c.end(), [&](const CpuCandidate& x){ return x.speedClass < 0.9 * top; }), c.end());
    if (std::any_of(c.begin(), c.end(), [](const CpuCandidate& x){ return !x.onCpu0Core; }))
        c.erase(std::remove_if(c.begin(), c.end(), [](const CpuCandidate& x){ return x.onCpu0Core; }), c.end());
    std::sort(c.begin(), c.end(), [](const CpuCandidate& a, const CpuCandidate& b){
        long ca = std::lround(a.coreBusy * 20), cb = std::lround(b.coreBusy * 20);   // 5% steps: noise isn't a reason
        if (ca != cb) return ca < cb;
        long ba = std::lround(a.busy * 20), bb = std::lround(b.busy * 20);
        if (ba != bb) return ba < bb;
        return a.group != b.group ? a.group > b.group : a.cpu > b.cpu;
    });
    for (auto& x : c){
        bool taken = false;
        std::shared_ptr<void> l = lock ? lock(x.group, x.core, taken) : nullptr;
        if (!taken){ p.cpu = x.cpu; p.group = x.group; p.lock = l; return; }
    }
    p.cpu = c[0].cpu; p.group = c[0].group; p.shared = true;
}

#if defined(_WIN32) || defined(__linux__)
namespace {

std::string pinnedText(const CpuPlacement& p){
    if (p.cpu < 0) return "no CPU placement";
    return "pinned to CPU " + std::to_string(p.cpu) + (p.group ? " of group " + std::to_string(p.group) : "")
         + " (fastest core class, least busy" + (p.shared ? "; every such core is in use by another timed run" : "") + ")";
}

#ifdef __linux__
// Relative speed of a logical CPU: cpu_capacity (ARM big.LITTLE), else the
// maximum clock (Intel hybrid: P-cores clock higher than E-cores), else 0.
long long linuxCapacity(int cpu){
    std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    for (const char* f : {"/cpu_capacity", "/cpufreq/cpuinfo_max_freq"}){
        std::ifstream in(base + f);
        long long v = 0;
        if (in >> v && v > 0) return v;
    }
    return 0;
}

// "0-3,8" -> {0, 1, 2, 3, 8}
std::vector<int> parseCpuList(const std::string& s){
    std::vector<int> out;
    std::istringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')){
        size_t dash = part.find('-');
        try {
            int a = std::stoi(part.substr(0, dash)), b = dash == std::string::npos ? a : std::stoi(part.substr(dash + 1));
            for (int i = a; i <= b && i - a < 4096; i++) out.push_back(i);
        } catch (...) {}
    }
    return out;
}

// cpu -> (busy ticks, all ticks) from /proc/stat
std::map<int, std::pair<unsigned long long, unsigned long long>> linuxTicks(){
    std::map<int, std::pair<unsigned long long, unsigned long long>> t;
    std::ifstream in("/proc/stat");
    std::string line;
    while (std::getline(in, line)){
        if (line.size() < 4 || line.compare(0, 3, "cpu") != 0 || !std::isdigit((unsigned char)line[3])) continue;
        std::istringstream ss(line.substr(3));
        int cpu;
        ss >> cpu;
        unsigned long long v, all = 0, idle = 0;
        for (int i = 0; i < 8 && ss >> v; i++){ all += v; if (i == 3 || i == 4) idle += v; }   // idle, iowait
        t[cpu] = {all - idle, all};
    }
    return t;
}
#endif

#ifdef _WIN32
// Busy fraction of every logical CPU of the calling thread's processor group
// over ~0.2 s (NtQuerySystemInformation, SystemProcessorPerformanceInformation);
// empty when unavailable.
std::vector<double> windowsBusy(int n){
    struct Perf { LARGE_INTEGER idle, kernel, user, reserved1[2]; ULONG reserved2; };
    typedef LONG (WINAPI *QueryFn)(ULONG, PVOID, ULONG, PULONG);
    auto fn = (QueryFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    if (!fn || n <= 0) return {};
    std::vector<Perf> a(n), b(n);
    ULONG len = 0;
    if (fn(8, a.data(), (ULONG)(n * sizeof(Perf)), &len) != 0) return {};
    Sleep(200);
    if (fn(8, b.data(), (ULONG)(n * sizeof(Perf)), &len) != 0) return {};
    int got = (int)(len / sizeof(Perf));
    std::vector<double> busy(n, 0.0);
    for (int i = 0; i < got && i < n; i++){
        double all = (double)(b[i].kernel.QuadPart - a[i].kernel.QuadPart + b[i].user.QuadPart - a[i].user.QuadPart);
        double idle = (double)(b[i].idle.QuadPart - a[i].idle.QuadPart);   // kernel time includes idle time
        busy[i] = all > 0 ? std::max(0.0, std::min(1.0, (all - idle) / all)) : 0;
    }
    return busy;
}

// The logical CPUs of the calling thread's processor group that this process may use.
std::vector<CpuCandidate> windowsCandidates(){
    PROCESSOR_NUMBER here{};
    GetCurrentProcessorNumberEx(&here);
    int group = here.Group;
    int n = (int)GetActiveProcessorCount((WORD)group);
    std::vector<double> busy = windowsBusy(n);
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len == 0) return {};
    std::vector<char> buf(len);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf.data(), &len))
        return {};
    DWORD_PTR procMask = 0, sysMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask) || procMask == 0) procMask = ~(DWORD_PTR)0;
    std::vector<CpuCandidate> out;
    for (DWORD off = 0; off < len;){
        auto* e = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf.data() + off);
        off += e->Size;
        if (e->Relationship != RelationProcessorCore || e->Processor.GroupCount == 0) continue;
        const GROUP_AFFINITY& ga = e->Processor.GroupMask[0];
        if (ga.Group != group || ga.Mask == 0) continue;
        int core = 0;
        while (!((ga.Mask >> core) & 1)) core++;
        double coreBusy = 0;
        for (int i = 0; i < (int)(8 * sizeof(KAFFINITY)); i++)
            if ((ga.Mask >> i) & 1) coreBusy += i < (int)busy.size() ? busy[i] : 0;
        for (int i = 0; i < (int)(8 * sizeof(KAFFINITY)); i++){
            if (!((ga.Mask >> i) & 1) || !((procMask >> i) & 1)) continue;
            CpuCandidate c;
            c.cpu = i; c.group = group; c.core = core;
            c.onCpu0Core = group == 0 && (ga.Mask & 1);
            c.speedClass = 1.0 + e->Processor.EfficiencyClass;          // higher = faster
            c.busy = i < (int)busy.size() ? busy[i] : 0;
            c.coreBusy = coreBusy;
            out.push_back(c);
        }
    }
    return out;
}
#endif

} // namespace
#endif

CpuPlacement timedRunPlacement(){
    CpuPlacement p;
    const char* env = std::getenv("CHESSGUI_CPU");
    if (env && std::string(env) == "none") return noPlacement();
    int forced = -1;
    if (env && *env){
        char* end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end && *end == 0 && v >= 0) forced = (int)v;
    }
#ifdef __APPLE__
    (void)forced;                                              // macOS can't pin threads
    if (env && std::string(env) == "background"){
        p.darwinBackground = true;
        p.description = "efficiency cores (macOS background band, CHESSGUI_CPU)";
    } else p.description = "macOS scheduling (threads can't be pinned)";
#elif defined(_WIN32)
    p.noPowerThrottling = true;
    if (forced >= 0){
        // counted over all processor groups
        int left = forced;
        for (WORD g = 0; g < GetActiveProcessorGroupCount(); g++){
            int n = (int)GetActiveProcessorCount(g);
            if (left < n){ p.cpu = left; p.group = g; break; }
            left -= n;
        }
        if (p.cpu >= 0){
            bool taken = false;
            p.lock = lockCore(p.group, p.cpu, taken);         // the CPU's number stands for its core here
            p.shared = taken;
            p.description = "pinned to CPU " + std::to_string(forced) + " (CHESSGUI_CPU)";
        }
    } else {
        chooseCpu(windowsCandidates(), lockCore, p);
        p.description = pinnedText(p);
    }
    p.description = p.cpu >= 0 ? p.description + ", no power throttling" : "no power throttling (no CPU pinning)";
#elif defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return noPlacement();
    if (forced >= 0){
        if (forced < CPU_SETSIZE && CPU_ISSET(forced, &allowed)){
            p.cpu = forced;
            bool taken = false;
            p.lock = lockCore(0, p.cpu, taken);               // the CPU's number stands for its core here
            p.shared = taken;
            p.description = "pinned to CPU " + std::to_string(p.cpu) + " (CHESSGUI_CPU)";
        }
    } else {
        auto t0 = linuxTicks();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto t1 = linuxTicks();
        auto busyOf = [&](int c){
            auto a = t0.find(c), b = t1.find(c);
            if (a == t0.end() || b == t1.end() || b->second.second <= a->second.second) return 0.0;
            return (double)(b->second.first - a->second.first) / (double)(b->second.second - a->second.second);
        };
        std::vector<int> zeroCore = parseCpuList([]{
            std::ifstream in("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list");
            std::string s; std::getline(in, s); return s; }());
        std::vector<CpuCandidate> c;
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++){
            if (!CPU_ISSET(cpu, &allowed)) continue;
            CpuCandidate x;
            x.cpu = cpu;
            x.onCpu0Core = cpu == 0 || std::find(zeroCore.begin(), zeroCore.end(), cpu) != zeroCore.end();
            x.speedClass = (double)linuxCapacity(cpu);
            x.busy = busyOf(cpu);
            std::ifstream sib("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
            std::string s;
            std::getline(sib, s);
            std::vector<int> siblings = parseCpuList(s);
            if (siblings.empty()) siblings = {cpu};
            x.core = *std::min_element(siblings.begin(), siblings.end());
            for (int y : siblings) x.coreBusy += busyOf(y);
            c.push_back(x);
        }
        chooseCpu(c, lockCore, p);
        p.description = pinnedText(p);
    }
    if (p.cpu < 0) p.description = "no CPU placement";
#else
    (void)forced;
    p.description = "no CPU placement on this system";
#endif
    return p;
}

bool applyToCurrentThread(const CpuPlacement& p){
#ifdef __APPLE__
    if (p.darwinBackground) return setpriority(PRIO_DARWIN_THREAD, 0, PRIO_DARWIN_BG) == 0;
    return true;
#elif defined(_WIN32)
    if (p.cpu < 0 || p.cpu >= (int)(8 * sizeof(KAFFINITY))) return p.cpu < 0;
    GROUP_AFFINITY ga{};
    ga.Mask = (KAFFINITY)1 << p.cpu;
    ga.Group = (WORD)p.group;
    return SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr) != 0;
#elif defined(__linux__)
    if (p.cpu < 0) return true;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(p.cpu, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof s, &s) == 0;
#else
    (void)p;
    return false;
#endif
}

int currentCpu(){
#ifdef _WIN32
    return (int)GetCurrentProcessorNumber();
#elif defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

double processCpuSeconds(long long pid){
#ifdef __APPLE__
    rusage_info_v2 ri{};
    if (proc_pid_rusage((pid_t)pid, RUSAGE_INFO_V2, (rusage_info_t*)&ri) != 0) return -1;
    // Apple silicon reports these in mach ticks, Intel Macs in nanoseconds
    mach_timebase_info_data_t tb{};
    mach_timebase_info(&tb);
    double ns = (double)(ri.ri_user_time + ri.ri_system_time) * tb.numer / tb.denom;
    return ns / 1e9;
#elif defined(__linux__)
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string s;
    if (!std::getline(in, s)) return -1;
    size_t close = s.rfind(')');                              // the name may contain spaces
    if (close == std::string::npos) return -1;
    std::istringstream rest(s.substr(close + 2));
    std::string field;
    unsigned long long utime = 0, stime = 0;
    for (int i = 3; i <= 15 && rest >> field; i++){           // fields 14, 15: utime, stime
        if (i == 14) utime = std::stoull(field);
        if (i == 15) stime = std::stoull(field);
    }
    long tick = sysconf(_SC_CLK_TCK);
    return tick > 0 ? (double)(utime + stime) / tick : -1;
#else
    (void)pid;
    return -1;
#endif
}

double processCpuSecondsHandle(void* processHandle){
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!processHandle || !GetProcessTimes((HANDLE)processHandle, &c, &e, &k, &u)) return -1;
    auto t = [](const FILETIME& f){ return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    return (double)(t(k) + t(u)) / 1e7;                       // 100 ns units
#else
    (void)processHandle;
    return -1;
#endif
}

double processPCoreSeconds(long long pid){
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    static const bool twoKinds = []{
        int n = 0;
        size_t len = sizeof n;
        return sysctlbyname("hw.nperflevels", &n, &len, nullptr, 0) == 0 && n > 1;
    }();
    if (!twoKinds) return -1;
    rusage_info_v6 ri{};
    if (proc_pid_rusage((pid_t)pid, RUSAGE_INFO_V6, (rusage_info_t*)&ri) != 0) return -1;
    mach_timebase_info_data_t tb{};
    mach_timebase_info(&tb);
    double ns = (double)(ri.ri_user_ptime + ri.ri_system_ptime) * tb.numer / tb.denom;
    return ns / 1e9;
#else
    (void)pid;
    return -1;
#endif
}

void keepThisProcessUnthrottled(){
#ifdef _WIN32
    struct { ULONG Version, ControlMask, StateMask; } st{1, 0x1, 0};   // see UciEngine::start
    typedef BOOL (WINAPI *SetInfoFn)(HANDLE, int, LPVOID, DWORD);
    auto fn = (SetInfoFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
    if (fn) fn(GetCurrentProcess(), 4, &st, sizeof st);
#endif
}
