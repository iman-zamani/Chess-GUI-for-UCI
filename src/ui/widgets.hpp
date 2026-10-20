/* Chess-GUI-for-UCI — shared ImGui widgets and helpers. GPL-2.0 */
#pragma once
#include "engine_config.hpp"
#include <imgui.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ui {

struct Fonts { ImFont* body = nullptr; ImFont* mono = nullptr; ImFont* large = nullptr; };
Fonts& fonts();

// Colours shared by the screens.
constexpr ImVec4 kGood{0.55f, 0.90f, 0.55f, 1}, kBad{1.0f, 0.50f, 0.50f, 1},
                 kWarn{0.90f, 0.72f, 0.45f, 1}, kAccent{0.47f, 0.67f, 1.0f, 1},
                 kDim{0.62f, 0.62f, 0.68f, 1};

// Engine picker: a combo of the registry's engines by name. Returns true when
// the selection changed. `name` is the remembered engine name (may be stale).
bool engineCombo(const char* label, const EngineRegistry& reg, std::string& name);
const EngineConfig* findEngine(const EngineRegistry& reg, const std::string& name);

// Text field + "Browse..." (native dialog when available). Returns true on change.
bool pathInput(const char* label, std::string& path, const char* hint,
               const std::function<std::string()>& browse);

void helpMarker(const char* text);           // "(?)" with a tooltip
std::string clockText(long long ms);         // 4:05, 1:02:03
std::string timeStamp();                     // 20261020_143501 for file names

// A long-running task on a worker thread that reports progress as log lines.
class Job {
public:
    ~Job(){ abort(); }
    void start(const std::string& title, std::function<void(Job&)> fn);
    void abort();                           // request stop and wait for the thread
    void requestStop(){ abort_ = true; }
    bool running() const { return running_; }
    bool stopRequested() const { return abort_; }
    std::atomic<bool>& abortFlag(){ return abort_; }
    void log(const std::string& line);
    std::vector<std::string> lines() const;
    std::string title() const;
    std::string text() const;                // title + all lines, for copy/save
    bool empty() const;
private:
    std::thread th_;
    std::atomic<bool> abort_{false}, running_{false};
    mutable std::mutex mx_;
    std::vector<std::string> lines_;
    std::string title_;
};

// Scrolling, colour-coded view of a Job's log ("FAIL"/"??" red, ">>>" green),
// with Abort / Copy / Save buttons.
void jobPanel(Job& job, const std::function<std::string()>& saveDialog,
              const std::function<void(const std::string&)>& toast);

}
