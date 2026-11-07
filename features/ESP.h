// muei 無影 - visual overlay.
//
// Draws from the Frame snapshot only. It never reads the target itself, so the
// geometry the operator sees is exactly the geometry the aimbot used.
#pragma once

#include "../imgui/imgui.h"

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

namespace esp {

// Screen-space bounding box derived from the head and foot bones.
struct Box {
    ImVec2 min;
    ImVec2 max;
    bool   valid = false;
};

Box computeBox(const Nav& nav, const Player& pl, const Frame& frame);

// Draw the whole ESP layer, plus the aimbot FOV ring.
void draw(ImDrawList* dl, const Nav& nav, const Config& cfg, Frame& frame);

} // namespace esp
