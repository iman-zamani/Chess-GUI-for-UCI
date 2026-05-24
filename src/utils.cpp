#include "utils.hpp"
#include <array>
#include <algorithm>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <cstdio>
#endif

#ifndef _WIN32
struct PipeDeleter {
    void operator()(FILE* f) const {
        if (f) pclose(f);
    }
};

std::string execCommand(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, PipeDeleter> pipe(popen(cmd, "r"));
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    // Clean up terminal returns
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    return result;
}
#endif


std::string openFileDialog() {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Executables\0*.exe;*.bat;*.cmd\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select Engine Executable";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
#elif defined(__APPLE__)
    return execCommand("osascript -e 'POSIX path of (choose file with prompt \"Select Engine Executable\")'");
#else
    
    std::string res;

    // 1. GNOME / Standard Linux
    res = execCommand("zenity --file-selection --title=\"Select Engine Executable\" 2>/dev/null");
    if (!res.empty()) return res;

    // 2. KDE Fallback
    res = execCommand("kdialog --getopenfilename / 2>/dev/null");
    if (!res.empty()) return res;

    // 3. Ultimate Python Tkinter Fallback (Works on almost all distros)
    res = execCommand("python3 -c \"import tkinter as tk; from tkinter import filedialog; root = tk.Tk(); root.withdraw(); print(filedialog.askopenfilename())\" 2>/dev/null");
    return res;
#endif
}