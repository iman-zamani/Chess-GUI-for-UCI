/* Chess-GUI-for-UCI — engine configurations (engines.json). GPL-2.0 */
#pragma once
#include <string>
#include <utility>
#include <vector>

// How to launch one engine, and which UCI options to set after the handshake.
struct EngineConfig {
    std::string name;                     // display name (defaults to the file name)
    std::string path;                     // executable
    std::vector<std::string> args;        // command-line arguments
    std::string workingDir;               // empty = the executable's own directory
    std::vector<std::pair<std::string, std::string>> options;   // UCI option name -> value
    std::string protocol = "uci";
    bool discovered = false;              // auto-found in Resources/engines (not saved)

    // The directory the engine process starts in. Engines that load NNUE
    // nets, books or tablebases by relative path need this to be their own
    // directory, not the GUI's.
    std::string effectiveWorkingDir() const;
    const std::string* option(const std::string& name) const;
    void setOption(const std::string& name, const std::string& value);
};

// Persistent list of engines. Saved as JSON:
//   { "version": 1, "engines": [ { "name", "path", "args", "workingDir", "options", "protocol" } ] }
class EngineRegistry {
public:
    bool load(const std::string& file, std::string* err = nullptr);   // missing file = empty list
    bool save(const std::string& file, std::string* err = nullptr) const;
    // Adds every executable file in `dir` that isn't already listed (by path).
    // Returns the number of engines added.
    int discover(const std::string& dir);

    std::vector<EngineConfig>& engines(){ return list_; }
    const std::vector<EngineConfig>& engines() const { return list_; }
    EngineConfig* findByPath(const std::string& path);
    EngineConfig* findByName(const std::string& name);
    void add(const EngineConfig& c){ list_.push_back(c); }
    bool remove(size_t index);
private:
    std::vector<EngineConfig> list_;
};

// Command-line arguments as one editable line. Splitting follows POSIX shell
// rules for quotes: '...' is literal, "..." allows \" and \\. Outside quotes a
// backslash escapes only a quote, a backslash or a space, so Windows paths like
// C:\nets\big.nnue can be typed as they are.
std::vector<std::string> splitArgs(const std::string& line, std::string* err = nullptr);
std::string joinArgs(const std::vector<std::string>& args);   // inverse of splitArgs

// Per-user configuration directory (created on demand):
//   Windows <Roaming AppData>\ChessGUIforUCI (the shell's known folder, else %APPDATA%),
//   macOS ~/Library/Application Support/ChessGUIforUCI,
//   Linux $XDG_CONFIG_HOME/chess-gui-for-uci (default ~/.config/chess-gui-for-uci).
// CHESSGUI_CONFIG_DIR overrides it (used by the tests).
std::string userConfigDir();

// Where match records and saved games go unless the user chose another folder (not created
// here: whoever writes there does that, so showing the path touches nothing):
// Documents/Chess-GUI-for-UCI on Windows (the shell's Documents folder, which can be redirected
// or on OneDrive; else %USERPROFILE%\Documents) and macOS; on Linux ~/Documents/Chess-GUI-for-UCI
// when ~/Documents exists, else $XDG_DATA_HOME/chess-gui-for-uci (default ~/.local/share/...).
// Never the program's own folder: an app bundle, an AppImage or Program Files can't be written.
// CHESSGUI_DATA_DIR overrides it (used by the tests).
std::string userDataDir();
// The program's arguments (without argv[0]) as UTF-8. On Windows argv is in the ANSI code
// page, which can't hold every path: the arguments are taken from the wide command line.
std::vector<std::string> utf8Args(int argc, char** argv);
// An environment variable as UTF-8 ("" if unset): Windows keeps them as UTF-16.
std::string envUtf8(const char* name);
// Directory of the running executable ("" if unknown).
std::string executableDir();
// A bundled resource, e.g. "elo/model.json": looked up in $CHESSGUI_RESOURCES,
// then Resources/ next to the executable, then ./Resources. "" if not found.
std::string findResource(const std::string& relPath);
