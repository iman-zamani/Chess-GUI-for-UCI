/* Chess-GUI-for-UCI — UCI engine process wrapper. GPL-2.0 */
#include "uci_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using clk = std::chrono::steady_clock;
static long long nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count();
}
static long long msSince(clk::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now()-t).count();
}

UciEngine::~UciEngine(){ quit(); }

// =============================================================== process start
#ifdef _WIN32
static std::wstring widen(const std::string& s){
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
// Quote one argument the way the MSVC runtime (CommandLineToArgvW) splits them.
static std::wstring quoteArg(const std::wstring& a){
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0; ; i++){
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\'){ i++; bs++; }
        if (i == a.size()){ q.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"'){ q.append(bs * 2 + 1, L'\\'); q.push_back(L'"'); }
        else { q.append(bs, L'\\'); q.push_back(a[i]); }
    }
    q.push_back(L'"');
    return q;
}

bool UciEngine::spawn(){
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outR=0,outW=0,inR=0,inW=0,errR=0,errW=0;
    if (!CreatePipe(&outR,&outW,&sa,0) || !CreatePipe(&inR,&inW,&sa,0) || !CreatePipe(&errR,&errW,&sa,0)){
        errMsg = "pipe failed"; return false;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    // Only these three handles may be inherited: with several engines
    // starting concurrently, a leaked pipe end would keep another engine's
    // stdin open after we close it.
    HANDLE inherit[3] = {inR, outW, errW};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf.data();
    bool attrOk = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)
               && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                            inherit, sizeof(inherit), nullptr, nullptr);
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = inR; si.StartupInfo.hStdOutput = outW; si.StartupInfo.hStdError = errW;
    si.lpAttributeList = attrOk ? attrs : nullptr;

    std::wstring cmd = quoteArg(widen(cfg_.path));
    for (auto& a : cfg_.args) cmd += L" " + quoteArg(widen(a));
    std::wstring exe = widen(cfg_.path), wd = widen(cfg_.effectiveWorkingDir());
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW | (attrOk ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE, flags, nullptr,
                             wd.empty() ? nullptr : wd.c_str(), &si.StartupInfo, &pi);
    DWORD gle = GetLastError();
    if (attrOk) DeleteProcThreadAttributeList(attrs);
    CloseHandle(inR); CloseHandle(outW); CloseHandle(errW);
    if (!ok){
        CloseHandle(inW); CloseHandle(outR); CloseHandle(errR);
        errMsg = "cannot launch " + cfg_.path + " (Windows error " + std::to_string(gle) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    hChildIn = inW; hChildOut = outR; hChildErr = errR; hProc = pi.hProcess;
    return true;
}
#else
static std::mutex spawnMx;   // pipes + fork: keep other engines' fds out of each child

static void setCloexec(int fd){ fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }

bool UciEngine::spawn(){
    // A crashed/killed engine must never take the GUI down with it:
    // without this, writing to a dead engine's pipe raises SIGPIPE and
    // terminates the whole application.
    signal(SIGPIPE, SIG_IGN);
    std::string wd = cfg_.effectiveWorkingDir();
    // Build argv before fork: only async-signal-safe calls in the child.
    std::vector<std::string> argStore;
    argStore.push_back(cfg_.path);
    for (auto& a : cfg_.args) argStore.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argStore) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::lock_guard<std::mutex> lk(spawnMx);
    int toChild[2], fromChild[2], errPipe[2], execErr[2];
    if (pipe(toChild) || pipe(fromChild) || pipe(errPipe) || pipe(execErr)){ errMsg="pipe failed"; return false; }
    for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1], errPipe[0], errPipe[1],
                   execErr[0], execErr[1]}) setCloexec(fd);
    pid_t p = fork();
    if (p < 0){
        for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1], errPipe[0], errPipe[1],
                       execErr[0], execErr[1]}) close(fd);
        errMsg="fork failed"; return false;
    }
    if (p == 0){
        dup2(toChild[0], 0); dup2(fromChild[1], 1); dup2(errPipe[1], 2);   // dup2 clears CLOEXEC
        int stage = 1;
        if (!wd.empty() && chdir(wd.c_str()) != 0){
            int e = errno; ssize_t r = write(execErr[1], &stage, sizeof stage); r = write(execErr[1], &e, sizeof e);
            (void)r; _exit(127);
        }
        if (std::strchr(argv[0], '/')) execv(argv[0], argv.data());
        else execvp(argv[0], argv.data());                 // bare name: search PATH
        int e = errno; stage = 2;
        ssize_t r = write(execErr[1], &stage, sizeof stage); r = write(execErr[1], &e, sizeof e); (void)r;
        _exit(127);
    }
    close(toChild[0]); close(fromChild[1]); close(errPipe[1]); close(execErr[1]);
    // exec succeeded iff the close-on-exec pipe closes without data
    int stage = 0, e = 0;
    ssize_t n = read(execErr[0], &stage, sizeof stage);
    if (n == (ssize_t)sizeof stage){ ssize_t r = read(execErr[0], &e, sizeof e); (void)r; }
    close(execErr[0]);
    if (n > 0){
        int st; waitpid(p, &st, 0);
        close(toChild[1]); close(fromChild[0]); close(errPipe[0]);
        errMsg = (stage == 1 ? "cannot enter working directory " + wd : "cannot launch " + cfg_.path)
               + ": " + std::strerror(e);
        return false;
    }
    inFd = toChild[1]; outFd = fromChild[0]; errFd = errPipe[0]; pid = p;
    fcntl(outFd, F_SETFL, O_NONBLOCK);
    fcntl(errFd, F_SETFL, O_NONBLOCK);
    return true;
}
#endif

