/* Chess-GUI-for-UCI — scripted UI driver (screenshots, smoke tests). GPL-2.0
 *
 * `chess-gui --script FILE` replays a list of input steps against the live
 * window, one step per frame. Used to take documentation screenshots and to
 * smoke-test the GUI headlessly (e.g. under Xvfb in CI). Coordinates are in
 * logical UI units (the 1280x800 design canvas), not physical pixels.
 *
 *   wait <ms>                  pause
 *   move <x> <y>               move the mouse
 *   click <x> <y>              press + release the left button
 *   drag <x1> <y1> <x2> <y2>   press at 1, move to 2, release
 *   key <Name> [ctrl]          F, Left, Right, Up, Down, Escape, Enter, ...
 *   text <string...>           type characters
 *   shot <file.png>            save the next rendered frame
 *   quit                       close the window
 *   # comment
 */
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

struct UiScriptStep {
    std::string op;
    std::vector<float> nums;
    std::string arg;            // key name / text / file
    bool ctrl = false;
};

inline bool loadUiScript(const std::string& path, std::vector<UiScriptStep>& out,
                         std::string* err){
    std::ifstream f(path);
    if (!f){ if (err) *err = "cannot open " + path; return false; }
    std::string line; int no = 0;
    while (std::getline(f, line)){
        no++;
        while (!line.empty() && (line.back()=='\r' || line.back()==' ')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line[b]=='#') continue;
        std::istringstream ss(line.substr(b));
        UiScriptStep s; ss >> s.op;
        if (s.op=="text" || s.op=="shot"){
            std::getline(ss, s.arg);
            if (!s.arg.empty() && s.arg[0]==' ') s.arg.erase(0, 1);
        } else if (s.op=="key"){
            ss >> s.arg; std::string mod; if (ss >> mod) s.ctrl = (mod=="ctrl");
        } else {
            float v; while (ss >> v) s.nums.push_back(v);
        }
        size_t need = s.op=="wait" ? 1 : (s.op=="move"||s.op=="click") ? 2 : s.op=="drag" ? 4 : 0;
        bool known = s.op=="wait"||s.op=="move"||s.op=="click"||s.op=="drag"||s.op=="key"
                  || s.op=="text"||s.op=="shot"||s.op=="quit";
        if (!known || s.nums.size() < need || ((s.op=="key"||s.op=="shot") && s.arg.empty())){
            if (err) *err = path + ":" + std::to_string(no) + ": bad step '" + line + "'";
            return false;
        }
        out.push_back(s);
    }
    return true;
}
