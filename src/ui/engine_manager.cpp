/* Chess-GUI-for-UCI — engine manager window (ImGui). GPL-2.0 */
#include "ui/engine_manager.hpp"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

void EngineManagerWindow::startTest(const EngineConfig& cfg){
    pendingPath_ = cfg.path;
    pending_ = std::async(std::launch::async, [cfg](){
        LaunchResult r;
        UciEngine e;
        auto t0 = std::chrono::steady_clock::now();
        r.ok = e.start(cfg);
        r.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0).count();
        r.name = e.name(); r.author = e.author(); r.error = e.lastError();
        r.options = e.options();
        e.quit();
        uint64_t cur = 0;
        r.log = e.logSince(cur);
        return r;
    });
}

void EngineManagerWindow::draw(EngineRegistry& reg, const Callbacks& cb){
    if (pending_.valid() &&
        pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        results_[pendingPath_] = pending_.get();

    auto& list = reg.engines();
    if (selected_ >= (int)list.size()) selected_ = (int)list.size() - 1;

    // ---- left: engine list ----
    float em = ImGui::GetFontSize();
    ImGui::BeginChild("list", ImVec2(em * 16, 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("Add")){
        EngineConfig c; c.name = "New engine";
        if (cb.browseFile){
            std::string p = cb.browseFile("Choose engine executable");
            if (!p.empty()){ c.path = p; c.name = fs::u8path(p).filename().u8string(); }
        }
        reg.add(c);
        selected_ = (int)reg.engines().size() - 1;
        if (cb.save) cb.save();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_ < 0);
    if (ImGui::Button("Duplicate") && selected_ >= 0){
        EngineConfig c = list[selected_];
        c.name += " (copy)"; c.discovered = false;
        reg.add(c);
        selected_ = (int)reg.engines().size() - 1;
        if (cb.save) cb.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && selected_ >= 0){
        reg.remove((size_t)selected_);
        selected_ = std::min(selected_, (int)reg.engines().size() - 1);
        argsFor_ = -1;
        if (cb.save) cb.save();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    for (int i = 0; i < (int)reg.engines().size(); i++){
        const EngineConfig& c = reg.engines()[i];
        std::string label = c.name.empty() ? fs::u8path(c.path).filename().u8string() : c.name;
        if (c.discovered) label += "  (found in Resources/engines)";
        if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), selected_ == i)){
            selected_ = i;
        }
    }
    if (reg.engines().empty())
        ImGui::TextWrapped("No engines yet. Click Add, or drop engine executables into "
                           "Resources/engines/ next to the program.");
    ImGui::EndChild();

    // ---- right: editor ----
    ImGui::SameLine();
    ImGui::BeginChild("edit", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_ >= 0 && selected_ < (int)reg.engines().size())
        drawEditor(reg.engines()[selected_], cb);
    ImGui::EndChild();
}

