/* Chess-GUI-for-UCI — entry point. GPL-2.0, (C) 2025 Iman Zamani */
#include "gui.hpp"
#include "engine_config.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Run relative to the executable, not the caller's working directory.
// This makes Resources/ load correctly when the app is double-clicked
// (Finder/Explorer launch with cwd = / or home) and inside a macOS .app
// bundle (binary in Contents/MacOS, data in Contents/Resources: run from Contents/,
// since data files in Contents/MacOS would break the bundle's code signature).
// Nothing the user creates is written here: see userDataDir().
static void chdirToExeDir(){
    namespace fs = std::filesystem;
    std::string dir = executableDir();                       // wide on Windows: any install path works
    if (dir.empty()) return;
    std::error_code ec;
    fs::current_path(fs::u8path(dir), ec);
#ifdef __APPLE__
    if (!ec && !fs::exists("Resources", ec) && fs::exists("../Resources", ec)) fs::current_path("..", ec);
#endif
}

int main(int argc, char** argv){
    // Command line: [--script FILE] (see ui_script.hpp).
    std::string scriptPath;
    std::vector<std::string> args = utf8Args(argc, argv);
    for (size_t i=0; i<args.size(); i++){
        const std::string& a = args[i];
        if (a=="--script" && i+1<args.size()) scriptPath = args[++i];
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
