/* Chess-GUI-for-UCI — entry point. GPL-2.0, (C) 2025 Iman Zamani */
#include "gui.hpp"
#include <cstdio>

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
// bundle (binary lives in Contents/MacOS, Resources/ is copied next to it).
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
        if (realpath(buf, real)){ int rc = chdir(dirname(real)); (void)rc; }
    }
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (n > 0){ buf[n]=0; int rc = chdir(dirname(buf)); (void)rc; }
#endif
}

int main(){
    chdirToExeDir();
    App app;
    if (!app.init()){
        fprintf(stderr, "Initialization failed. Ensure the Resources folder is next to the executable.\n");
        return 1;
    }
    app.run();
    return 0;
}
