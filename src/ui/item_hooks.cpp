/* Chess-GUI-for-UCI — ImGui item registry for scripted UI tests. GPL-2.0 */
#include "ui/item_hooks.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdarg>
#include <unordered_map>

namespace {
struct Rec { ImRect bb; std::string label; bool hasLabel = false; };
std::unordered_map<ImGuiID, Rec> current, previous;
}

namespace uiitems {
void beginFrame(){
    previous.swap(current);
    current.clear();
    if (ImGuiContext* g = ImGui::GetCurrentContext()) g->TestEngineHookItems = true;
}
std::vector<UiItem> find(const std::string& label){
    std::vector<UiItem> out;
    for (auto& kv : previous){
        const Rec& r = kv.second;
        if (!r.hasLabel) continue;
        std::string vis = r.label.substr(0, r.label.find("##"));
        if (vis != label && r.label != label) continue;
        UiItem it; it.label = vis; it.fullLabel = r.label;
        it.x0 = r.bb.Min.x; it.y0 = r.bb.Min.y; it.x1 = r.bb.Max.x; it.y1 = r.bb.Max.y;
        out.push_back(it);
    }
    return out;
}
}

// ---- hooks called by Dear ImGui (see imgui_internal.h, IMGUI_ENABLE_TEST_ENGINE) ----
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*){
    current[id].bb = bb;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags){
    if (!label) return;
    Rec& r = current[id];
    r.label = label; r.hasLabel = true;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id){
    auto it = previous.find(id);
    return it != previous.end() && it->second.hasLabel ? it->second.label.c_str() : nullptr;
}
