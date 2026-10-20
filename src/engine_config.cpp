/* Chess-GUI-for-UCI — engine configurations (engines.json). GPL-2.0 */
#include "engine_config.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string EngineConfig::effectiveWorkingDir() const{
    if (!workingDir.empty()) return workingDir;
    std::error_code ec;
    fs::path p = fs::absolute(fs::u8path(path), ec);
    if (ec) p = fs::u8path(path);
    return p.parent_path().u8string();
}

const std::string* EngineConfig::option(const std::string& n) const{
    for (auto& o : options) if (o.first == n) return &o.second;
    return nullptr;
}

void EngineConfig::setOption(const std::string& n, const std::string& v){
    for (auto& o : options) if (o.first == n){ o.second = v; return; }
    options.push_back({n, v});
}

bool EngineRegistry::load(const std::string& file, std::string* err){
    list_.clear();
    std::ifstream f(fs::u8path(file));
    if (!f) return true;                                   // first run: nothing saved yet
    try {
        json j = json::parse(f);
        for (auto& e : j.at("engines")){
            EngineConfig c;
            c.name = e.value("name", "");
            c.path = e.at("path").get<std::string>();
            c.args = e.value("args", std::vector<std::string>{});
            c.workingDir = e.value("workingDir", "");
            c.protocol = e.value("protocol", "uci");
            if (e.contains("options"))
                for (auto& o : e["options"])
                    c.options.push_back({o.at(0).get<std::string>(), o.at(1).get<std::string>()});
            if (c.name.empty()) c.name = fs::u8path(c.path).filename().u8string();
            list_.push_back(c);
        }
    } catch (std::exception& ex){
        if (err) *err = file + ": " + ex.what();
        list_.clear();
        return false;
    }
    return true;
}

bool EngineRegistry::save(const std::string& file, std::string* err) const{
    json arr = json::array();
    for (auto& c : list_){
        if (c.discovered && c.options.empty() && c.args.empty() && c.workingDir.empty()) continue;
        json o = json::array();
        for (auto& kv : c.options) o.push_back({kv.first, kv.second});
        arr.push_back({{"name", c.name}, {"path", c.path}, {"args", c.args},
                       {"workingDir", c.workingDir}, {"options", o}, {"protocol", c.protocol}});
    }
    json j = {{"version", 1}, {"engines", arr}};
    std::error_code ec;
    fs::create_directories(fs::u8path(file).parent_path(), ec);
    // write to a temp file and rename, so a crash never leaves a half-written list
    std::string tmp = file + ".tmp";
    {
        std::ofstream f(fs::u8path(tmp), std::ios::binary | std::ios::trunc);
        if (!f){ if (err) *err = "cannot write " + tmp; return false; }
        f << j.dump(2) << "\n";
        if (!f){ if (err) *err = "write failed: " + tmp; return false; }
    }
    fs::rename(fs::u8path(tmp), fs::u8path(file), ec);
    if (ec){ if (err) *err = "cannot replace " + file + ": " + ec.message(); return false; }
    return true;
}

static bool looksExecutable(const fs::directory_entry& e){
    std::error_code ec;
    if (!e.is_regular_file(ec)) return false;
    std::string name = e.path().filename().u8string();
    if (name.empty() || name[0] == '.') return false;
    std::string ext = e.path().extension().u8string();
    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
#ifdef _WIN32
    return ext == ".exe";
#else
    if (ext == ".txt" || ext == ".md" || ext == ".nnue" || ext == ".bin" || ext == ".json") return false;
    auto perms = e.status(ec).permissions();
    return (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
#endif
}

int EngineRegistry::discover(const std::string& dir){
    int added = 0;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(dir), ec)) return 0;
    std::vector<fs::path> found;
    for (auto& e : fs::directory_iterator(fs::u8path(dir), ec)){
        if (looksExecutable(e)) found.push_back(e.path());
        else if (e.is_directory(ec)){                      // one level down: engines/<name>/<binary>
            for (auto& s : fs::directory_iterator(e.path(), ec))
                if (looksExecutable(s)) found.push_back(s.path());
        }
    }
    std::sort(found.begin(), found.end());
    for (auto& p : found){
        std::string ps = p.u8string();
        if (findByPath(ps)) continue;
        EngineConfig c;
        c.path = ps; c.name = p.filename().u8string(); c.discovered = true;
        list_.push_back(c);
        added++;
    }
    return added;
}