void EngineManagerWindow::drawEditor(EngineConfig& c, const Callbacks& cb){
    bool changed = false;
    float labelW = ImGui::GetFontSize() * 9;
    auto label = [&](const char* s){ ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(s);
                                     ImGui::SameLine(labelW); ImGui::SetNextItemWidth(-1); };

    label("Name");
    changed |= ImGui::InputText("##name", &c.name);

    label("Executable");
    if (cb.browseFile){
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 6);
        changed |= ImGui::InputText("##path", &c.path);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")){
            std::string p = cb.browseFile("Choose engine executable");
            if (!p.empty()){ c.path = p; changed = true; }
        }
    } else changed |= ImGui::InputText("##path", &c.path);

    if (argsFor_ != selected_){ argsEdit_ = joinArgs(c.args); argsFor_ = selected_; }
    label("Arguments");
    if (ImGui::InputTextWithHint("##args", "none (e.g. --uci or -t 1)", &argsEdit_)){
        std::string err;
        auto v = splitArgs(argsEdit_, &err);
        if (err.empty()){ c.args = v; changed = true; }
    }
    { std::string err; splitArgs(argsEdit_, &err);
      if (!err.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Arguments: %s", err.c_str()); }

    label("Working directory");
    EngineConfig probe; probe.path = c.path;
    std::string hint = "engine's own directory: " + probe.effectiveWorkingDir();
    changed |= ImGui::InputTextWithHint("##wd", hint.c_str(), &c.workingDir);
    ImGui::TextDisabled("Relative paths the engine uses (NNUE nets, books, tablebases) "
                        "resolve against the working directory.");

    if (changed){ c.discovered = false; if (cb.save) cb.save(); }

    ImGui::Spacing();
    bool running = pending_.valid();
    ImGui::BeginDisabled(running || c.path.empty());
    if (ImGui::Button(running ? "Testing..." : "Test launch")) startTest(c);
    ImGui::EndDisabled();
    auto it = results_.find(c.path);
    if (it != results_.end()){
        const LaunchResult& r = it->second;
        ImGui::SameLine();
        if (r.ok) ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.55f, 1), "OK: %s%s%s, %d options, handshake %lld ms",
                                     r.name.c_str(), r.author.empty() ? "" : " by ",
                                     r.author.c_str(), (int)r.options.size(), r.ms);
        else ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Failed: %s", r.error.c_str());
        if (ImGui::CollapsingHeader("Handshake log")){
            ImGui::BeginChild("log", ImVec2(0, ImGui::GetFontSize() * 12), ImGuiChildFlags_Borders);
            for (auto& l : r.log){
                ImVec4 col = l.dir == '>' ? ImVec4(0.55f, 0.75f, 1, 1)
                           : l.dir == '!' ? ImVec4(1, 0.6f, 0.4f, 1)
                           : l.dir == '#' ? ImVec4(0.7f, 0.7f, 0.7f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1);
                ImGui::TextColored(col, "%6lld %c %s", l.tMs, l.dir, l.text.c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::Spacing();
    drawOptions(c, cb);
}

void EngineManagerWindow::drawOptions(EngineConfig& c, const Callbacks& cb){
    ImGui::SeparatorText("UCI options");
    auto it = results_.find(c.path);
    if (it == results_.end() || it->second.options.empty()){
        if (!c.options.empty()){
            ImGui::TextUnformatted("Saved option values (run Test launch to edit them with the engine's limits):");
            for (auto& o : c.options) ImGui::BulletText("%s = %s", o.first.c_str(), o.second.c_str());
        } else ImGui::TextDisabled("Run Test launch to list the engine's options.");
        return;
    }
    bool changed = false;
    if (ImGui::BeginTable("opts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                     ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4);
        ImGui::TableHeadersRow();
        int row = 0;
        for (const UciOption& o : it->second.options){
            ImGui::PushID(row++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(o.name.c_str());
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
            const std::string* saved = c.option(o.name);
            std::string cur = saved ? *saved : o.defVal;
            if (o.type == "check"){
                bool v = cur == "true";
                if (ImGui::Checkbox("##v", &v)){ c.setOption(o.name, v ? "true" : "false"); changed = true; }
            } else if (o.type == "spin"){
                long long v = std::atoll(cur.c_str());
                if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v, nullptr, nullptr, "%lld",
                                       ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()){
                    v = std::max(o.minV, std::min(o.maxV, v));
                    if (std::to_string(v) != cur){ c.setOption(o.name, std::to_string(v)); changed = true; }
                }
            } else if (o.type == "combo"){
                if (ImGui::BeginCombo("##v", cur.c_str())){
                    for (auto& var : o.vars)
                        if (ImGui::Selectable(var.c_str(), var == cur)){ c.setOption(o.name, var); changed = true; }
                    ImGui::EndCombo();
                }
            } else if (o.type == "button"){
                bool v = saved != nullptr;
                if (ImGui::Checkbox("press at start##v", &v)){
                    if (v) c.setOption(o.name, "");
                    else c.options.erase(std::remove_if(c.options.begin(), c.options.end(),
                                         [&](auto& p){ return p.first == o.name; }), c.options.end());
                    changed = true;
                }
            } else {
                std::string v = cur;
                if (ImGui::InputText("##v", &v, ImGuiInputTextFlags_EnterReturnsTrue) ||
                    (ImGui::IsItemDeactivatedAfterEdit())){
                    c.setOption(o.name, v); changed = true;
                }
            }
            ImGui::TableNextColumn();
            std::string def = o.type == "spin" ? o.defVal + "  [" + std::to_string(o.minV) + ".." +
                                                 std::to_string(o.maxV) + "]" : o.defVal;
            ImGui::TextDisabled("%s", def.c_str());
            ImGui::TableNextColumn();
            if (saved && o.type != "button" && ImGui::SmallButton("Reset")){
                c.options.erase(std::remove_if(c.options.begin(), c.options.end(),
                                [&](auto& p){ return p.first == o.name; }), c.options.end());
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (changed){ c.discovered = false; if (cb.save) cb.save(); }
}
