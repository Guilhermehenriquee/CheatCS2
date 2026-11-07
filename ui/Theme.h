// muei 無影 - theme and fonts.
//
// One dark palette with a single accent that follows Config::accentCol, so the
// colour picker actually changes the UI rather than only the ESP.
//
// Three baked sizes, because the menu draws its own typography instead of using
// ImGui widgets: a title face for the brand row, a body face for labels and a
// small face for captions and the toolbar. Only the title face carries CJK, and
// only the two codepoints the brand needs, which keeps the atlas small.
#pragma once

#include "../imgui/imgui.h"

namespace theme {

// Palette pulled from a NeverLose-style dark shell: near-black surfaces, one
// saturated accent, cool greys for secondary text.
namespace col {
    inline ImU32 window()      { return IM_COL32(13, 15, 22, 255); }
    inline ImU32 sidebar()     { return IM_COL32(16, 18, 25, 255); }
    inline ImU32 toolbar()     { return IM_COL32(11, 13, 20, 255); }
    inline ImU32 card()        { return IM_COL32(17, 19, 27, 228); }
    inline ImU32 cardBorder()  { return IM_COL32(31, 34, 44, 255); }
    inline ImU32 divider()     { return IM_COL32(26, 29, 38, 255); }
    inline ImU32 hairline()    { return IM_COL32(255, 255, 255, 20); }
    inline ImU32 caption()     { return IM_COL32(124, 130, 144, 255); }
    inline ImU32 label()       { return IM_COL32(207, 209, 218, 255); }
    inline ImU32 labelDim()    { return IM_COL32(143, 149, 162, 255); }
    inline ImU32 value()       { return IM_COL32(170, 173, 184, 255); }
    inline ImU32 pill()        { return IM_COL32(25, 28, 38, 255); }
    inline ImU32 pillBorder()  { return IM_COL32(32, 35, 46, 255); }
    inline ImU32 track()       { return IM_COL32(34, 38, 48, 255); }
    inline ImU32 toggleOff()   { return IM_COL32(29, 33, 43, 255); }
    inline ImU32 knobOff()     { return IM_COL32(133, 144, 156, 255); }
    inline ImU32 knobOn()      { return IM_COL32(248, 249, 252, 255); }
    inline ImU32 good()        { return IM_COL32(90, 224, 140, 255); }
    inline ImU32 bad()         { return IM_COL32(255, 115, 110, 255); }
    inline ImU32 warn()        { return IM_COL32(255, 176, 84, 255); }
}

// Accent-driven colours. These read the live accent set by apply().
ImU32 accentU32(float alpha = 1.0f);
ImVec4 accentVec4(float alpha = 1.0f);
ImU32 accentMix(ImU32 from, float t, float alpha = 1.0f);

// Font faces. Index 0 is the title, 1 the body (also io.FontDefault), 2 small.
ImFont* fontTitle();
ImFont* fontBody();
ImFont* fontSmall();

// Apply the palette. accent is the same float[4] the config stores.
void apply(const float accent[4]);

// Bake the three faces. Returns how many were added; 0 means the built-in
// bitmap font is in use.
int loadFonts();

} // namespace theme
