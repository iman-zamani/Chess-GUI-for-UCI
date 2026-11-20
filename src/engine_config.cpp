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
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
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

std::vector<std::string> utf8Args(int argc, char** argv){
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    if (wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n)){
        for (int i = 1; i < n; i++) out.push_back(fs::path(w[i]).u8string());
        LocalFree(w);
    }
#else
    for (int i = 1; i < argc; i++) out.push_back(argv[i]);
#endif
    return out;
}

std::string envUtf8(const char* name){
#ifdef _WIN32
    std::wstring w(name, name + std::strlen(name));          // names are ASCII
    const wchar_t* v = _wgetenv(w.c_str());
    return v ? fs::path(v).u8string() : std::string();
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

#ifdef _WIN32
// Where a known folder really is: Documents can be redirected or moved to OneDrive, so it is
// not always %USERPROFILE%\Documents. Empty if the shell can't say.
static fs::path knownFolder(const KNOWNFOLDERID& id){
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) r = fs::path(std::wstring(p));
    CoTaskMemFree(p);                                        // freed whether the call succeeded or not
    return r;
}
#endif

// The default data folder, worked out once: it does not change while the program runs, and
// the match screen asks for it every frame.
static fs::path defaultDataDir(){
    fs::path base;
    std::error_code ec;
#ifdef _WIN32
    fs::path docs = knownFolder(FOLDERID_Documents);
    if (!docs.empty()) base = docs / "Chess-GUI-for-UCI";
    else if (std::string home = envUtf8("USERPROFILE"); !home.empty()) base = fs::u8path(home) / "Documents" / "Chess-GUI-for-UCI";
#elif defined(__APPLE__)
    std::string home = envUtf8("HOME");
    if (!home.empty()) base = fs::u8path(home) / "Documents" / "Chess-GUI-for-UCI";
#else
    std::string home = envUtf8("HOME"), xdg = envUtf8("XDG_DATA_HOME");
    if (!home.empty() && fs::is_directory(fs::u8path(home) / "Documents", ec)) base = fs::u8path(home) / "Documents" / "Chess-GUI-for-UCI";
    else if (!xdg.empty()) base = fs::u8path(xdg) / "chess-gui-for-uci";
    else if (!home.empty()) base = fs::u8path(home) / ".local" / "share" / "chess-gui-for-uci";
#endif
    if (base.empty()) base = fs::temp_directory_path(ec) / "Chess-GUI-for-UCI";
    return base;
}

std::string userDataDir(){
    std::string over = envUtf8("CHESSGUI_DATA_DIR");
    if (!over.empty()) return over;
    static const std::string def = defaultDataDir().u8string();
    return def;
}

static fs::path defaultConfigDir(){
    fs::path base;
#ifdef _WIN32
    fs::path roaming = knownFolder(FOLDERID_RoamingAppData);
    if (!roaming.empty()) base = roaming / "ChessGUIforUCI";
    else if (std::string a = envUtf8("APPDATA"); !a.empty()) base = fs::u8path(a) / "ChessGUIforUCI";
#elif defined(__APPLE__)
    if (std::string h = envUtf8("HOME"); !h.empty()) base = fs::u8path(h) / "Library" / "Application Support" / "ChessGUIforUCI";
#else
    if (std::string x = envUtf8("XDG_CONFIG_HOME"); !x.empty()) base = fs::u8path(x) / "chess-gui-for-uci";
    else if (std::string h = envUtf8("HOME"); !h.empty()) base = fs::u8path(h) / ".config" / "chess-gui-for-uci";
#endif
    if (base.empty()) base = fs::current_path() / "config";
    return base;
}

std::string userConfigDir(){
    if (std::string o = envUtf8("CHESSGUI_CONFIG_DIR"); !o.empty()) return o;
    static const fs::path base = defaultConfigDir();         // (asked for on every frame of some screens)
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
    if (std::string e = envUtf8("CHESSGUI_RESOURCES"); !e.empty()) roots.push_back(fs::u8path(e));
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
