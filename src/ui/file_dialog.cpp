/* Chess-GUI-for-UCI — native file dialogs. GPL-2.0 */
#include "ui/file_dialog.hpp"
#include <portable-file-dialogs.h>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace filedialog {

#if !defined(_WIN32) && !defined(__APPLE__)
// Linux/BSD: portable-file-dialogs drives an external dialog program.
static bool haveDialogProgram(){
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    std::error_code ec;
    while (std::getline(ss, dir, ':'))
        for (const char* prog : {"zenity", "kdialog", "matedialog", "qarma"})
            if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / prog, ec)) return true;
    return false;
}
#endif

bool available(){
    // scripted/CI runs must never block on a modal dialog
    if (std::getenv("CHESSGUI_NO_DIALOGS")) return false;
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    static const bool have = haveDialogProgram();
    return have;
#endif
}

std::string openFile(const std::string& title, const std::string& startDir,
                     const std::vector<std::string>& filters){
    if (!available()) return "";
    auto r = pfd::open_file(title, startDir, filters, pfd::opt::none).result();
    return r.empty() ? "" : r[0];
}

std::string saveFile(const std::string& title, const std::string& startPath,
                     const std::vector<std::string>& filters){
    if (!available()) return "";
    return pfd::save_file(title, startPath, filters, pfd::opt::none).result();
}

std::string selectFolder(const std::string& title, const std::string& startDir){
    if (!available()) return "";
    return pfd::select_folder(title, startDir).result();
}

}
