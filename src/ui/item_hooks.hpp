/* Chess-GUI-for-UCI — ImGui item registry for scripted UI tests. GPL-2.0
 *
 * Built with IMGUI_ENABLE_TEST_ENGINE, Dear ImGui reports every widget it
 * submits (id, bounding box, label) to a few hook functions. We implement
 * them to remember the widgets of the last frame, so --script steps can
 * click a button by its label instead of by pixel coordinates.
 */
#pragma once
#include <string>
#include <vector>

struct UiItem {
    std::string label;          // visible label (text before "##")
    std::string fullLabel;      // label including any "##id" suffix
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // screen rect in pixels
};

namespace uiitems {
void beginFrame();                                   // call before ImGui::NewFrame
// Widgets of the last completed frame whose label (or full label) equals `label`.
std::vector<UiItem> find(const std::string& label);
}
