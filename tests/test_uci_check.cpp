/* Chess-GUI-for-UCI — UCI compliance checker tests (good and broken mocks). GPL-2.0 */
#include "testlib.hpp"
#include "uci_check.hpp"
#include <algorithm>

static const std::string kMock = MOCK_ENGINE_PATH;

static std::vector<CheckResult> run(std::vector<std::string> args, std::vector<std::string> only = {}){
    EngineConfig c; c.path = kMock; c.args = args;
    UciCheckOptions o; o.quick = true; o.only = only;
    return runUciChecks(c, o);
}
static const CheckResult* find(const std::vector<CheckResult>& r, const std::string& id){
    for (auto& x : r) if (x.id == id) return &x;
    return nullptr;
}
#define EXPECT_STATUS(results, id, st) do { const auto r_ = (results); const CheckResult* x_ = find(r_, id); \
    CHECK_MSG(x_ != nullptr, "missing check " << id); \
    if (x_) CHECK_MSG(x_->status == st, id << ": " << checkStatusName(x_->status) << " - " << x_->detail); } while (0)

TEST_CASE("ucicheck/good-engine-passes-everything"){
    auto r = run({});
    CHECK_EQ(r.size(), uciCheckIds().size());
    for (auto& x : r) CHECK_MSG(x.status == CheckStatus::Pass, x.id << ": " << checkStatusName(x.status) << " - " << x.detail);
    CheckSummary s = summarize(r);
    CHECK_EQ(s.fail, 0);
    std::string text = checkReportText("Mock", r);
    CHECK(text.find("PASS") != std::string::npos);
}

TEST_CASE("ucicheck/no-uciok"){
    auto r = run({"--no-uciok"});
    REQUIRE(r.size() == 1);
    EXPECT_STATUS(r, "handshake", CheckStatus::Fail);
    CHECK(!r[0].log.empty());                                   // the log excerpt is attached
}

TEST_CASE("ucicheck/busy-readyok"){
    EXPECT_STATUS(run({"--busy-readyok"}, {"isready-during-search"}), "isready-during-search", CheckStatus::Fail);
}

TEST_CASE("ucicheck/slow-readyok"){
    EXPECT_STATUS(run({"--slow-readyok", "1500"}, {"isready-idle"}), "isready-idle", CheckStatus::Warn);
}

TEST_CASE("ucicheck/ignores-stop"){
    EXPECT_STATUS(run({"--ignore-stop"}, {"stop"}), "stop", CheckStatus::Fail);
}

TEST_CASE("ucicheck/output-after-bestmove"){
    auto r = run({"--output-after-bestmove"}, {"go-modes"});
    EXPECT_STATUS(r, "go-modes", CheckStatus::Fail);
    const CheckResult* g = find(r, "go-modes");
    if (g) CHECK(g->detail.find("after bestmove") != std::string::npos);
}

TEST_CASE("ucicheck/illegal-and-none-bestmove"){
    EXPECT_STATUS(run({"--illegal-bestmove"}, {"go-modes"}), "go-modes", CheckStatus::Fail);
    EXPECT_STATUS(run({"--none-bestmove"}, {"positions"}), "positions", CheckStatus::Fail);
}

TEST_CASE("ucicheck/mate-blind"){
    EXPECT_STATUS(run({"--mate-blind"}, {"mate-scores"}), "mate-scores", CheckStatus::Warn);
}

TEST_CASE("ucicheck/time-hog-loses-on-time"){
    EXPECT_STATUS(run({"--time-hog"}, {"time-management"}), "time-management", CheckStatus::Fail);
}

TEST_CASE("ucicheck/no-quit"){
    EXPECT_STATUS(run({"--no-quit"}, {"quit"}), "quit", CheckStatus::Fail);
}

TEST_CASE("ucicheck/crash-is-a-failure-not-a-hang"){
    auto r = run({"--crash-on-go", "2"}, {"go-modes"});
    EXPECT_STATUS(r, "go-modes", CheckStatus::Fail);
}
