/* Chess-GUI-for-UCI — UCI console window. GPL-2.0 */
#pragma once
#include "uci_engine.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Live log of every line sent to and received from each engine process
// (stdin '>', stdout '<', stderr '!', GUI notes '#'), with a filter, pause,
// copy, save, and a command line that sends raw text to a live engine.
// Works in every screen: any engine the program starts shows up here.
class UciConsole {
public:
    void draw(bool* open, const std::function<std::string()>& saveDialog,
              const std::function<void(const std::string&)>& toast);
private:
    struct Session {
        uint64_t id = 0;
        std::string label;
        std::shared_ptr<UciLogBuffer> log;
        uint64_t cursor = 0;
        std::vector<UciLogLine> lines;
        bool alive = false;
    };
    void refresh();
    bool visible(const UciLogLine& l) const;
    std::string visibleText(const Session& s) const;

    std::vector<Session> sessions_;
    uint64_t selected_ = 0;
    std::string filter_;
    bool showIn_ = true, showOut_ = true, showErr_ = true, showNotes_ = true;
    bool hideInfo_ = false;
    bool paused_ = false;
    std::string cmd_;
    std::vector<std::string> history_;
    int historyPos_ = -1;
    bool scrollToEnd_ = true;
};
