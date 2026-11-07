#include "Theme.h"

#include "../core/Log.h"

#include <windows.h>

#include <cmath>

namespace theme {
namespace {

float g_accent[4] = { 0.00f, 0.85f, 1.00f, 1.00f };

ImFont* g_title = nullptr;
ImFont* g_body  = nullptr;
ImFont* g_small = nullptr;

inline float sat(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

bool fileExists(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

const char* findFont(const char* const* candidates, int count) {
    for (int i = 0; i < count; ++i) {
        if (fileExists(candidates[i])) return candidates[i];
    }
    return nullptr;
}

} // namespace

ImU32 accentU32(float alpha) {
    return IM_COL32((int)(g_accent[0] * 255.f), (int)(g_accent[1] * 255.f),
                    (int)(g_accent[2] * 255.f), (int)(sat(alpha) * 255.f));
}

ImVec4 accentVec4(float alpha) { return ImVec4(g_accent[0], g_accent[1], g_accent[2], sat(alpha)); }

ImU32 accentMix(ImU32 from, float t, float alpha) {
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    t = sat(t);
    ImVec4 b(a.x + (g_accent[0] - a.x) * t,
             a.y + (g_accent[1] - a.y) * t,
             a.z + (g_accent[2] - a.z) * t,
             a.w + (1.0f - a.w) * t);
    b.w *= sat(alpha);
    return ImGui::ColorConvertFloat4ToU32(b);
}

ImFont* fontTitle() { return g_title ? g_title : ImGui::GetFont(); }
ImFont* fontBody()  { return g_body  ? g_body  : ImGui::GetFont(); }
ImFont* fontSmall() { return g_small ? g_small : ImGui::GetFont(); }

void apply(const float accent[4]) {
    for (int i = 0; i < 4; ++i) g_accent[i] = sat(accent[i]);

    ImGuiStyle& s = ImGui::GetStyle();

    // The menu draws its own chrome; these values only affect the few native
    // widgets that remain (the custom-name text field) and any tooltip.
    s.WindowRounding    = 14.f;
    s.WindowBorderSize  = 0.f;
    s.WindowPadding     = ImVec2(0.f, 0.f);
    s.ChildRounding     = 10.f;
    s.ChildBorderSize   = 0.f;
    s.PopupRounding     = 12.f;
    s.PopupBorderSize   = 1.f;
    s.FrameRounding     = 5.f;
    s.FrameBorderSize   = 0.f;
    s.FramePadding      = ImVec2(10.f, 6.f);
    s.ItemSpacing       = ImVec2(8.f, 6.f);
    s.ItemInnerSpacing  = ImVec2(8.f, 6.f);
    s.GrabRounding      = 4.f;
    s.ScrollbarSize     = 8.f;
    s.ScrollbarRounding = 4.f;
    s.AntiAliasedLines  = true;
    s.AntiAliasedFill   = true;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.93f, 0.95f, 1.f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.42f, 0.44f, 0.49f, 1.f);
    c[ImGuiCol_WindowBg]        = ImVec4(0.05f, 0.06f, 0.09f, 0.92f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.078f, 0.078f, 0.114f, 0.94f);
    c[ImGuiCol_Border]          = ImVec4(0.14f, 0.15f, 0.20f, 1.f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.098f, 0.110f, 0.149f, 1.f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.13f, 0.15f, 0.20f, 1.f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.16f, 0.18f, 0.24f, 1.f);
    c[ImGuiCol_CheckMark]       = accentVec4(1.f);
    c[ImGuiCol_SliderGrab]      = accentVec4(0.85f);
    c[ImGuiCol_SliderGrabActive]= accentVec4(1.f);
    c[ImGuiCol_Button]          = ImVec4(0.13f, 0.15f, 0.19f, 1.f);
    c[ImGuiCol_ButtonHovered]   = accentVec4(0.55f);
    c[ImGuiCol_ButtonActive]    = accentVec4(0.80f);
    c[ImGuiCol_Header]          = accentVec4(0.30f);
    c[ImGuiCol_HeaderHovered]   = accentVec4(0.45f);
    c[ImGuiCol_HeaderActive]    = accentVec4(0.65f);
    c[ImGuiCol_Separator]       = ImVec4(0.14f, 0.15f, 0.20f, 1.f);
    c[ImGuiCol_TextSelectedBg]  = accentVec4(0.40f);
    c[ImGuiCol_NavCursor]       = accentVec4(1.f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.20f, 0.22f, 0.27f, 1.f);
}

int loadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    const char* uiCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
    };
    const char* cjkCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };

    const char* ui  = findFont(uiCandidates, (int)(sizeof(uiCandidates) / sizeof(uiCandidates[0])));
    const char* cjk = findFont(cjkCandidates, (int)(sizeof(cjkCandidates) / sizeof(cjkCandidates[0])));

    int loaded = 0;

    if (!ui) {
        logx::line("theme: no UI font found, falling back to the built-in atlas");
        ImFont* def = io.Fonts->AddFontDefault();
        g_title = g_body = g_small = def;
        io.FontDefault = def;
        return 0;
    }

    g_title = io.Fonts->AddFontFromFileTTF(ui, 21.0f);
    if (g_title) ++loaded;

    // Merge only the two codepoints the brand needs: 影 (U+5F71) and 無 (U+7121).
    // Baking the whole simplified-Chinese set would grow the atlas tenfold for
    // two glyphs.
    if (g_title && cjk) {
        static const ImWchar brandGlyphs[] = { 0x5F71, 0x5F71, 0x7121, 0x7121, 0 };
        ImFontConfig cfg;
        cfg.MergeMode   = true;
        cfg.PixelSnapH  = true;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        if (io.Fonts->AddFontFromFileTTF(cjk, 21.0f, &cfg, brandGlyphs)) ++loaded;
    }

    g_body = io.Fonts->AddFontFromFileTTF(ui, 15.0f);
    if (g_body) ++loaded;

    g_small = io.Fonts->AddFontFromFileTTF(ui, 12.0f);
    if (g_small) ++loaded;

    if (!g_body) g_body = g_title;
    if (!g_small) g_small = g_body;
    io.FontDefault = g_body;

    logx::line("theme: %d font face(s) baked (title 21, body 15, small 12)", loaded);
    return loaded;
}

} // namespace theme