bool UciEngine::start(const std::string& exePath){
    EngineConfig c; c.path = exePath;
    return start(c);
}

bool UciEngine::start(const EngineConfig& cfg){
    quit();
    cfg_ = cfg;
    // The child starts in its own working directory, so a relative engine
    // path must be resolved against ours first.
    {
        namespace fs = std::filesystem;
        fs::path p = fs::u8path(cfg_.path);
        bool bare = !p.has_parent_path();
        std::error_code ec;
        if (!bare && p.is_relative()){
            fs::path a = fs::absolute(p, ec);
            if (!ec) cfg_.path = a.lexically_normal().u8string();
        }
        if (!cfg_.workingDir.empty() && fs::u8path(cfg_.workingDir).is_relative()){
            fs::path a = fs::absolute(fs::u8path(cfg_.workingDir), ec);
            if (!ec) cfg_.workingDir = a.lexically_normal().u8string();
        }
        if (bare && cfg_.workingDir.empty()) cfg_.workingDir = fs::current_path(ec).u8string();
    }
    resetSearchInfo();
    opts.clear(); engName = "engine"; engAuthor.clear(); errMsg.clear();
    rbuf.clear(); ebuf.clear();
    startMs = nowMs();
    addLog('#', "start " + cfg_.path + " (working dir " + cfg_.effectiveWorkingDir() + ")");
    if (!spawn()){ addLog('#', errMsg); return false; }
    running = true;
    // UCI handshake
    sendRaw("uci");
    auto t0 = clk::now();
    bool ok = false;
    while (msSince(t0) < uciokTimeout && alive()){
        std::string ln = readLine(200);
        if (ln.empty()) continue;
        if (ln.rfind("id name ",0)==0) engName = ln.substr(8);
        else if (ln.rfind("id author ",0)==0) engAuthor = ln.substr(10);
        else if (ln.rfind("option ",0)==0) parseOptionLine(ln);
        else if (ln == "uciok"){ ok=true; break; }
    }
    auto fail = [&](const std::string& why){
        errMsg = why;
        std::string tail = stderrTail(3);
        if (!tail.empty()) errMsg += " — stderr: " + tail;
        addLog('#', errMsg);
        quit();
        return false;
    };
    auto secs = [](int ms){ std::ostringstream o; o << ms / 1000.0 << " s"; return o.str(); };
    if (!ok) return fail(alive() ? "no uciok within " + secs(uciokTimeout) + " (not a UCI engine?)"
                                 : "engine exited during the uci handshake");
    for (auto& o : cfg_.options) setOption(o.first, o.second);
    if (!isReady(readyTimeout)) return fail(alive() ? "no readyok within " + secs(readyTimeout)
                                                    : "engine exited after uci");
    return true;
}

