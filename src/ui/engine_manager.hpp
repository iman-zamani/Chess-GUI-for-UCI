/* Chess-GUI-for-UCI — engine manager window (ImGui). GPL-2.0 */
#pragma once
#include "engine_config.hpp"
#include "uci_engine.hpp"
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

// Add / edit / remove engines: name, executable, arguments, working
// directory and UCI options, with a "Test launch" that runs the handshake
// in the background and shows what the engine said.
class EngineManagerWindow {
public:
    struct Callbacks {
        std::function<void()> save;                       // persist the registry
        std::function<void()> close;                      // leave the screen
        std::function<std::string(const std::string&)> browseFile;   // may be empty
    };
    // Draws a full-window panel. `registry` is edited in place.
    void draw(EngineRegistry& registry, const Callbacks& cb);
    void select(int index){ selected_ = index; }

private:
    struct LaunchResult {
        bool ok = false;
        std::string name, author, error;
        long long ms = 0;
        std::vector<UciOption> options;
        std::vector<UciLogLine> log;
    };
    void startTest(const EngineConfig& cfg);
    void drawEditor(EngineConfig& c, const Callbacks& cb);
    void drawOptions(EngineConfig& c, const Callbacks& cb);

    int selected_ = 0;
    std::string argsEdit_;                 // argument line being edited
    int argsFor_ = -1;                     // engine index argsEdit_ belongs to
    std::future<LaunchResult> pending_;
    std::string pendingPath_;
    std::map<std::string, LaunchResult> results_;   // by executable path
};
