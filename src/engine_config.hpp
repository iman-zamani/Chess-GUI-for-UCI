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

// Per-user configuration directory (created on demand):
//   Windows %APPDATA%\ChessGUIforUCI, macOS ~/Library/Application Support/ChessGUIforUCI,
//   Linux $XDG_CONFIG_HOME/chess-gui-for-uci (default ~/.config/chess-gui-for-uci).
// CHESSGUI_CONFIG_DIR overrides it (used by the tests).
std::string userConfigDir();