EngineConfig* EngineRegistry::findByPath(const std::string& p){
    for (auto& c : list_) if (c.path == p) return &c;
    return nullptr;
}
EngineConfig* EngineRegistry::findByName(const std::string& n){
    for (auto& c : list_) if (c.name == n) return &c;
    return nullptr;
}
bool EngineRegistry::remove(size_t i){
    if (i >= list_.size()) return false;
    list_.erase(list_.begin() + (long)i);
    return true;
}

std::vector<std::string> splitArgs(const std::string& line, std::string* err){
    std::vector<std::string> out;
    std::string cur;
    bool have = false;                     // distinguishes "" (empty arg) from no arg
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++){
        char c = line[i];
        if (quote == '\''){
            if (c == '\'') quote = 0; else cur += c;
        } else if (quote == '"'){
            if (c == '"') quote = 0;
            else if (c == '\\' && i + 1 < line.size() && (line[i+1] == '"' || line[i+1] == '\\')) cur += line[++i];
            else cur += c;
        } else if (c == '\'' || c == '"'){
            quote = c; have = true;
        } else if (c == '\\' && i + 1 < line.size() && std::strchr("'\"\\ \t", line[i+1])){
            cur += line[++i]; have = true;         // other backslashes stay (Windows paths)
        } else if (c == ' ' || c == '\t'){
            if (have){ out.push_back(cur); cur.clear(); have = false; }
        } else { cur += c; have = true; }
    }
    if (quote && err) *err = "unterminated quote";
    if (have) out.push_back(cur);
    return out;
}

std::string joinArgs(const std::vector<std::string>& args){
    std::string s;
    for (auto& a : args){
        if (!s.empty()) s += ' ';
        bool plain = !a.empty() && a.find_first_of(" \t'\"\\") == std::string::npos;
        if (plain){ s += a; continue; }
        s += '\'';                         // single quotes: everything literal except '
        for (char c : a){ if (c == '\'') s += "'\\''"; else s += c; }
        s += '\'';
    }
    return s;
}

std::string userConfigDir(){
    if (const char* o = std::getenv("CHESSGUI_CONFIG_DIR")) if (*o) return o;
    fs::path base;
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA")) base = fs::u8path(a) / "ChessGUIforUCI";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) base = fs::u8path(h) / "Library" / "Application Support" / "ChessGUIforUCI";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) base = fs::u8path(x) / "chess-gui-for-uci";
    else if (const char* h = std::getenv("HOME")) base = fs::u8path(h) / ".config" / "chess-gui-for-uci";
#endif
    if (base.empty()) base = fs::current_path() / "config";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.u8string();
}

std::string executableDir(){
    fs::path p;
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) p = fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
    char buf[PATH_MAX]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0) p = buf;
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) p = std::string(buf, (size_t)n);
#endif
    if (p.empty()) return "";
    std::error_code ec;
    fs::path c = fs::canonical(p, ec);
    return (ec ? p : c).parent_path().u8string();
}

std::string findResource(const std::string& rel){
    std::vector<fs::path> roots;
    if (const char* e = std::getenv("CHESSGUI_RESOURCES")) roots.push_back(fs::u8path(e));
    std::string exe = executableDir();
    if (!exe.empty()){
        roots.push_back(fs::u8path(exe) / "Resources");
        roots.push_back(fs::u8path(exe) / ".." / "Resources");          // macOS bundle: Contents/Resources
        roots.push_back(fs::u8path(exe) / ".." / "share" / "chess-gui-for-uci");
    }
    roots.push_back(fs::current_path() / "Resources");
    for (auto& r : roots){
        std::error_code ec;
        fs::path f = r / fs::u8path(rel);
        if (fs::is_regular_file(f, ec)) return f.u8string();
    }
    return "";
}
