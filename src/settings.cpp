/* Chess-GUI-for-UCI — persistent user settings (settings.json). GPL-2.0 */
#include "settings.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

// One list of (key, member) bindings drives both directions.
template<class S, class F>
static void forEachField(S& s, F&& f){
    f("windowW", s.windowW); f("windowH", s.windowH);
    f("theme", s.theme); f("boardTheme", s.boardTheme); f("showCoordinates", s.showCoordinates);
    f("lastPgnDir", s.lastPgnDir); f("lastEngineDir", s.lastEngineDir);
    f("lastOpeningsDir", s.lastOpeningsDir); f("lastSaveDir", s.lastSaveDir);
    f("tcBaseMs", s.tcBaseMs); f("tcIncMs", s.tcIncMs);
    f("analysisMultiPV", s.analysisMultiPV); f("showArrows", s.showArrows);
    f("opponentEngine", s.opponentEngine); f("analysisEngine", s.analysisEngine);
    f("perftEngine", s.perftEngine); f("accTestEngine", s.accTestEngine);
    f("accRefEngine", s.accRefEngine); f("judgeEngine", s.judgeEngine);
    f("uciCheckEngine", s.uciCheckEngine);
    f("accCount", s.accCount); f("accTestMs", s.accTestMs); f("accRefMs", s.accRefMs);
    f("accMultiPV", s.accMultiPV);
    f("matchGamesPerPair", s.matchGamesPerPair);
}

bool Settings::load(const std::string& file, std::string* err){
    std::ifstream f(fs::u8path(file));
    if (!f) return true;
    std::stringstream ss; ss << f.rdbuf();
    try {
        json j = json::parse(ss.str());
        if (!j.is_object()) throw std::runtime_error("not a JSON object");
        forEachField(*this, [&](const char* key, auto& member){
            using T = std::decay_t<decltype(member)>;
            if (j.contains(key)){
                try { member = j[key].get<T>(); } catch (...) {}   // wrong type: keep default
            }
        });
        raw_ = ss.str();
    } catch (std::exception& e){
        if (err) *err = file + ": " + e.what();
        return false;
    }
    return true;
}

bool Settings::save(const std::string& file, std::string* err) const{
    json j = json::object();
    if (!raw_.empty()){
        try { j = json::parse(raw_); if (!j.is_object()) j = json::object(); } catch (...) {}
    }
    forEachField(*this, [&](const char* key, const auto& member){ j[key] = member; });
    std::error_code ec;
    fs::create_directories(fs::u8path(file).parent_path(), ec);
    std::string tmp = file + ".tmp";
    {
        std::ofstream f(fs::u8path(tmp), std::ios::binary | std::ios::trunc);
        if (!f){ if (err) *err = "cannot write " + tmp; return false; }
        f << j.dump(2) << "\n";
    }
    fs::rename(fs::u8path(tmp), fs::u8path(file), ec);
    if (ec){ if (err) *err = "cannot replace " + file + ": " + ec.message(); return false; }
    return true;
}
