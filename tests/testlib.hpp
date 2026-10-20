/* Chess-GUI-for-UCI — minimal self-contained test harness. GPL-2.0
 *
 * TEST_CASE("group/name") { CHECK(...); CHECK_EQ(a, b); REQUIRE(...); }
 * The test runner takes an optional name prefix: `core_tests perft/` runs
 * every case whose name starts with "perft/". CTest registers one entry per
 * group (see tests/CMakeLists.txt).
 */
#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace testlib {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry(){ static std::vector<Case> r; return r; }
inline int& failures(){ static int f = 0; return f; }
struct RequireFailed {};

struct Registrar {
    Registrar(const char* name, std::function<void()> fn){ registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg){
    failures()++;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, msg.c_str());
}

template<class A, class B>
std::string describe(const A& a, const B& b){
    std::ostringstream o; o << "'" << a << "' != '" << b << "'"; return o.str();
}

} // namespace testlib

#define TL_CAT2(a, b) a##b
#define TL_CAT(a, b) TL_CAT2(a, b)
#define TEST_CASE(name) \
    static void TL_CAT(tl_case_, __LINE__)(); \
    static testlib::Registrar TL_CAT(tl_reg_, __LINE__)(name, &TL_CAT(tl_case_, __LINE__)); \
    static void TL_CAT(tl_case_, __LINE__)()

#define CHECK(cond) \
    do { if (!(cond)) testlib::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define REQUIRE(cond) \
    do { if (!(cond)) { testlib::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")"); \
                        throw testlib::RequireFailed{}; } } while (0)
#define CHECK_EQ(a, b) \
    do { auto&& tl_a = (a); auto&& tl_b = (b); \
         if (!(tl_a == tl_b)) testlib::fail(__FILE__, __LINE__, \
             std::string(#a " == " #b ": ") + testlib::describe(tl_a, tl_b)); } while (0)
#define CHECK_MSG(cond, msg) \
    do { if (!(cond)) { std::ostringstream tl_o; tl_o << msg; \
                        testlib::fail(__FILE__, __LINE__, tl_o.str()); } } while (0)

// Define TESTLIB_MAIN in exactly one translation unit.
#ifdef TESTLIB_MAIN
int main(int argc, char** argv){
    std::string prefix = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (auto& c : testlib::registry()){
        if (c.name.compare(0, prefix.size(), prefix) != 0) continue;
        int before = testlib::failures();
        std::fprintf(stderr, "[ RUN  ] %s\n", c.name.c_str());
        try { c.fn(); }
        catch (testlib::RequireFailed&) {}
        catch (std::exception& e){ testlib::fail(__FILE__, __LINE__, std::string("exception: ") + e.what()); }
        std::fprintf(stderr, "[ %s ] %s\n", testlib::failures() == before ? " OK " : "FAIL", c.name.c_str());
        ran++;
    }
    if (!ran){ std::fprintf(stderr, "no test matches '%s'\n", prefix.c_str()); return 1; }
    std::fprintf(stderr, "%d case(s), %d failure(s)\n", ran, testlib::failures());
    return testlib::failures() ? 1 : 0;
}
#endif
