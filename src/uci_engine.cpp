/* Chess-GUI-for-UCI — UCI engine process wrapper. GPL-2.0 */
#include "uci_engine.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <fcntl.h>
#endif

using clk = std::chrono::steady_clock;
static long long msSince(clk::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now()-t).count();
}

UciEngine::~UciEngine(){ quit(); }

#ifdef _WIN32
bool UciEngine::start(const std::string& exePath){
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outR=0,outW=0,inR=0,inW=0;
    if (!CreatePipe(&outR,&outW,&sa,0) || !CreatePipe(&inR,&inW,&sa,0)){ errMsg="pipe failed"; return false; }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW,  HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{}; si.cb=sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exePath + "\"";
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)){
        errMsg = "cannot launch: " + exePath; return false;
    }
    CloseHandle(inR); CloseHandle(outW); CloseHandle(pi.hThread);
    hChildIn = inW; hChildOut = outR; hProc = pi.hProcess;
    running = true;
#else
bool UciEngine::start(const std::string& exePath){
    // A crashed/killed engine must never take the GUI down with it:
    // without this, writing to a dead engine's pipe raises SIGPIPE and
    // terminates the whole application.
    signal(SIGPIPE, SIG_IGN);
    int toChild[2], fromChild[2];
    if (pipe(toChild) || pipe(fromChild)){ errMsg="pipe failed"; return false; }
    pid_t p = fork();
    if (p < 0){ errMsg="fork failed"; return false; }
    if (p == 0){
        dup2(toChild[0], 0); dup2(fromChild[1], 1); dup2(fromChild[1], 2);
        close(toChild[0]); close(toChild[1]); close(fromChild[0]); close(fromChild[1]);
        execl(exePath.c_str(), exePath.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(toChild[0]); close(fromChild[1]);
    inFd = toChild[1]; outFd = fromChild[0]; pid = p;
    fcntl(outFd, F_SETFL, O_NONBLOCK);
    running = true;
#endif
    // UCI handshake
    sendRaw("uci");
    auto t0 = clk::now();
    bool ok = false;
    while (msSince(t0) < 6000){
        std::string ln = readLine(200);
        if (ln.empty()) continue;
        if (ln.rfind("id name ",0)==0) engName = ln.substr(8);
        if (ln.rfind("option ",0)==0) parseOptionLine(ln);
        if (ln == "uciok"){ ok=true; break; }
    }
    if (!ok){ errMsg = "no uciok (not a UCI engine?)"; quit(); return false; }
    sendRaw("isready");
    t0 = clk::now();
    while (msSince(t0) < 6000){
        if (readLine(200) == "readyok") return true;
    }
    errMsg = "no readyok"; quit(); return false;
}

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

void UciEngine::sendRaw(const std::string& line){
    if (!running) return;
    std::string s = line + "\n";
#ifdef _WIN32
    DWORD written=0;
    WriteFile((HANDLE)hChildIn, s.c_str(), (DWORD)s.size(), &written, nullptr);
#else
    ssize_t r = write(inFd, s.c_str(), s.size()); (void)r;
#endif
}

std::string UciEngine::readLine(int timeoutMs){
    auto t0 = clk::now();
    while (true){
        size_t nl = rbuf.find('\n');
        if (nl != std::string::npos){
            std::string ln = rbuf.substr(0, nl);
            if (!ln.empty() && ln.back()=='\r') ln.pop_back();
            rbuf.erase(0, nl+1);
            return ln;
        }
        if (!running || msSince(t0) >= timeoutMs) return "";
        char buf[4096];
#ifdef _WIN32
        DWORD avail=0;
        if (!PeekNamedPipe((HANDLE)hChildOut, nullptr,0,nullptr,&avail,nullptr)){ running=false; return ""; }
        if (!avail){ std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        DWORD got=0;
        if (!ReadFile((HANDLE)hChildOut, buf, sizeof(buf), &got, nullptr) || !got){ running=false; return ""; }
        rbuf.append(buf, got);
#else
        fd_set fds; FD_ZERO(&fds); FD_SET(outFd,&fds);
        timeval tv{0, 5000};
        int r = select(outFd+1, &fds, nullptr, nullptr, &tv);
        if (r > 0){
            ssize_t got = read(outFd, buf, sizeof(buf));
            if (got <= 0){ running=false; return ""; }
            rbuf.append(buf, (size_t)got);
        }
#endif
    }
}

void UciEngine::quit(){
    if (!running){
#ifndef _WIN32
        if (pid > 0){ int st; waitpid((pid_t)pid, &st, WNOHANG); }
#endif
        return;
    }
    sendRaw("quit");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    running = false;
#ifdef _WIN32
    if (hProc){ WaitForSingleObject((HANDLE)hProc, 500); TerminateProcess((HANDLE)hProc, 0);
        CloseHandle((HANDLE)hProc); CloseHandle((HANDLE)hChildIn); CloseHandle((HANDLE)hChildOut); hProc=nullptr; }
#else
    if (pid > 0){
        int st;
        if (waitpid((pid_t)pid, &st, WNOHANG) == 0){ kill((pid_t)pid, SIGKILL); waitpid((pid_t)pid, &st, 0); }
        close(inFd); close(outFd); pid=-1;
    }
#endif
}

void UciEngine::setOption(const std::string& n, const std::string& v){
    sendRaw("setoption name " + n + " value " + v);
}
void UciEngine::newGame(){
    sendRaw("ucinewgame"); sendRaw("isready");
    auto t0 = clk::now();
    while (msSince(t0) < 5000) if (readLine(100)=="readyok") break;
}
void UciEngine::setPosition(const std::string& fen, const std::vector<std::string>& mv){
    std::string cmd = "position fen " + fen;
    if (!mv.empty()){
        cmd += " moves";
        for (auto& m : mv) cmd += " " + m;
    }
    sendRaw(cmd);
}

void UciEngine::parseInfo(const std::string& line){
    if (line.rfind("info",0)!=0) return;
    std::istringstream ss(line);
    std::string tok; ss >> tok;
    int mpv = 1, depth = 0, cp = 0, mateIn = 0;
    bool isMate=false, sawScore=false, sawPv=false, sawDepth=false;
    std::string pv;
    while (ss >> tok){
        if (tok=="depth"){ ss >> depth; sawDepth=true; }
        else if (tok=="multipv") ss >> mpv;
        else if (tok=="score"){
            std::string kind; ss >> kind;
            int v; ss >> v;
            if (kind=="cp"){ isMate=false; cp=v; sawScore=true; }
            else if (kind=="mate"){ isMate=true; mateIn=v; sawScore=true; }
        }
        else if (tok=="pv"){
            std::string m;
            while (ss >> m){ if(!pv.empty())pv+=" "; pv+=m; }
            sawPv=true;
        }
    }
    if (mpv < 1) mpv = 1;
    std::lock_guard<std::mutex> lk(infoMx);
    if (mpv==1){                       // legacy single-line info follows the best line
        info.valid = true;
        if (sawDepth) info.depth = depth;
        if (sawScore){ info.isMate=isMate; info.scoreCp=cp; info.mateIn=mateIn; }
        if (sawPv) info.pv = pv;
    }
    if (sawPv && sawScore && mpv<=64){
        if ((int)pvs_.size() < mpv) pvs_.resize(mpv);
        PvLine& L = pvs_[mpv-1];
        L.multipv=mpv; L.depth=depth; L.scoreCp=cp; L.isMate=isMate;
        L.mateIn=mateIn; L.pv=pv; L.valid=true;
    }
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

std::string UciEngine::waitBestmove(int timeoutMs){
    auto t0 = clk::now();
    while (msSince(t0) < timeoutMs && running){
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
    { std::lock_guard<std::mutex> lk(infoMx); info = EngineInfo{}; pvs_.clear(); ponder_.clear(); }
    sendRaw("go movetime " + std::to_string(ms));
    return waitBestmove(ms + 8000);
}
std::string UciEngine::goDepth(int d){
    { std::lock_guard<std::mutex> lk(infoMx); info = EngineInfo{}; pvs_.clear(); ponder_.clear(); }
    sendRaw("go depth " + std::to_string(d));
    return waitBestmove(600000);
}
std::string UciEngine::goClock(int wt,int bt,int wi,int bi){
    { std::lock_guard<std::mutex> lk(infoMx); info = EngineInfo{}; pvs_.clear(); ponder_.clear(); }
    std::ostringstream o;
    o << "go wtime "<<wt<<" btime "<<bt<<" winc "<<wi<<" binc "<<bi;
    sendRaw(o.str());
    return waitBestmove(wt + bt + 20000);
}
void UciEngine::goInfinite(){
    { std::lock_guard<std::mutex> lk(infoMx); info = EngineInfo{}; pvs_.clear(); ponder_.clear(); }
    sendRaw("go infinite");
}
std::string UciEngine::goPonderWait(int wt,int bt,int wi,int bi,int timeoutMs){
    { std::lock_guard<std::mutex> lk(infoMx); info = EngineInfo{}; pvs_.clear(); ponder_.clear(); }
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
    while (msSince(t0) < timeoutMs && running){
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
    sendRaw("isready");
    auto t1 = clk::now();
    while (msSince(t1) < 3000){ if (readLine(100)=="readyok") break; }
    return nodes;
}

void UciEngine::pump(){
    for (int i=0;i<64;i++){
        std::string ln = readLine(1);
        if (ln.empty()) break;
        parseInfo(ln);
    }
}
