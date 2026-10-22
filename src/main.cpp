/* Chess-GUI-for-UCI — entry point. GPL-2.0, (C) 2025 Iman Zamani */
#include "gui.hpp"
#include <cstdio>
#include <string>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#include <libgen.h>
#include <climits>
#include <cstdint>
#else
#include <unistd.h>
#include <libgen.h>
#include <climits>
#endif

// Run relative to the executable, not the caller's working directory.
// This makes Resources/ load correctly when the app is double-clicked
// (Finder/Explorer launch with cwd = / or home) and inside a macOS .app
// bundle (binary in Contents/MacOS, data in Contents/Resources: run from Contents/,
// since data files in Contents/MacOS would break the bundle's code signature).
static void chdirToExeDir(){
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0){
        for (int i=(int)n-1; i>=0; i--)
            if (buf[i]=='\\' || buf[i]=='/'){ buf[i]=0; break; }
        _chdir(buf);
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0){
        char real[PATH_MAX];
        if (realpath(buf, real)){
            int rc = chdir(dirname(real));
            if (rc == 0 && access("Resources", F_OK) != 0 && access("../Resources", F_OK) == 0) rc = chdir("..");
            (void)rc;
        }
    }
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (n > 0){ buf[n]=0; int rc = chdir(dirname(buf)); (void)rc; }
#endif
}

int main(int argc, char** argv){
    // Command line: [--script FILE] (see ui_script.hpp).
    std::string scriptPath;
    for (int i=1; i<argc; i++){
        std::string a = argv[i];
        if (a=="--script" && i+1<argc) scriptPath = argv[++i];
        else if (a.rfind("-psn", 0)==0) continue;       // old macOS Finder launch arg
        else { fprintf(stderr, "usage: %s [--script FILE]\n", argv[0]); return 2; }
    }
    // Load the script before leaving the caller's directory, so relative paths
    // in it (and the script path itself) mean what the caller expects.
    App app;
    if (!scriptPath.empty() && !app.loadScript(scriptPath)) return 2;
    if (!scriptPath.empty()){                 // a scripted run must never block on a modal dialog
#ifdef _WIN32
        _putenv_s("CHESSGUI_NO_DIALOGS", "1");
#else
        setenv("CHESSGUI_NO_DIALOGS", "1", 1);
#endif
    }
    chdirToExeDir();
    if (!app.init()){
        fprintf(stderr, "Initialization failed. Ensure the Resources folder is next to the executable.\n");
        return 1;
    }
    app.run();
    return app.scriptFailed() ? 1 : 0;
}
