#include "uci_engine.hpp"
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#endif

UciEngine::UciEngine() : isRunning(false) {
#ifdef _WIN32
    hChildStd_IN_Rd = NULL; hChildStd_IN_Wr = NULL;
    hChildStd_OUT_Rd = NULL; hChildStd_OUT_Wr = NULL;
    hProcess = NULL;
#else
    childPid = -1;
#endif
}

UciEngine::~UciEngine() {
    stopEngine();
}

bool UciEngine::startEngine(const std::string& path) {
    if (isRunning) stopEngine();

#ifdef _WIN32
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) return false;
    if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) return false;
    if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &saAttr, 0)) return false;
    if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) return false;

    STARTUPINFOA siStartInfo;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStd_OUT_Wr;
    siStartInfo.hStdOutput = hChildStd_OUT_Wr;
    siStartInfo.hStdInput = hChildStd_IN_Rd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    bool success = CreateProcessA(NULL, (LPSTR)path.c_str(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo);
    if (!success) return false;

    hProcess = piProcInfo.hProcess;
    CloseHandle(piProcInfo.hThread);
#else
    if (pipe(pipeIn) == -1 || pipe(pipeOut) == -1) return false;

    childPid = fork();
    if (childPid < 0) return false;

    if (childPid == 0) {
        dup2(pipeOut[0], STDIN_FILENO);
        dup2(pipeIn[1], STDOUT_FILENO);
        close(pipeOut[0]); close(pipeOut[1]);
        close(pipeIn[0]); close(pipeIn[1]);

        execlp(path.c_str(), path.c_str(), nullptr);
        exit(1); 
    } else {
        close(pipeOut[0]);
        close(pipeIn[1]);
    }
#endif

    isRunning = true;
    readerThread = std::thread(&UciEngine::readOutput, this);
    return true;
}

void UciEngine::stopEngine() {
    if (!isRunning) return;
    isRunning = false;

    sendCommand("quit");

#ifdef _WIN32
    if (hProcess) {
        WaitForSingleObject(hProcess, 1000);
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        CloseHandle(hChildStd_IN_Rd); CloseHandle(hChildStd_IN_Wr);
        CloseHandle(hChildStd_OUT_Rd); CloseHandle(hChildStd_OUT_Wr);
        hProcess = NULL;
    }
#else
    if (childPid > 0) {
        kill(childPid, SIGKILL);
        close(pipeIn[0]);
        close(pipeOut[1]);
        childPid = -1;
    }
#endif

    if (readerThread.joinable()) {
        readerThread.join();
    }
}

void UciEngine::sendCommand(const std::string& cmd) {
    if (!isRunning) return;
    std::string fullCmd = cmd + "\n";
#ifdef _WIN32
    DWORD written;
    WriteFile(hChildStd_IN_Wr, fullCmd.c_str(), fullCmd.length(), &written, NULL);
#else
    write(pipeOut[1], fullCmd.c_str(), fullCmd.length());
#endif
}

std::string UciEngine::getBestMove() {
    std::lock_guard<std::mutex> lock(moveMutex);
    std::string move = latestBestMove;
    latestBestMove = ""; 
    return move;
}

bool UciEngine::isEngineRunning() const {
    return isRunning;
}

void UciEngine::readOutput() {
    char buffer[4096];
    std::string accumulated = "";

    while (isRunning) {
        int bytesRead = 0;
#ifdef _WIN32
        DWORD read;
        if (!ReadFile(hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &read, NULL) || read == 0) break;
        bytesRead = read;
#else
        bytesRead = read(pipeIn[0], buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) break;
#endif
        buffer[bytesRead] = '\0';
        accumulated += buffer;

        size_t pos;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string line = accumulated.substr(0, pos);
            accumulated.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.find("bestmove") == 0) {
                std::istringstream iss(line);
                std::string token, move;
                iss >> token >> move;
                
                std::lock_guard<std::mutex> lock(moveMutex);
                latestBestMove = move;
            }
        }
    }
    isRunning = false;
}