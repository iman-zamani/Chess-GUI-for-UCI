/* Chess-GUI-for-UCI — shared ImGui widgets and helpers. GPL-2.0 */
#include "ui/widgets.hpp"
#include <misc/cpp/imgui_stdlib.h>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace ui {

Fonts& fonts(){ static Fonts f; return f; }

const EngineConfig* findEngine(const EngineRegistry& reg, const std::string& name){
    for (auto& c : reg.engines()) if (c.name == name) return &c;
    return nullptr;
}

bool engineCombo(const char* label, const EngineRegistry& reg, std::string& name){
    bool changed = false;
    const EngineConfig* cur = findEngine(reg, name);
    if (!cur && !reg.engines().empty()){ name = reg.engines()[0].name; cur = &reg.engines()[0]; changed = true; }
    const char* preview = cur ? cur->name.c_str() : "(no engines - add one in Engines)";
    if (ImGui::BeginCombo(label, preview)){
        for (size_t i = 0; i < reg.engines().size(); i++){
            const EngineConfig& c = reg.engines()[i];
            ImGui::PushID((int)i);
            if (ImGui::Selectable(c.name.c_str(), cur == &c)){ name = c.name; changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.path.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool pathInput(const char* label, std::string& path, const char* hint,
               const std::function<std::string()>& browse){
    bool changed = false;
    ImGui::PushID(label);
    float bw = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-bw - ImGui::GetStyle().ItemSpacing.x);
    changed |= ImGui::InputTextWithHint("##path", hint, &path);
    ImGui::SameLine();
    if (ImGui::Button("Browse...") && browse){
        std::string p = browse();
        if (!p.empty()){ path = p; changed = true; }
    }
    ImGui::PopID();
    return changed;
}

std::string humanCount(long long n){
    char b[32];
    if (n >= 1000000000) std::snprintf(b, sizeof b, "%.2fG", n / 1e9);
    else if (n >= 1000000) std::snprintf(b, sizeof b, "%.2fM", n / 1e6);
    else if (n >= 10000) std::snprintf(b, sizeof b, "%.1fk", n / 1e3);
    else std::snprintf(b, sizeof b, "%lld", n);
    return b;
}

void searchStats(const char* id, const std::vector<MoveStat>& all, bool white){
    std::vector<const MoveStat*> st;
    for (auto& s : all) if (s.white == white) st.push_back(&s);
    ImGui::PushID(id);
    if (st.empty()){ ImGui::TextDisabled("No engine moves yet."); ImGui::PopID(); return; }
    const MoveStat& l = *st.back();
    ImGui::Text("last move: depth %d/%d  nodes %s  nps %s  hash %s  time %.2fs", l.depth, l.seldepth,
                humanCount(l.nodes).c_str(), humanCount(l.nps).c_str(),
                l.hashfull < 0 ? "-" : (std::to_string(l.hashfull / 10) + "%").c_str(), l.timeMs / 1000.0);
    std::vector<float> depth, nps;
    double sumD = 0, sumN = 0, sumT = 0;
    for (auto* s : st){ depth.push_back((float)s->depth); nps.push_back((float)s->nps); sumD += s->depth; sumN += s->nps; sumT += s->timeMs; }
    ImGui::TextDisabled("%d moves: avg depth %.1f, avg nps %s, avg time %.2fs", (int)st.size(), sumD / st.size(),
                        humanCount((long long)(sumN / st.size())).c_str(), sumT / st.size() / 1000.0);
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * 3;
    ImGui::PlotLines("##depth", depth.data(), (int)depth.size(), 0, "depth", FLT_MAX, FLT_MAX, ImVec2(w, h));
    ImGui::PlotLines("##nps", nps.data(), (int)nps.size(), 0, "nps", FLT_MAX, FLT_MAX, ImVec2(w, h));
    if (ImGui::TreeNode("Per move")){
        if (ImGui::BeginTable("permove", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, ImGui::GetFontSize() * 10))){
            for (const char* hdr : {"move", "depth", "seldepth", "nodes", "nps", "hash", "time"}) ImGui::TableSetupColumn(hdr);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (auto* s : st){
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", s->ply / 2 + 1);
                ImGui::TableNextColumn(); ImGui::Text("%d", s->depth);
                ImGui::TableNextColumn(); ImGui::Text("%d", s->seldepth);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(humanCount(s->nodes).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(humanCount(s->nps).c_str());
                ImGui::TableNextColumn(); if (s->hashfull >= 0) ImGui::Text("%d%%", s->hashfull / 10); else ImGui::TextDisabled("-");
                ImGui::TableNextColumn(); ImGui::Text("%.2f", s->timeMs / 1000.0);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void helpMarker(const char* text){
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()){
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string clockText(long long ms){
    if (ms < 0) ms = 0;
    long long s = ms / 1000;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else if (ms < 10000) std::snprintf(b, sizeof b, "%lld:%02lld.%lld", s / 60, s % 60, (ms % 1000) / 100);
    else std::snprintf(b, sizeof b, "%lld:%02lld", s / 60, s % 60);
    return b;
}

std::string timeStamp(){
    char b[32]; std::time_t t = std::time(nullptr);
    std::strftime(b, sizeof b, "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

// ---------------------------------------------------------------- Job
void Job::start(const std::string& title, std::function<void(Job&)> fn){
    abort();
    {
        std::lock_guard<std::mutex> lk(mx_);
        lines_.clear(); title_ = title;
    }
    abort_ = false; running_ = true;
    th_ = std::thread([this, fn](){ fn(*this); running_ = false; });
}
void Job::abort(){
    abort_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
}
void Job::log(const std::string& line){
    std::lock_guard<std::mutex> lk(mx_);
    lines_.push_back(line);
}
std::vector<std::string> Job::lines() const{
    std::lock_guard<std::mutex> lk(mx_);
    return lines_;
}
std::string Job::title() const{
    std::lock_guard<std::mutex> lk(mx_);
    return title_;
}
std::string Job::text() const{
    std::lock_guard<std::mutex> lk(mx_);
    std::string s = title_ + "\n";
    for (auto& l : lines_) s += l + "\n";
    return s;
}
bool Job::empty() const{
    std::lock_guard<std::mutex> lk(mx_);
    return lines_.empty() && title_.empty();
}

void jobPanel(Job& job, const std::function<std::string()>& saveDialog,
              const std::function<void(const std::string&)>& toast){
    std::string title = job.title();
    if (title.empty()){ ImGui::TextDisabled("No test run yet."); return; }
    ImGui::PushFont(fonts().body);
    ImGui::TextColored(kAccent, "%s", title.c_str());
    if (job.running()){
        ImGui::SameLine(); ImGui::TextColored(kWarn, "running...");
        ImGui::SameLine();
        if (ImGui::SmallButton("Abort")) job.requestStop();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy log")){ ImGui::SetClipboardText(job.text().c_str()); if (toast) toast("Log copied"); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save log...") && saveDialog){
        std::string path = saveDialog();
        if (!path.empty()){
            std::ofstream f(path);
            f << job.text();
            if (toast) toast(f ? "Saved " + path : "Could not write " + path);
        }
    }
    ImGui::PopFont();
    ImGui::BeginChild("joblog", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (fonts().mono) ImGui::PushFont(fonts().mono);
    auto lines = job.lines();
    ImGuiListClipper clip;
    clip.Begin((int)lines.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++){
            const std::string& l = lines[i];
            if (l.find("FAIL") != std::string::npos || l.find(" ??") != std::string::npos)
                ImGui::TextColored(kBad, "%s", l.c_str());
            else if (l.rfind(">>>", 0) == 0) ImGui::TextColored(kGood, "%s", l.c_str());
            else ImGui::TextUnformatted(l.c_str());
        }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetFontSize() * 3)
        ImGui::SetScrollHereY(1.0f);                       // follow the tail unless scrolled up
    if (fonts().mono) ImGui::PopFont();
    ImGui::EndChild();
}

}
