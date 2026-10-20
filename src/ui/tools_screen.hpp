/* Chess-GUI-for-UCI — engine testing tools screen. GPL-2.0 */
#pragma once
#include "ui/widgets.hpp"

class App;

// Move-generator tests (GUI self-test, engine "go perft" suite) and the
// accuracy test against a reference engine, with a shared output log.
class ToolsScreen {
public:
    explicit ToolsScreen(App& app) : app_(app) {}
    void draw();
    bool busy() const { return job_.running(); }
    ui::Job& job(){ return job_; }
private:
    void drawPerftTab();
    void drawAccuracyTab();
    void runSelfTest();
    void runEnginePerft(const EngineConfig& cfg);
    void runAccuracy(const EngineConfig& test, const EngineConfig& ref);

    App& app_;
    ui::Job job_;
    std::string suitePath_ = "Resources/perftsuite.epd";
    int perftDepth_ = 4;
};