// =============================================================== I/O
void UciEngine::addLog(char dir, const std::string& text){
    std::lock_guard<std::mutex> lk(logMx);
    log_.push_back({nowMs() - startMs, dir, text});
    while (log_.size() > logCap){ log_.pop_front(); logBase++; }
}
std::vector<UciLogLine> UciEngine::logSince(uint64_t& cursor){
    std::lock_guard<std::mutex> lk(logMx);
    std::vector<UciLogLine> out;
    if (cursor < logBase) cursor = logBase;
    for (uint64_t i = cursor; i < logBase + log_.size(); i++) out.push_back(log_[(size_t)(i - logBase)]);
    cursor = logBase + log_.size();
    return out;
}
void UciEngine::setLogCapacity(size_t n){
    std::lock_guard<std::mutex> lk(logMx);
    logCap = std::max<size_t>(n, 16);
}
std::string UciEngine::stderrTail(size_t maxLines){
    std::lock_guard<std::mutex> lk(logMx);
    std::vector<std::string> lines;
    for (auto it = log_.rbegin(); it != log_.rend() && lines.size() < maxLines; ++it)
        if (it->dir == '!') lines.push_back(it->text);
    std::string s;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it){ if (!s.empty()) s += " | "; s += *it; }
    return s;
}

void UciEngine::sendRaw(const std::string& line){
    if (!running) return;
    addLog('>', line);
    std::string s = line + "\n";
#ifdef _WIN32
    DWORD written=0;
    if (!WriteFile((HANDLE)hChildIn, s.c_str(), (DWORD)s.size(), &written, nullptr)) markDead();
#else
    if (write(inFd, s.c_str(), s.size()) < 0 && errno == EPIPE) markDead();
#endif
}

void UciEngine::markDead(){
    if (running.exchange(false)) addLog('#', "engine process ended");
}

// Moves available stdout/stderr bytes into rbuf/ebuf, waiting up to
// timeoutMs for something to arrive. Complete stderr lines go to the log.
bool UciEngine::fillBuffers(int timeoutMs){
    bool got = false;
    char buf[8192];
#ifdef _WIN32
    auto t0 = clk::now();
    while (true){
        for (int k = 0; k < 2; k++){
            HANDLE h = (HANDLE)(k == 0 ? hChildOut : hChildErr);
            if (!h) continue;
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)){ if (k == 0) markDead(); continue; }
            if (!avail) continue;
            DWORD n = 0;
            if (!ReadFile(h, buf, std::min<DWORD>(avail, sizeof buf), &n, nullptr) || !n){ if (k == 0) markDead(); continue; }
            (k == 0 ? rbuf : ebuf).append(buf, n);
            if (k == 0) got = true;
        }
        if (got || !running || msSince(t0) >= timeoutMs) break;
        Sleep(2);
    }
#else
    pollfd fds[2] = {{outFd, POLLIN, 0}, {errFd, POLLIN, 0}};
    int r = poll(fds, 2, std::max(0, timeoutMs));
    if (r > 0){
        for (int k = 0; k < 2; k++){
            if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t n = read(fds[k].fd, buf, sizeof buf);
            if (n > 0){ (k == 0 ? rbuf : ebuf).append(buf, (size_t)n); if (k == 0) got = true; }
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)){
                if (k == 0) markDead();
                else errFd = -1;                     // stderr closed; keep reading stdout
            }
        }
        if (errFd < 0) fds[1].fd = -1;
    }
#endif
    size_t nl;
    while ((nl = ebuf.find('\n')) != std::string::npos){
        std::string ln = ebuf.substr(0, nl);
        if (!ln.empty() && ln.back()=='\r') ln.pop_back();
        ebuf.erase(0, nl+1);
        addLog('!', ln);
    }
    return got;
}

std::string UciEngine::readLine(int timeoutMs){
    auto t0 = clk::now();
    while (true){
        size_t nl = rbuf.find('\n');
        if (nl != std::string::npos){
            std::string ln = rbuf.substr(0, nl);
            if (!ln.empty() && ln.back()=='\r') ln.pop_back();
            rbuf.erase(0, nl+1);
            addLog('<', ln);
            return ln;
        }
        if (!running) return "";
        long long left = timeoutMs - msSince(t0);
        if (left <= 0) return "";
        fillBuffers((int)std::min<long long>(left, 50));
    }
}

