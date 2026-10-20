/* Chess-GUI-for-UCI — UCI console window. GPL-2.0 */
#include "ui/uci_console.hpp"
#include "ui/widgets.hpp"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

static const size_t kMaxLines = 50000;

void UciConsole::refresh(){
    auto live = EngineMonitor::instance().list();
    // drop sessions the monitor has forgotten, update the others, add new ones
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(), [&](const Session& s){
        return std::none_of(live.begin(), live.end(), [&](const MonitoredEngine& m){ return m.id == s.id; });
    }), sessions_.end());
    for (auto& m : live){
        auto it = std::find_if(sessions_.begin(), sessions_.end(), [&](const Session& s){ return s.id == m.id; });
        if (it == sessions_.end()){
            Session s; s.id = m.id; s.label = m.label; s.log = m.log; s.alive = m.alive;
            sessions_.push_back(s);
            if (!selected_) selected_ = m.id;
        } else { it->label = m.label; it->alive = m.alive; }
    }
    std::sort(sessions_.begin(), sessions_.end(), [](const Session& a, const Session& b){ return a.id > b.id; });
    if (paused_) return;
    for (auto& s : sessions_){
        auto add = s.log->since(s.cursor);
        s.lines.insert(s.lines.end(), add.begin(), add.end());
        if (s.lines.size() > kMaxLines) s.lines.erase(s.lines.begin(), s.lines.end() - kMaxLines);
    }
}

bool UciConsole::visible(const UciLogLine& l) const{
    if ((l.dir == '>' && !showIn_) || (l.dir == '<' && !showOut_) || (l.dir == '!' && !showErr_) ||
        (l.dir == '#' && !showNotes_)) return false;
    if (hideInfo_ && l.dir == '<' && l.text.rfind("info", 0) == 0) return false;
    if (filter_.empty()) return true;
    auto lower = [](std::string s){ for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    return lower(l.text).find(lower(filter_)) != std::string::npos;
}

static std::string lineText(const UciLogLine& l){
    char b[48];
    std::snprintf(b, sizeof b, "%9.3f %c ", l.tMs / 1000.0, l.dir);
    return b + l.text;
}

std::string UciConsole::visibleText(const Session& s) const{
    std::string out;
    for (auto& l : s.lines) if (visible(l)) out += lineText(l) + "\n";
    return out;
}

void UciConsole::draw(bool* open, const std::function<std::string()>& saveDialog,
                      const std::function<void(const std::string&)>& toast){
    if (!*open) return;
    refresh();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 52, em * 30), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UCI console", open)){ ImGui::End(); return; }

    ImGui::BeginChild("engines", ImVec2(em * 13, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (sessions_.empty()) ImGui::TextWrapped("No engine has been started yet. Every engine the program starts "
                                              "(games, analysis, matches, tests) appears here.");
    for (auto& s : sessions_){
        ImGui::PushID((int)s.id);
        ImGui::TextColored(s.alive ? ui::kGood : ui::kDim, s.alive ? "*" : "-");
        ImGui::SameLine();
        if (ImGui::Selectable(s.label.c_str(), selected_ == s.id)){ selected_ = s.id; scrollToEnd_ = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", s.label.c_str(), s.alive ? "running" : "ended");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    Session* cur = nullptr;
    for (auto& s : sessions_) if (s.id == selected_) cur = &s;
    ImGui::SetNextItemWidth(em * 12);
    ImGui::InputTextWithHint("##filter", "filter", &filter_);
    ImGui::SameLine(); ImGui::Checkbox("> sent", &showIn_);
    ImGui::SameLine(); ImGui::Checkbox("< stdout", &showOut_);
    ImGui::SameLine(); ImGui::Checkbox("! stderr", &showErr_);
    ImGui::SameLine(); ImGui::Checkbox("# notes", &showNotes_);
    ImGui::SameLine(); ImGui::Checkbox("hide info", &hideInfo_);
    if (ImGui::Button(paused_ ? "Resume" : "Pause")) paused_ = !paused_;
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button("Copy") && cur){ ImGui::SetClipboardText(visibleText(*cur).c_str()); toast("Copied the visible lines"); }
    ImGui::SameLine();
    if (ImGui::Button("Save...") && cur && saveDialog){
        std::string path = saveDialog();
        if (!path.empty()){
            std::ofstream f(path);
            f << "# " << cur->label << "\n" << visibleText(*cur);
            toast(f ? "Saved " + path : "Could not write " + path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear") && cur) cur->lines.clear();
    ImGui::EndDisabled();
    if (paused_){ ImGui::SameLine(); ImGui::TextColored(ui::kWarn, "paused"); }

    float cmdH = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log", ImVec2(0, -cmdH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (cur){
        ImGui::PushFont(ui::fonts().mono);
        std::vector<int> idx;
        idx.reserve(cur->lines.size());
        for (int i = 0; i < (int)cur->lines.size(); i++) if (visible(cur->lines[i])) idx.push_back(i);
        ImGuiListClipper clip;
        clip.Begin((int)idx.size());
        while (clip.Step())
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++){
                const UciLogLine& l = cur->lines[idx[k]];
                ImVec4 col = l.dir == '>' ? ImVec4(0.55f, 0.80f, 1.0f, 1) : l.dir == '!' ? ImVec4(1.0f, 0.62f, 0.35f, 1)
                           : l.dir == '#' ? ui::kDim : ImVec4(0.92f, 0.92f, 0.92f, 1);
                ImGui::TextDisabled("%9.3f", l.tMs / 1000.0);
                ImGui::SameLine();
                ImGui::TextColored(col, "%c %s", l.dir, l.text.c_str());
            }
        if (scrollToEnd_ || (!paused_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - em * 2)){
            ImGui::SetScrollHereY(1.0f);
            scrollToEnd_ = false;
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();

    // command line
    bool live = cur && cur->alive;
    ImGui::BeginDisabled(!live);
    ImGui::SetNextItemWidth(-em * 4);
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
        auto* self = (UciConsole*)d->UserData;
        if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory || self->history_.empty()) return 0;
        int n = (int)self->history_.size();
        if (d->EventKey == ImGuiKey_UpArrow) self->historyPos_ = self->historyPos_ < 0 ? n - 1 : std::max(0, self->historyPos_ - 1);
        else if (d->EventKey == ImGuiKey_DownArrow && self->historyPos_ >= 0)
            self->historyPos_ = self->historyPos_ + 1 >= n ? -1 : self->historyPos_ + 1;
        d->DeleteChars(0, d->BufTextLen);
        if (self->historyPos_ >= 0) d->InsertChars(0, self->history_[self->historyPos_].c_str());
        return 0;
    };
    bool enter = ImGui::InputTextWithHint("##cmd", live ? "raw UCI command, e.g. 'go depth 10' or 'd' (Enter sends)"
                                                       : "the selected engine is not running",
                                          &cmd_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                          cb, this);
    ImGui::SameLine();
    if ((ImGui::Button("Send") || enter) && live && !cmd_.empty()){
        if (EngineMonitor::instance().send(cur->id, cmd_)){
            history_.push_back(cmd_);
            if (history_.size() > 100) history_.erase(history_.begin());
            scrollToEnd_ = true;
        } else toast("The engine is no longer running");
        cmd_.clear(); historyPos_ = -1;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::End();
}
