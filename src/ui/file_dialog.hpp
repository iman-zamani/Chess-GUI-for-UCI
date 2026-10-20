/* Chess-GUI-for-UCI — native file dialogs. GPL-2.0 */
#pragma once
#include <string>
#include <vector>

// Native open/save/folder dialogs (portable-file-dialogs: Win32 COM, macOS
// osascript, Linux zenity/kdialog). Each returns "" when cancelled or when
// no dialog backend is available; callers keep a typed path as fallback.
namespace filedialog {
bool available();
// filters: pairs of {"PGN files", "*.pgn"}; "*" for all files
std::string openFile(const std::string& title, const std::string& startDir,
                     const std::vector<std::string>& filters = {"All files", "*"});
std::string saveFile(const std::string& title, const std::string& startPath,
                     const std::vector<std::string>& filters = {"All files", "*"});
std::string selectFolder(const std::string& title, const std::string& startDir);
}