bool UciEngine::alive(){
    if (!running) return false;
#ifdef _WIN32
    if (hProc && WaitForSingleObject((HANDLE)hProc, 0) == WAIT_OBJECT_0){
        fillBuffers(0);                      // keep whatever it printed last
        if (rbuf.find('\n') == std::string::npos) markDead();
    }
#else
    if (pid > 0){
        int st;
        if (waitpid((pid_t)pid, &st, WNOHANG) == (pid_t)pid){
            pid = -1;
            addLog('#', WIFSIGNALED(st) ? "engine killed by signal " + std::to_string(WTERMSIG(st))
                                        : "engine exited with code " + std::to_string(WEXITSTATUS(st)));
            fillBuffers(0);
            if (rbuf.find('\n') == std::string::npos) markDead();
        }
    }
#endif
    return running;
}

void UciEngine::quit(){
#ifdef _WIN32
    if (!hProc) { running = false; return; }
    if (running) sendRaw("quit");
    if (WaitForSingleObject((HANDLE)hProc, 1000) != WAIT_OBJECT_0) TerminateProcess((HANDLE)hProc, 0);
    WaitForSingleObject((HANDLE)hProc, 1000);
    CloseHandle((HANDLE)hProc); CloseHandle((HANDLE)hChildIn); CloseHandle((HANDLE)hChildOut);
    CloseHandle((HANDLE)hChildErr);
    hProc = hChildIn = hChildOut = hChildErr = nullptr;
#else
    if (inFd < 0 && pid <= 0){ running = false; return; }
    if (running) sendRaw("quit");
    if (pid > 0){
        int st = 0; bool exited = false;
        for (int i = 0; i < 100 && !exited; i++){          // up to 1 s for a clean exit
            if (waitpid((pid_t)pid, &st, WNOHANG) == (pid_t)pid) exited = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!exited){ kill((pid_t)pid, SIGKILL); waitpid((pid_t)pid, &st, 0); }
        pid = -1;
    }
    if (inFd >= 0) close(inFd);
    if (outFd >= 0) close(outFd);
    if (errFd >= 0) close(errFd);
    inFd = outFd = errFd = -1;
#endif
    running = false;
}

// =============================================================== UCI commands
void UciEngine::parseOptionLine(const std::string& line){
    // option name <name...> type <t> [default <d...>] [min N] [max N] [var V]...
    std::istringstream ss(line);
    std::string tok; ss >> tok;                     // "option"
    UciOption o;
    std::string key;
    auto isKey=[](const std::string& s){
        return s=="name"||s=="type"||s=="default"||s=="min"||s=="max"||s=="var";
    };
    std::vector<std::string> toks;
    while (ss >> tok) toks.push_back(tok);
    for (size_t i=0;i<toks.size();){
        if (!isKey(toks[i])){ i++; continue; }
        key = toks[i++];
        std::string val;
        while (i<toks.size() && !isKey(toks[i])){
            if (!val.empty()) val += " ";
            val += toks[i++];
        }
        if (key=="name") o.name = val;
        else if (key=="type") o.type = val;
        else if (key=="default") o.defVal = (val=="<empty>"? "" : val);
        else if (key=="min") o.minV = atoll(val.c_str());
        else if (key=="max") o.maxV = atoll(val.c_str());
        else if (key=="var") o.vars.push_back(val);
    }
    if (!o.name.empty() && !o.type.empty()) opts.push_back(o);
}

bool UciEngine::hasOption(const std::string& n) const{
    for (auto& o : opts){
        if (o.name.size() != n.size()) continue;
        bool same = true;                            // UCI option names are case-insensitive
        for (size_t i = 0; i < n.size() && same; i++)
            same = std::tolower((unsigned char)o.name[i]) == std::tolower((unsigned char)n[i]);
        if (same) return true;
    }
    return false;
}

void UciEngine::setOption(const std::string& n, const std::string& v){
    sendRaw(v.empty() ? "setoption name " + n : "setoption name " + n + " value " + v);
}
bool UciEngine::isReady(int timeoutMs){
    sendRaw("isready");
    auto t0 = clk::now();
    while (msSince(t0) < timeoutMs && alive()){
        std::string ln = readLine(100);
        if (ln == "readyok") return true;
        if (!ln.empty()) parseInfo(ln);
    }
    return false;
}
void UciEngine::newGame(){
    sendRaw("ucinewgame");
    isReady(10000);
}
void UciEngine::setPosition(const std::string& fen, const std::vector<std::string>& mv){
    std::string cmd = "position fen " + fen;
    if (!mv.empty()){
        cmd += " moves";
        for (auto& m : mv) cmd += " " + m;
    }
    sendRaw(cmd);
}

void parseInfoLine(const std::string& line, EngineInfo& info, std::vector<PvLine>& pvs){
    if (line.rfind("info",0)!=0) return;
    std::istringstream ss(line);
    std::string tok; ss >> tok;
    int mpv = 1, depth = -1, seldepth = -1, cp = 0, mateIn = 0, hashfull = -1, currNo = -1;
    long long nodes = -1, nps = -1, timeMs = -1, tbhits = -1;
    bool isMate=false, sawScore=false, sawPv=false, lower=false, upper=false;
    std::string pv, currmove, str;
    bool sawString = false;
    while (ss >> tok){
        if (tok=="depth") ss >> depth;
        else if (tok=="seldepth") ss >> seldepth;
        else if (tok=="multipv") ss >> mpv;
        else if (tok=="nodes") ss >> nodes;
        else if (tok=="nps") ss >> nps;
        else if (tok=="time") ss >> timeMs;
        else if (tok=="hashfull") ss >> hashfull;
        else if (tok=="tbhits") ss >> tbhits;
        else if (tok=="currmove") ss >> currmove;
        else if (tok=="currmovenumber") ss >> currNo;
        else if (tok=="lowerbound") lower = true;
        else if (tok=="upperbound") upper = true;
        else if (tok=="score"){
            std::string kind; ss >> kind;
            int v = 0; ss >> v;
            if (kind=="cp"){ isMate=false; cp=v; sawScore=true; }
            else if (kind=="mate"){ isMate=true; mateIn=v; sawScore=true; }
        }
        else if (tok=="pv"){
            std::string m;
            while (ss >> m){ if(!pv.empty())pv+=" "; pv+=m; }
            sawPv=true;
        }
        else if (tok=="string"){                  // rest of the line is free text
            std::getline(ss, str);
            if (!str.empty() && str[0]==' ') str.erase(0,1);
            sawString = true;
        }
    }
    if (ss.fail() && !ss.eof()) {}                // malformed numbers: keep what parsed
    if (mpv < 1) mpv = 1;
    if (mpv==1){                       // the summary follows the best line
        info.valid = true;
        if (depth >= 0) info.depth = depth;
        if (seldepth >= 0) info.seldepth = seldepth;
        if (sawScore){ info.isMate=isMate; info.scoreCp=cp; info.mateIn=mateIn;
                       info.lowerbound=lower; info.upperbound=upper; }
        if (sawPv) info.pv = pv;
    }
    if (nodes >= 0) info.nodes = nodes;
    if (nps >= 0) info.nps = nps;
    if (timeMs >= 0) info.timeMs = timeMs;
    if (hashfull >= 0) info.hashfull = hashfull;
    if (tbhits >= 0) info.tbhits = tbhits;
    if (!currmove.empty()) info.currmove = currmove;
    if (currNo >= 0) info.currmoveNumber = currNo;
    if (sawString) info.lastString = str;
    if (sawScore && mpv<=256 && (sawPv || (mpv <= (int)pvs.size() && pvs[mpv-1].valid))){
        if ((int)pvs.size() < mpv) pvs.resize(mpv);
        PvLine& L = pvs[mpv-1];
        L.multipv=mpv; L.scoreCp=cp; L.isMate=isMate; L.mateIn=mateIn;
        L.lowerbound=lower; L.upperbound=upper;
        if (depth >= 0) L.depth=depth;
        if (seldepth >= 0) L.seldepth=seldepth;
        if (nodes >= 0) L.nodes=nodes;
        if (nps >= 0) L.nps=nps;
        if (timeMs >= 0) L.timeMs=timeMs;
        if (sawPv) L.pv=pv;
        L.valid = !L.pv.empty();
    }
}
void UciEngine::parseInfo(const std::string& line){
    if (line.rfind("info",0)!=0) return;
    std::lock_guard<std::mutex> lk(infoMx);
    parseInfoLine(line, info, pvs_);
}
EngineInfo UciEngine::lastInfo(){
    std::lock_guard<std::mutex> lk(infoMx);
    return info;
}
std::vector<PvLine> UciEngine::lastPvs(){
    std::lock_guard<std::mutex> lk(infoMx);
    return pvs_;
}
std::string UciEngine::lastPonder(){
    std::lock_guard<std::mutex> lk(infoMx);
    return ponder_;
}
void UciEngine::resetSearchInfo(){
    std::lock_guard<std::mutex> lk(infoMx);
    info = EngineInfo{}; pvs_.clear(); ponder_.clear();
}

std::string UciEngine::waitBestmove(int timeoutMs){
    auto t0 = clk::now();
    while (msSince(t0) < timeoutMs && alive()){
        std::string ln = readLine(100);
        if (ln.empty()) continue;
        parseInfo(ln);
        if (ln.rfind("bestmove",0)==0){
            std::istringstream ss(ln);
            std::string a,b,c2,d; ss >> a >> b >> c2 >> d;
            if (c2=="ponder"){ std::lock_guard<std::mutex> lk(infoMx); ponder_=d; }
            return b=="(none)" ? "" : b;
        }
    }
    return "";
}
std::string UciEngine::goMovetime(int ms){
    resetSearchInfo();
    sendRaw("go movetime " + std::to_string(ms));
    return waitBestmove(ms + 8000);
}
std::string UciEngine::goDepth(int d){
    resetSearchInfo();
    sendRaw("go depth " + std::to_string(d));
    return waitBestmove(600000);
}
std::string UciEngine::goNodes(long long n, int timeoutMs){
    resetSearchInfo();
    sendRaw("go nodes " + std::to_string(n));
    return waitBestmove(timeoutMs);
}
std::string UciEngine::goClock(int wt,int bt,int wi,int bi){
    resetSearchInfo();
    std::ostringstream o;
    o << "go wtime "<<wt<<" btime "<<bt<<" winc "<<wi<<" binc "<<bi;
    sendRaw(o.str());
    return waitBestmove(wt + bt + 20000);
}
void UciEngine::goInfinite(){
    resetSearchInfo();
    sendRaw("go infinite");
}
std::string UciEngine::goPonderWait(int wt,int bt,int wi,int bi,int timeoutMs){
    resetSearchInfo();
    std::ostringstream o;
    o << "go ponder wtime "<<wt<<" btime "<<bt<<" winc "<<wi<<" binc "<<bi;
    sendRaw(o.str());
    return waitBestmove(timeoutMs);
}
std::string UciEngine::stopAndWait(int timeoutMs){
    sendRaw("stop");
    return waitBestmove(timeoutMs);
}
long long UciEngine::goPerft(int depth, int timeoutMs){
    // Stockfish and many engines: "go perft N" prints "Nodes searched: X"
    sendRaw("go perft " + std::to_string(depth));
    auto t0 = clk::now();
    long long nodes = -1;
    while (msSince(t0) < timeoutMs && alive()){
        std::string ln = readLine(100);
        if (ln.empty()) continue;
        size_t p = ln.find("Nodes searched:");
        if (p != std::string::npos){
            nodes = atoll(ln.c_str()+p+15);
            break;
        }
        if (ln.rfind("bestmove",0)==0) break;         // engine ignored perft
        if (ln.rfind("Unknown command",0)==0 || ln.find("nknown")!=std::string::npos) break;
    }
    // make sure engine is idle again
    isReady(3000);
    return nodes;
}

void UciEngine::pump(){
    for (int i=0;i<256;i++){
        std::string ln = readLine(i == 0 ? 1 : 0);
        if (ln.empty()) break;
        parseInfo(ln);
    }
}
