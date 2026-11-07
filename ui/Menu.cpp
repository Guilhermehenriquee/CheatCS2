// muei 無影 - menu.
//
// Everything here is drawn into the window draw list; there are no ImGui widgets
// except the text field and the colour picker, both of which need real input
// handling. The visual language follows the NeverLose shell: a 168 px sidebar
// with a brand block and captioned nav groups, a toolbar carrying live status, a
// grid of rounded cards with 37 px rows, and three custom controls (pill toggle,
// slim slider with a value pill, popup select).
//
// Interaction is ImGui::InvisibleButton hit-testing. Every animated value goes
// through Motion(), an exponential ease kept in the window's state storage, so
// hover, toggles, page transitions and popups all settle instead of snapping.
//
// draw() returns true when any control changed, which arms the debounced save.
// The menu does vector arithmetic on ImVec2 everywhere, so the courtesy
// operators have to be enabled before imgui.h is first pulled in by any header.
#define IMGUI_DEFINE_MATH_OPERATORS

#include "Menu.h"
#include "Theme.h"

#include "../imgui/imgui.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace menu {
namespace {

// --------------------------------------------------------------- geometry ---
constexpr float kShellW   = 758.0f;
constexpr float kShellH   = 558.0f;
constexpr float kSidebarW = 168.0f;
constexpr float kToolbarH = 56.0f;
constexpr float kRowH     = 37.0f;
constexpr float kRadius   = 14.0f;

constexpr float X0 = 182.0f;   // left card x
constexpr float XR = 476.0f;   // right card x
constexpr float CW = 280.0f;   // left card width
constexpr float CR = 268.0f;   // right card width
constexpr float Y0 = 92.0f;    // first card y

// Text sizes matched to the baked faces so glyphs stay 1:1 crisp.
constexpr float T_TITLE = 21.0f;
constexpr float T_BODY  = 15.0f;
constexpr float T_CTRL  = 14.0f;
constexpr float T_SMALL = 12.0f;
constexpr float T_CAP   = 10.0f;

inline float sat(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t));
}

ImU32 mixU32(ImU32 a, ImU32 b, float t) {
    return ImGui::ColorConvertFloat4ToU32(
        lerpColor(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), sat(t)));
}

// ---------------------------------------------------------------- motion ----
// Exponential approach, framerate independent. Returns the current value and
// stores it in the window state keyed by the caller's ID stack.
float Motion(const char* key, float target, float speed = 16.0f, float initial = -1.0f) {
    ImGuiID id = ImGui::GetID(key);
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.f ? target : initial);
    *v = lerp(*v, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
    if (std::fabs(*v - target) < 0.0005f) *v = target;
    return *v;
}

// ------------------------------------------------------------------ text ----
void Txt(ImDrawList* d, ImVec2 p, ImU32 c, const char* s, float size, ImFont* f) {
    d->AddText(f, size, p, c, s);
}

void TxtY(ImDrawList* d, float x, float y, float h, ImU32 c, const char* s,
          float size, ImFont* f) {
    const float th = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s).y;
    Txt(d, ImVec2(x, y + std::floor((h - th) * 0.5f)), c, s, size, f);
}

float TxtW(const char* s, float size, ImFont* f) {
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
}

// ------------------------------------------------------------- hit-testing ---
bool Hit(const char* id, ImVec2 p, ImVec2 size) {
    ImGui::SetCursorScreenPos(p);
    return ImGui::InvisibleButton(id, size);
}

// ------------------------------------------------------------------ popup ---
// The popup outlives the frame that opened it, so it must never hold a pointer
// into a caller's stack. It writes into a slot owned by the select widget, and
// that widget copies the slot back into the real config field on the next frame.
struct Popup {
    bool               open  = false;
    ImGuiID            owner = 0;
    ImVec2             anchor{};
    float              width = 150.f;
    const char* const* items = nullptr;
    int                count = 0;
    int*               target = nullptr;   // points into g_slots, never a local
    int                openedFrame = 0;
    ImGuiID            pickedOwner = 0;    // one-shot: a value was chosen
};

Popup g_popup;

constexpr int kSlotCount = 48;
int          g_slotValue[kSlotCount];
ImGuiID      g_slotId[kSlotCount];

int* slotFor(ImGuiID id) {
    for (int i = 0; i < kSlotCount; ++i) {
        if (g_slotId[i] == id) return &g_slotValue[i];
    }
    for (int i = 0; i < kSlotCount; ++i) {
        if (g_slotId[i] == 0) {
            g_slotId[i] = id;
            g_slotValue[i] = 0;
            return &g_slotValue[i];
        }
    }
    // Table full: reuse the last slot. Only reachable if the menu grows past 48
    // selects, which is well beyond the current page count.
    g_slotId[kSlotCount - 1] = id;
    return &g_slotValue[kSlotCount - 1];
}

void openPopup(ImGuiID owner, ImVec2 anchor, float width, const char* const* items,
               int count, int* target) {
    const bool same = g_popup.open && g_popup.owner == owner;
    g_popup.open        = !same;
    g_popup.owner       = owner;
    g_popup.anchor      = anchor;
    g_popup.width       = width;
    g_popup.items       = items;
    g_popup.count       = count;
    g_popup.target      = target;
    g_popup.openedFrame = ImGui::GetFrameCount();
}

// -------------------------------------------------------------------- card ---
float Card(ImDrawList* d, ImVec2 p, float w, int rows, const char* title) {
    const float h = rows * kRowH;
    Txt(d, p + ImVec2(13.0f, -16.0f), theme::col::caption(), title, T_CAP, theme::fontSmall());
    d->AddRectFilled(p, p + ImVec2(w, h), theme::col::card(), kRadius);
    d->AddRect(p, p + ImVec2(w, h), theme::col::cardBorder(), kRadius);
    return h;
}

void rowLine(ImDrawList* d, float x, float y, float w) {
    d->AddLine(ImVec2(x + 13.0f, y), ImVec2(x + w - 13.0f, y), theme::col::divider());
}

// ----------------------------------------------------------------- widgets ---
bool RowToggle(ImDrawList* d, float x, float y, float w, const char* uid,
               const char* label, bool* v, bool first, bool enabled = true) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, enabled ? theme::col::label() : theme::col::labelDim(),
         label, T_BODY, theme::fontBody());

    ImGui::PushID(uid);
    const ImVec2 p(x + w - 43.0f, y + 9.0f);
    bool changed = false;
    if (enabled && Hit("##hit", p - ImVec2(5.f, 6.f), ImVec2(39.f, 30.f))) {
        *v = !*v;
        changed = true;
    }
    const float on    = Motion("on", *v ? 1.f : 0.f, 19.f);
    const float hover = Motion("hv", (enabled && ImGui::IsItemHovered()) ? 1.f : 0.f, 20.f);
    ImGui::PopID();

    if (hover > 0.001f) {
        d->AddRectFilled(p - ImVec2(1.f + hover, 1.f + hover),
                         p + ImVec2(30.f + hover, 19.f + hover),
                         theme::accentU32(0.18f * hover), 10.f);
    }
    d->AddRectFilled(p, p + ImVec2(29.f, 18.f),
                     mixU32(theme::col::toggleOff(), theme::accentU32(1.f), on), 9.f);
    d->AddCircleFilled(p + ImVec2(lerp(9.f, 20.f, on), 9.f), 7.f,
                       mixU32(theme::col::knobOff(), theme::col::knobOn(), on));
    return changed;
}

bool RowSlider(ImDrawList* d, float x, float y, float w, const char* uid,
               const char* label, float* v, float lo, float hi, const char* fmt,
               bool first, bool enabled = true) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, enabled ? theme::col::label() : theme::col::labelDim(),
         label, T_BODY, theme::fontBody());

    const ImVec2 start(x + w - 145.0f, y + 18.0f);
    const float tw = 80.0f;

    ImGui::PushID(uid);
    bool changed = false;
    if (enabled) {
        Hit("##hit", start - ImVec2(5.f, 9.f), ImVec2(tw + 10.f, 20.f));
        if (ImGui::IsItemActive()) {
            const float t = clampf((ImGui::GetIO().MousePos.x - start.x) / tw, 0.f, 1.f);
            *v = lo + (hi - lo) * t;
            changed = true;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(0)) {
            // handled above; kept explicit so a click anywhere on the track works
        }
    }
    const float shown = Motion("mv", clampf((*v - lo) / (hi - lo == 0.f ? 1.f : hi - lo), 0.f, 1.f), 22.f);
    ImGui::PopID();

    d->AddRectFilled(start, start + ImVec2(tw, 3.f), theme::col::track(), 2.f);
    d->AddRectFilled(start, start + ImVec2(tw * shown, 3.f), theme::accentU32(1.f), 2.f);
    d->AddCircleFilled(start + ImVec2(tw * shown, 1.5f), 5.5f, theme::col::knobOn());

    char buf[48];
    std::snprintf(buf, sizeof(buf), fmt, *v);

    const ImVec2 pill(x + w - 55.0f, y + 8.0f);
    d->AddRectFilled(pill, pill + ImVec2(42.f, 21.f), theme::col::pill(), 5.f);
    d->AddRect(pill, pill + ImVec2(42.f, 21.f), theme::col::pillBorder(), 5.f);
    // Centred, and clipped by width so a long format cannot spill out.
    const float pw = TxtW(buf, T_CTRL, theme::fontBody());
    TxtY(d, pill.x + std::floor((42.f - pw) * 0.5f), pill.y, 21.f, theme::col::value(), buf,
         T_CTRL, theme::fontBody());
    return changed;
}

bool RowSliderInt(ImDrawList* d, float x, float y, float w, const char* uid,
                  const char* label, int* v, int lo, int hi, bool first) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());

    const ImVec2 start(x + w - 145.0f, y + 18.0f);
    const float tw = 80.0f;

    ImGui::PushID(uid);
    bool changed = false;
    Hit("##hit", start - ImVec2(5.f, 9.f), ImVec2(tw + 10.f, 20.f));
    if (ImGui::IsItemActive()) {
        const float t = clampf((ImGui::GetIO().MousePos.x - start.x) / tw, 0.f, 1.f);
        *v = lo + (int)std::lround(t * (float)(hi - lo));
        changed = true;
    }
    const float shown = Motion("mv", clampf((float)(*v - lo) / (float)(hi - lo == 0 ? 1 : hi - lo), 0.f, 1.f), 22.f);
    ImGui::PopID();

    d->AddRectFilled(start, start + ImVec2(tw, 3.f), theme::col::track(), 2.f);
    d->AddRectFilled(start, start + ImVec2(tw * shown, 3.f), theme::accentU32(1.f), 2.f);
    d->AddCircleFilled(start + ImVec2(tw * shown, 1.5f), 5.5f, theme::col::knobOn());

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", *v);
    const ImVec2 pill(x + w - 55.0f, y + 8.0f);
    d->AddRectFilled(pill, pill + ImVec2(42.f, 21.f), theme::col::pill(), 5.f);
    d->AddRect(pill, pill + ImVec2(42.f, 21.f), theme::col::pillBorder(), 5.f);
    const float pw = TxtW(buf, T_CTRL, theme::fontBody());
    TxtY(d, pill.x + std::floor((42.f - pw) * 0.5f), pill.y, 21.f, theme::col::value(), buf,
         T_CTRL, theme::fontBody());
    return changed;
}

void chevron(ImDrawList* d, ImVec2 p, ImU32 c) {
    d->AddLine(p, p + ImVec2(3.f, 3.f), c, 1.3f);
    d->AddLine(p + ImVec2(3.f, 3.f), p + ImVec2(0.f, 6.f), c, 1.3f);
}

bool RowSelect(ImDrawList* d, float x, float y, float w, const char* uid,
               const char* label, int* v, const char* const* items, int count,
               bool first) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());

    const float cw = clampf(w * 0.48f, 96.f, 140.f);
    const ImVec2 cp(x + w - cw - 13.0f, y + 7.0f);

    ImGui::PushID(uid);
    const ImGuiID self = ImGui::GetID("##hit");
    int* slot = slotFor(self);

    // A pick made last frame lands here, then the slot is re-synced below.
    bool changed = false;
    if (g_popup.pickedOwner == self) {
        *v = *slot;
        g_popup.pickedOwner = 0;
        changed = true;
    }
    *slot = *v;

    const bool clicked = Hit("##hit", cp, ImVec2(cw, 23.f));
    const float hover = Motion("hv", ImGui::IsItemHovered() ? 1.f : 0.f, 20.f);
    ImGui::PopID();

    if (clicked) openPopup(self, cp, cw, items, count, slot);

    const bool active = g_popup.open && g_popup.owner == self;
    d->AddRectFilled(cp, cp + ImVec2(cw, 23.f),
                     mixU32(theme::col::pill(), theme::accentU32(0.22f),
                            clampf(hover + (active ? 0.35f : 0.f), 0.f, 1.f)),
                     5.f);
    d->AddRect(cp, cp + ImVec2(cw, 23.f),
               mixU32(theme::col::pillBorder(), theme::accentU32(1.f),
                      active ? 1.f : hover * 0.8f), 5.f);

    const char* text = (items && *v >= 0 && *v < count) ? items[*v] : "-";
    TxtY(d, cp.x + 8.f, cp.y, 23.f, theme::col::value(), text, T_CTRL, theme::fontBody());
    chevron(d, cp + ImVec2(cw - 14.f, 9.f), theme::col::labelDim());
    return changed;
}

bool RowColor(ImDrawList* d, float x, float y, float w, const char* uid,
              const char* label, float rgba[4], bool first) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());

    ImGui::PushID(uid);
    ImGui::SetCursorScreenPos(ImVec2(x + w - 62.0f, y + 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    const bool changed = ImGui::ColorEdit4("##c", rgba,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

bool RowButton(ImDrawList* d, float x, float y, float w, const char* uid,
               const char* label, const char* action, bool first) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());

    const ImVec2 bp(x + w - 96.0f, y + 8.0f);
    ImGui::PushID(uid);
    const bool clicked = Hit("##hit", bp, ImVec2(84.f, 21.f));
    const float hover = Motion("hv", ImGui::IsItemHovered() ? 1.f : 0.f, 20.f);
    ImGui::PopID();

    d->AddRectFilled(bp, bp + ImVec2(84.f, 21.f),
                     mixU32(theme::col::pill(), theme::accentU32(0.30f), hover), 5.f);
    d->AddRect(bp, bp + ImVec2(84.f, 21.f),
               mixU32(theme::col::pillBorder(), theme::accentU32(0.9f), hover), 5.f);
    const float tw = TxtW(action, T_CTRL, theme::fontBody());
    TxtY(d, bp.x + std::floor((84.f - tw) * 0.5f), bp.y, 21.f,
         mixU32(theme::col::value(), theme::accentU32(1.f), hover), action, T_CTRL,
         theme::fontBody());
    return clicked;
}

void RowInfo(ImDrawList* d, float x, float y, float w, const char* label,
             const char* value, ImU32 valueColor, bool first) {
    if (!first) rowLine(d, x, y, w);
    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());
    const float vw = TxtW(value, T_CTRL, theme::fontBody());
    const float maxW = w - 32.f;
    const float vx = x + w - 13.f - (vw > maxW ? maxW : vw);
    TxtY(d, vx, y, kRowH, valueColor, value, T_CTRL, theme::fontBody());
}

bool RowText(ImDrawList* d, float x, float y, float w, const char* uid,
             const char* label, char* buf, size_t cap, bool first) {
    if (!first) rowLine(d, x, y, w);

    TxtY(d, x + 13.0f, y, kRowH, theme::col::label(), label, T_BODY, theme::fontBody());

    ImGui::PushID(uid);
    ImGui::SetCursorScreenPos(ImVec2(x + w - 150.0f, y + 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, 4.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(theme::col::pill()));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(theme::col::pillBorder()));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme::col::value()));
    ImGui::SetNextItemWidth(137.f);
    const bool changed = ImGui::InputText("##t", buf, cap);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------------- icons ---
// Drawn rather than pulled from an icon font: one less dependency in the atlas.
void iconCrosshair(ImDrawList* d, ImVec2 c, ImU32 col) {
    d->AddCircle(c, 5.5f, col, 0, 1.4f);
    d->AddLine(c + ImVec2(-9.f, 0.f), c + ImVec2(-6.f, 0.f), col, 1.4f);
    d->AddLine(c + ImVec2(6.f, 0.f), c + ImVec2(9.f, 0.f), col, 1.4f);
    d->AddLine(c + ImVec2(0.f, -9.f), c + ImVec2(0.f, -6.f), col, 1.4f);
    d->AddLine(c + ImVec2(0.f, 6.f), c + ImVec2(0.f, 9.f), col, 1.4f);
    d->AddCircleFilled(c, 1.4f, col);
}

void iconEye(ImDrawList* d, ImVec2 c, ImU32 col) {
    d->PathArcTo(c + ImVec2(0.f, 6.f), 10.f, -2.55f, -0.6f, 16);
    d->PathStroke(col, 0, 1.4f);
    d->AddCircleFilled(c, 2.6f, col);
}

void iconSliders(ImDrawList* d, ImVec2 c, ImU32 col) {
    for (int i = -1; i <= 1; ++i) {
        const float yy = c.y + i * 4.5f;
        d->AddLine(ImVec2(c.x - 8.f, yy), ImVec2(c.x + 8.f, yy), col, 1.2f);
        const float kx = c.x + (i == 0 ? 3.f : (i < 0 ? -4.f : 1.f));
        d->AddCircleFilled(ImVec2(kx, yy), 2.1f, col);
    }
}

void iconGear(ImDrawList* d, ImVec2 c, ImU32 col) {
    d->AddCircle(c, 4.f, col, 0, 1.4f);
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * 0.785398f;
        const ImVec2 o(std::cos(a), std::sin(a));
        d->AddLine(c + o * 5.6f, c + o * 8.2f, col, 1.4f);
    }
    d->AddCircleFilled(c, 1.2f, col);
}

// ----------------------------------------------------------------- toolbar ---
void attachPill(ImDrawList* d, ImVec2 p, bool attached) {
    const ImU32 dot = attached ? theme::col::good() : theme::col::bad();
    const char* text = attached ? "attached" : "waiting";
    const float w = 104.f;

    d->AddRectFilled(p, p + ImVec2(w, 28.f), theme::col::pill(), 8.f);
    d->AddRect(p, p + ImVec2(w, 28.f), theme::col::pillBorder(), 8.f);
    d->AddCircleFilled(p + ImVec2(14.f, 14.f), 3.6f, dot);
    TxtY(d, p.x + 26.f, p.y, 28.f, theme::col::value(), text, T_CTRL, theme::fontBody());
}

// ----------------------------------------------------------------- the shell -
void sidebar(ImDrawList* d, ImVec2 b, State& state, const Status& status, bool& changedTab) {
    // Body, with the left corners rounded and the right edge squared off.
    d->AddRectFilled(b, b + ImVec2(kSidebarW, kShellH), theme::col::sidebar(), kRadius,
                     ImDrawFlags_RoundCornersLeft);
    d->AddRectFilled(b + ImVec2(kSidebarW - 13.f, 0.f), b + ImVec2(kSidebarW, kShellH),
                     theme::col::sidebar());
    d->AddLine(b + ImVec2(kSidebarW, 0.f), b + ImVec2(kSidebarW, kShellH),
               theme::col::divider());

    // Brand block.
    d->AddRectFilled(b + ImVec2(15.f, 12.f), b + ImVec2(45.f, 44.f), theme::accentU32(0.16f), 8.f);
    d->AddRect(b + ImVec2(15.f, 12.f), b + ImVec2(45.f, 44.f), theme::accentU32(0.55f), 8.f);
    Txt(d, b + ImVec2(22.f, 16.f), theme::accentU32(1.f), "無", 19.f, theme::fontTitle());
    Txt(d, b + ImVec2(53.f, 12.f), IM_COL32(228, 230, 236, 255), "muei", T_TITLE, theme::fontTitle());
    Txt(d, b + ImVec2(53.f, 35.f), theme::col::caption(), "Counter-Strike 2", T_SMALL - 1.f,
        theme::fontSmall());

    d->AddLine(b + ImVec2(12.f, 58.f), b + ImVec2(kSidebarW - 12.f, 58.f), theme::col::divider());

    struct NavItem { const char* label; float y; int tab; void (*icon)(ImDrawList*, ImVec2, ImU32); };
    const NavItem items[] = {
        { "Aimbot",  84.f, TabAimbot,  iconCrosshair },
        { "Visuals", 141.f, TabVisuals, iconEye },
        { "Misc",    198.f, TabMisc,   iconSliders },
        { "Config",  234.f, TabConfig, iconGear },
    };
    const char* captions[] = { "COMBAT", "DISPLAY", "UTILITY" };
    const float captionY[] = { 67.f, 124.f, 181.f };
    for (int i = 0; i < 3; ++i) {
        Txt(d, b + ImVec2(16.f, captionY[i]), theme::col::caption(), captions[i], T_CAP,
            theme::fontSmall());
    }

    for (int i = 0; i < 4; ++i) {
        const NavItem& it = items[i];
        const bool selected = (state.tab == it.tab);
        const ImVec2 p = b + ImVec2(8.f, it.y);

        ImGui::PushID(100 + i);
        const bool clicked = Hit("##nav", p, ImVec2(kSidebarW - 20.f, 30.f));
        const float r = Motion("hv", selected ? 1.f : (ImGui::IsItemHovered() ? 0.5f : 0.f), 18.f);
        ImGui::PopID();

        if (clicked && state.tab != it.tab) changedTab = true;

        if (r > 0.002f) {
            d->AddRectFilled(p, p + ImVec2(kSidebarW - 20.f, 30.f),
                             mixU32(theme::col::sidebar(), IM_COL32(39, 43, 54, 255), r), 7.f);
        }
        if (selected) {
            d->AddRectFilled(p + ImVec2(0.f, 7.f), p + ImVec2(2.5f, 23.f),
                             theme::accentU32(1.f), 2.f);
        }

        it.icon(d, p + ImVec2(18.f, 15.f), mixU32(theme::col::labelDim(), theme::accentU32(1.f), r));
        TxtY(d, p.x + 34.f, p.y, 30.f, mixU32(theme::col::labelDim(), theme::col::label(), r),
             it.label, T_BODY, theme::fontBody());
    }

    // Live footer: backend and pid, so the sidebar never lies about the state.
    const ImVec2 foot = b + ImVec2(8.f, kShellH - 62.f);
    d->AddRectFilled(foot, foot + ImVec2(kSidebarW - 20.f, 46.f), theme::col::card(), 8.f);
    d->AddRect(foot, foot + ImVec2(kSidebarW - 20.f, 46.f), theme::col::cardBorder(), 8.f);
    d->AddCircleFilled(foot + ImVec2(14.f, 16.f), 3.2f,
                       status.attached ? theme::col::good() : theme::col::bad());
    TxtY(d, foot.x + 24.f, foot.y + 6.f, 20.f, theme::col::label(), status.backend,
         T_SMALL, theme::fontSmall());
    char line[64];
    std::snprintf(line, sizeof(line), "pid %lu", status.pid);
    TxtY(d, foot.x + 24.f, foot.y + 24.f, 16.f, theme::col::caption(), line, T_SMALL - 1.f,
         theme::fontSmall());
}

void toolbar(ImDrawList* d, ImVec2 b, const Status& status) {
    d->AddRectFilled(b + ImVec2(kSidebarW, 0.f), b + ImVec2(kShellW, kToolbarH),
                     theme::col::toolbar(), kRadius, ImDrawFlags_RoundCornersTopRight);
    d->AddLine(b + ImVec2(kSidebarW, kToolbarH), b + ImVec2(kShellW, kToolbarH),
               theme::col::divider());

    attachPill(d, b + ImVec2(kSidebarW + 16.f, 14.f), status.attached);

    char players[48];
    std::snprintf(players, sizeof(players), "%d players", status.players);
    TxtY(d, b.x + kSidebarW + 134.f, b.y + 14.f, 28.f, theme::col::caption(), players,
         T_SMALL, theme::fontSmall());

    // Build tag, right aligned.
    char build[48];
    std::snprintf(build, sizeof(build), "build %s", status.build ? status.build : "?");
    const float bw = TxtW(build, T_SMALL, theme::fontSmall());
    TxtY(d, b.x + kShellW - 20.f - bw, b.y + 14.f, 28.f, theme::col::caption(), build,
         T_SMALL, theme::fontSmall());
}

// ------------------------------------------------------------------- pages ---
void pageAimbot(ImDrawList* d, ImVec2 b, Config& cfg, bool& changed) {
    const float x0 = b.x + X0, xr = b.x + XR, y = b.y + Y0;

    int mode = (int)cfg.aimMode;
    int key  = (int)cfg.aimKey;
    int bone = cfg.aimBone;

    static const char* bones[] = { "Head", "Neck", "Spine", "Chest", "Pelvis" };
    static const int   boneVals[] = { Bone::HEAD, Bone::NECK, Bone::SPINE, Bone::CHEST, Bone::PELVIS };
    int boneIdx = 0;
    for (int i = 0; i < 5; ++i) if (boneVals[i] == bone) boneIdx = i;

    static const char* methods[] = { "Relative mouse", "Angle write" };
    static const char* keys[]    = { "Right mouse", "Left mouse", "Shift", "Alt", "Mouse 4" };

    Card(d, ImVec2(x0, y), CW, 6, "AIMBOT");
    changed |= RowToggle(d, x0, y, CW, "a.ena", "Enable aimbot", &cfg.aimbot, true);
    changed |= RowToggle(d, x0, y + 37.f, CW, "a.vis", "Visible only", &cfg.aimVisibleOnly, false);
    changed |= RowToggle(d, x0, y + 74.f, CW, "a.aut", "Auto fire", &cfg.aimAutoFire, false);
    changed |= RowSelect(d, x0, y + 111.f, CW, "a.bone", "Target bone", &boneIdx, bones, 5, false);
    changed |= RowSelect(d, x0, y + 148.f, CW, "a.mode", "Method", &mode, methods, 2, false);
    changed |= RowSelect(d, x0, y + 185.f, CW, "a.key", "Hotkey", &key, keys, 5, false);
    cfg.aimBone = boneVals[boneIdx];
    cfg.aimMode = (AimMode)mode;
    cfg.aimKey  = (AimKey)key;

    Card(d, ImVec2(xr, y), CR, 5, "AIM");
    changed |= RowSlider(d, xr, y, CR, "a.fov", "Field of view", &cfg.aimFov, 1.f, 60.f, "%.1f", true);
    changed |= RowSlider(d, xr, y + 37.f, CR, "a.sm", "Smoothing", &cfg.aimSmooth, 1.f, 20.f, "%.2f", false);
    changed |= RowSlider(d, xr, y + 74.f, CR, "a.md", "Max distance", &cfg.aimMaxDist, 200.f, 8000.f, "%.0f", false);
    changed |= RowSlider(d, xr, y + 111.f, CR, "a.sn", "Sensitivity", &cfg.aimSens, 0.1f, 10.f, "%.2f", false);
    changed |= RowToggle(d, xr, y + 148.f, CR, "a.drv", "Driver input", &cfg.aimUseDriver, false);

    const float y2 = y + 6.f * kRowH + 26.f;

    static const char* strategies[] = { "Off", "Travel time", "Travel + jitter" };
    int strategy = (int)cfg.resolverMode;

    Card(d, ImVec2(x0, y2), CW, 4, "RESOLVER");
    changed |= RowToggle(d, x0, y2, CW, "r.ena", "Lead moving targets", &cfg.resolverOn, true);
    changed |= RowSelect(d, x0, y2 + 37.f, CW, "r.str", "Strategy", &strategy, strategies, 3, false);
    changed |= RowSlider(d, x0, y2 + 74.f, CW, "r.sp", "Bullet speed", &cfg.resolverBulletSpeed, 200.f, 2500.f, "%.0f", false);
    changed |= RowSlider(d, x0, y2 + 111.f, CW, "r.lat", "Latency", &cfg.resolverLatency, 0.f, 0.08f, "%.3f", false);
    cfg.resolverMode = (ResolveMode)strategy;

    Card(d, ImVec2(xr, y2), CR, 4, "RECOIL");
    changed |= RowToggle(d, xr, y2, CR, "c.ena", "Recoil control", &cfg.rcs, true);
    changed |= RowSliderInt(d, xr, y2 + 37.f, CR, "c.bul", "Start after", &cfg.rcsBullets, 0, 10, false);
    changed |= RowSlider(d, xr, y2 + 74.f, CR, "c.sx", "Horizontal scale", &cfg.rcsScaleX, 0.1f, 3.f, "%.2f", false);
    changed |= RowSlider(d, xr, y2 + 111.f, CR, "c.sy", "Vertical scale", &cfg.rcsScaleY, 0.1f, 3.f, "%.2f", false);
}

// Decorative board for the gap under the colour card: it draws the same box,
// health bar and label the ESP would put on a player, so the operator can read
// the current colours without alt-tabbing into a match. Purely a preview; it
// takes no input and reflects cfg, it does not sample the game.
void previewCard(ImDrawList* d, ImVec2 p, float w, float h, const Config& cfg) {
    Txt(d, p + ImVec2(13.0f, -16.0f), theme::col::caption(), "PREVIEW", T_CAP, theme::fontSmall());
    d->AddRectFilled(p, p + ImVec2(w, h), theme::col::card(), kRadius);
    d->AddRect(p, p + ImVec2(w, h), theme::col::cardBorder(), kRadius);

    const ImU32 accent = IM_COL32((int)(cfg.espEnemyCol[0] * 255.f),
                                  (int)(cfg.espEnemyCol[1] * 255.f),
                                  (int)(cfg.espEnemyCol[2] * 255.f), 235);

    // Target box, centred with a little headroom for the label.
    const ImVec2 boxMin(p.x + w * 0.5f - 26.f, p.y + 40.f);
    const ImVec2 boxMax(p.x + w * 0.5f + 26.f, p.y + h - 26.f);

    d->AddRect(boxMin - ImVec2(1.f, 1.f), boxMax + ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 150), 0.f, 0, 2.6f);
    d->AddRect(boxMin, boxMax, accent, 0.f, 0, 1.4f);

    // Corner ticks, the way most ESPs read at a glance.
    const float k = 9.f;
    d->AddLine(boxMin, ImVec2(boxMin.x + k, boxMin.y), accent, 2.f);
    d->AddLine(boxMin, ImVec2(boxMin.x, boxMin.y + k), accent, 2.f);
    d->AddLine(ImVec2(boxMax.x - k, boxMin.y), ImVec2(boxMax.x, boxMin.y), accent, 2.f);
    d->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x, boxMin.y + k), accent, 2.f);
    d->AddLine(ImVec2(boxMin.x, boxMax.y - k), ImVec2(boxMin.x, boxMax.y), accent, 2.f);
    d->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x + k, boxMax.y), accent, 2.f);
    d->AddLine(ImVec2(boxMax.x - k, boxMax.y), ImVec2(boxMax.x, boxMax.y), accent, 2.f);
    d->AddLine(boxMax - ImVec2(0.f, k), boxMax, accent, 2.f);

    // Health bar on the left edge.
    const float barH = boxMax.y - boxMin.y;
    const ImVec2 hbMin(boxMin.x - 7.f, boxMin.y);
    const ImVec2 hbMax(boxMin.x - 3.f, boxMax.y);
    d->AddRectFilled(hbMin - ImVec2(1.f, 1.f), hbMax + ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 190));
    d->AddRectFilled(ImVec2(hbMin.x, hbMin.y + barH * 0.3f), hbMax, IM_COL32(120, 220, 120, 230));

    // Label above the box, distance below.
    const char* name = "player";
    const float nw = TxtW(name, T_SMALL, theme::fontSmall());
    Txt(d, ImVec2(boxMin.x + (boxMax.x - boxMin.x - nw) * 0.5f, boxMin.y - 15.f),
        accent, name, T_SMALL, theme::fontSmall());

    const char* dist = "12 m";
    const float dw = TxtW(dist, T_SMALL, theme::fontSmall());
    Txt(d, ImVec2(boxMin.x + (boxMax.x - boxMin.x - dw) * 0.5f, boxMax.y + 3.f),
        theme::col::labelDim(), dist, T_SMALL, theme::fontSmall());

    // FOV ring around a crosshair, bottom right of the board.
    const ImVec2 c(p.x + w - 46.f, p.y + h - 40.f);
    const float radius = clampf(std::tanf(cfg.aimFov * DEG2RAD) * 62.f, 9.f, 26.f);
    d->AddCircle(c, radius, theme::accentU32(0.35f), 0, 1.f);
    d->AddLine(c - ImVec2(4.f, 0.f), c + ImVec2(4.f, 0.f), theme::col::labelDim(), 1.f);
    d->AddLine(c - ImVec2(0.f, 4.f), c + ImVec2(0.f, 4.f), theme::col::labelDim(), 1.f);
}

void pageVisuals(ImDrawList* d, ImVec2 b, Config& cfg, bool& changed) {
    const float x0 = b.x + X0, xr = b.x + XR, y = b.y + Y0;

    Card(d, ImVec2(x0, y), CW, 7, "PLAYERS");
    changed |= RowToggle(d, x0, y, CW, "e.ena", "Enable ESP", &cfg.esp, true);
    changed |= RowToggle(d, x0, y + 37.f, CW, "e.box", "Box", &cfg.espBox, false);
    changed |= RowToggle(d, x0, y + 74.f, CW, "e.fil", "Filled box", &cfg.espFilled, false);
    changed |= RowToggle(d, x0, y + 111.f, CW, "e.hp", "Health bar", &cfg.espHealth, false);
    changed |= RowToggle(d, x0, y + 148.f, CW, "e.sk", "Skeleton", &cfg.espSkeleton, false);
    changed |= RowToggle(d, x0, y + 185.f, CW, "e.sn", "Snaplines", &cfg.espSnaplines, false);
    changed |= RowToggle(d, x0, y + 222.f, CW, "e.di", "Distance", &cfg.espDistance, false);

    static const char* filters[] = { "All players", "Enemies only" };
    Card(d, ImVec2(xr, y), CR, 3, "FILTER");
    changed |= RowToggle(d, xr, y, CR, "f.nm", "Player name", &cfg.espName, true);
    changed |= RowSelect(d, xr, y + 37.f, CR, "f.tm", "Team filter", &cfg.espTeamFilter, filters, 2, false);
    changed |= RowSlider(d, xr, y + 74.f, CR, "f.md", "Max distance", &cfg.espMaxDist, 200.f, 8000.f, "%.0f", false);

    const float yC = y + 3.f * kRowH + 26.f;
    Card(d, ImVec2(xr, yC), CR, 4, "COLOURS");
    changed |= RowColor(d, xr, yC, CR, "k.en", "Enemy", cfg.espEnemyCol, true);
    changed |= RowColor(d, xr, yC + 37.f, CR, "k.tm", "Team", cfg.espTeamCol, false);
    changed |= RowColor(d, xr, yC + 74.f, CR, "k.sk", "Skeleton", cfg.espSkelCol, false);
    changed |= RowSlider(d, xr, yC + 111.f, CR, "k.th", "Box thickness", &cfg.espThickness, 1.f, 5.f, "%.1f", false);

    const float yG = y + 7.f * kRowH + 26.f;
    Card(d, ImVec2(x0, yG), CW, 4, "GLOW");
    changed |= RowToggle(d, x0, yG, CW, "g.ena", "Enable glow", &cfg.glow, true);
    changed |= RowSliderInt(d, x0, yG + 37.f, CW, "g.ty", "Glow type", &cfg.glowType, 0, 3, false);
    changed |= RowColor(d, x0, yG + 74.f, CW, "g.en", "Enemy glow", cfg.glowEnemy, false);
    changed |= RowColor(d, x0, yG + 111.f, CW, "g.tm", "Team glow", cfg.glowTeam, false);

    // Aligned with the GLOW card so both columns close on the same baseline.
    previewCard(d, ImVec2(xr, yG), CR, 4.f * kRowH, cfg);
}

void pageMisc(ImDrawList* d, ImVec2 b, Config& cfg, const Status& status, bool& changed) {
    const float x0 = b.x + X0, xr = b.x + XR, y = b.y + Y0;

    Card(d, ImVec2(x0, y), CW, 2, "MOVEMENT");
    changed |= RowToggle(d, x0, y, CW, "m.bh", "Bunny hop", &cfg.bhop, true);
    changed |= RowToggle(d, x0, y + 37.f, CW, "m.as", "Auto strafe", &cfg.bhopAutoStrafe, false);

    const float yT = y + 2.f * kRowH + 26.f;
    Card(d, ImVec2(x0, yT), CW, 6, "TRIGGERBOT");
    changed |= RowToggle(d, x0, yT, CW, "t.ena", "Enable triggerbot", &cfg.triggerbot, true);
    changed |= RowToggle(d, x0, yT + 37.f, CW, "t.tm", "Enemies only", &cfg.triggerTeamFlag, false);
    changed |= RowToggle(d, x0, yT + 74.f, CW, "t.hd", "Head check", &cfg.triggerHeadOnly, false);
    changed |= RowSliderInt(d, x0, yT + 111.f, CW, "t.dl", "Delay", &cfg.triggerDelayMs, 0, 250, false);
    changed |= RowSliderInt(d, x0, yT + 148.f, CW, "t.ho", "Hold time", &cfg.triggerHoldMs, 1, 200, false);
    changed |= RowToggle(d, x0, yT + 185.f, CW, "t.dr", "Driver click", &cfg.triggerUseDriver, false);

    static const char* panics[] = { "End", "F1", "F2", "Delete", "Insert" };
    static const int   panicVals[] = { VK_END, VK_F1, VK_F2, VK_DELETE, VK_INSERT };
    int panicIdx = 0;
    for (int i = 0; i < 5; ++i) if (panicVals[i] == cfg.panicKey) panicIdx = i;

    Card(d, ImVec2(xr, y), CR, 2, "PLAYER");
    changed |= RowToggle(d, xr, y, CR, "p.nf", "No flash", &cfg.noFlash, true);
    changed |= RowSelect(d, xr, y + 37.f, CR, "p.pk", "Panic key", &panicIdx, panics, 5, false);
    cfg.panicKey = panicVals[panicIdx];

    const float yS = y + 2.f * kRowH + 26.f;
    Card(d, ImVec2(xr, yS), CR, 6, "SKINS");
    changed |= RowToggle(d, xr, yS, CR, "s.ena", "Skin changer", &cfg.skinChanger, true);
    changed |= RowSliderInt(d, xr, yS + 37.f, CR, "s.pk2", "Paint kit", &cfg.skinPaintKit, 0, 2000, false);
    changed |= RowSlider(d, xr, yS + 74.f, CR, "s.wr", "Wear", &cfg.skinWear, 0.f, 1.f, "%.4f", false);
    changed |= RowSliderInt(d, xr, yS + 111.f, CR, "s.st", "StatTrak", &cfg.skinStatTrak, -1, 9999, false);
    changed |= RowText(d, xr, yS + 148.f, CR, "s.nm", "Name", cfg.skinName, sizeof(cfg.skinName), false);
    changed |= RowToggle(d, xr, yS + 185.f, CR, "s.mk", "Model mask", &cfg.skinSetMask, false);

    if (!status.skinReady) {
        Txt(d, ImVec2(xr + 4.f, yS + 6.f * kRowH + 9.f), theme::col::warn(),
            "needs offsets in offsets.cfg", T_SMALL, theme::fontSmall());
    }
}

void pageConfig(ImDrawList* d, ImVec2 b, Config& cfg, const Status& status, bool& changed) {
    const float x0 = b.x + X0, xr = b.x + XR, y = b.y + Y0;

    static const char* backends[] = { "Auto", "User handle", "Kernel driver", "DMA" };
    static const char* menuKeys[] = { "Insert", "F1", "F2", "Home", "Delete" };
    static const int   menuVals[] = { VK_INSERT, VK_F1, VK_F2, VK_HOME, VK_DELETE };

    int menuIdx = 0;
    for (int i = 0; i < 5; ++i) if (menuVals[i] == cfg.menuKey) menuIdx = i;

    Card(d, ImVec2(x0, y), CW, 3, "MEMORY");
    changed |= RowSelect(d, x0, y, CW, "c.bk", "Backend", &cfg.backend, backends, 4, true);
    changed |= RowSelect(d, x0, y + 37.f, CW, "c.mk", "Menu key", &menuIdx, menuKeys, 5, false);
    cfg.menuKey = menuVals[menuIdx];
    RowInfo(d, x0, y + 74.f, CW, "Backend in use", status.backend,
            status.attached ? theme::col::good() : theme::col::bad(), false);

    Card(d, ImVec2(xr, y), CR, 1, "THEME");
    changed |= RowColor(d, xr, y, CR, "c.ac", "Accent", cfg.accentCol, true);

    const float yF = y + 3.f * kRowH + 26.f;
    Card(d, ImVec2(x0, yF), CW, 3, "FILE");
    if (RowButton(d, x0, yF, CW, "c.sv", "Save now", "write", true)) {
        cfg.save();
    }
    if (RowButton(d, x0, yF + 37.f, CW, "c.ld", "Reload from disk", "read", false)) {
        cfg.load();
        changed = true;
    }
    if (RowButton(d, x0, yF + 74.f, CW, "c.rs", "Reset defaults", "reset", false)) {
        cfg.reset();
        changed = true;
    }

    const float ySt = y + 1.f * kRowH + 26.f;
    Card(d, ImVec2(xr, ySt), CR, 5, "STATUS");
    char buf[64];

    std::snprintf(buf, sizeof(buf), "%lu", status.pid);
    RowInfo(d, xr, ySt, CR, "Process id", buf,
            status.pid ? theme::col::label() : theme::col::labelDim(), true);

    std::snprintf(buf, sizeof(buf), "%d", status.players);
    RowInfo(d, xr, ySt + 37.f, CR, "Entities", buf, theme::col::label(), false);

    RowInfo(d, xr, ySt + 74.f, CR, "Recoil offsets",
            status.rcsReady ? "ready" : "missing",
            status.rcsReady ? theme::col::good() : theme::col::warn(), false);

    RowInfo(d, xr, ySt + 111.f, CR, "Skin offsets",
            status.skinReady ? "ready" : "missing",
            status.skinReady ? theme::col::good() : theme::col::warn(), false);

    RowInfo(d, xr, ySt + 148.f, CR, "Target",
            status.aimTarget >= 0 ? "locked" : "-",
            status.aimTarget >= 0 ? theme::accentU32(1.f) : theme::col::labelDim(), false);
}

// ------------------------------------------------------------- popup layer ---
void popupLayer(ImDrawList* d) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_popup.open = false;

    const float open = Motion("##popup", g_popup.open ? 1.f : 0.f, 22.f, 0.f);
    if (open < 0.002f || !g_popup.items) return;

    const int count = g_popup.count;
    const ImVec2 size(g_popup.width, count * 32.f + 10.f);

    ImVec2 p(g_popup.anchor.x, g_popup.anchor.y + 27.f);
    p.x = clampf(p.x, 10.f, ImGui::GetIO().DisplaySize.x - size.x - 10.f);
    p.y = clampf(p.y, 10.f, ImGui::GetIO().DisplaySize.y - size.y - 10.f);

    const int first = d->VtxBuffer.Size;

    d->AddRectFilled(p, p + size, IM_COL32(20, 20, 29, 255), 14.f);
    d->AddRect(p, p + size, IM_COL32(57, 61, 76, 190), 14.f);
    d->AddLine(p + ImVec2(14.f, 1.f), p + ImVec2(size.x - 14.f, 1.f),
               IM_COL32(255, 255, 255, 20));

    // A click that lands outside the list closes it. Done before the rows so a
    // row click is not also treated as an outside click.
    const bool accepts = ImGui::GetFrameCount() > g_popup.openedFrame;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const bool overList = mp.x >= p.x && mp.x <= p.x + size.x &&
                          mp.y >= p.y && mp.y <= p.y + size.y;
    if (accepts && ImGui::IsMouseClicked(0) && !overList) g_popup.open = false;

    for (int i = 0; i < count; ++i) {
        const ImVec2 rp = p + ImVec2(5.f, 5.f + i * 32.f);

        ImGui::PushID(7000 + i);
        const bool clicked = Hit("##row", rp, ImVec2(size.x - 10.f, 32.f));
        const float hover = Motion("hv", ImGui::IsItemHovered() ? 1.f : 0.f, 22.f);
        ImGui::PopID();

        if (hover > 0.001f) {
            d->AddRectFilled(rp, rp + ImVec2(size.x - 10.f, 32.f),
                             theme::accentU32(0.16f * hover), 8.f);
        }

        const bool selected = g_popup.target && *g_popup.target == i;
        if (selected) {
            const ImVec2 c = rp + ImVec2(15.f, 16.f);
            d->AddLine(c, c + ImVec2(3.f, 3.f), theme::col::knobOn(), 1.5f);
            d->AddLine(c + ImVec2(3.f, 3.f), c + ImVec2(8.f, -3.f), theme::col::knobOn(), 1.5f);
        }

        TxtY(d, rp.x + 34.f, rp.y, 32.f,
             selected ? IM_COL32(224, 229, 243, 255) : IM_COL32(182, 185, 196, 255),
             g_popup.items[i], T_CTRL, theme::fontBody());

        if (accepts && clicked && g_popup.target) {
            *g_popup.target = i;
            g_popup.pickedOwner = g_popup.owner;
            g_popup.open = false;
        }
    }

    // Scale + fade the block we just emitted.
    const float eased = 1.f - std::pow(1.f - open, 3.f);
    const ImVec2 pivot(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    for (int i = first; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos = pivot + (v.pos - pivot) * lerp(0.95f, 1.f, eased) +
                ImVec2(3.f * (1.f - eased), 0.f);
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00FFFFFFu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }
}

} // namespace

bool draw(State& state, Config& cfg, const Frame& frame, const Status& status) {
    (void)frame;
    if (!state.open) return false;

    bool changed = false;

    ImGui::SetNextWindowSize(ImVec2(kShellW, kShellH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(120.f, 90.f), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRadius);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("##muei_menu", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        return false;
    }

    const ImVec2 b = ImGui::GetWindowPos();
    ImDrawList* d = ImGui::GetWindowDrawList();

    // Shell body. Opaque on purpose: in own-window mode the swap chain is
    // cleared to the colour key, and anything translucent over that key blends
    // into a purple tint instead of being keyed out. No drop shadow either, for
    // the same reason.
    d->AddRectFilled(b, b + ImVec2(kShellW, kShellH), theme::col::window(), kRadius);

    bool changedTab = false;
    sidebar(d, b, state, status, changedTab);
    if (changedTab) g_popup.open = false;

    toolbar(d, b, status);

    // Content, with a short fade-and-slide when the page changes.
    const int contentFirst = d->VtxBuffer.Size;

    ImGui::SetCursorScreenPos(b + ImVec2(kSidebarW + 14.f, Y0));
    ImGui::PushID(state.tab);

    switch (state.tab) {
        case TabAimbot:  pageAimbot(d, b, cfg, changed); break;
        case TabVisuals: pageVisuals(d, b, cfg, changed); break;
        case TabMisc:    pageMisc(d, b, cfg, status, changed); break;
        case TabConfig:  pageConfig(d, b, cfg, status, changed); break;
        default: break;
    }

    ImGui::PopID();

    const float mix = Motion("##page_mix", 1.f, 15.f, changedTab ? 0.f : 1.f);
    const float eased = 1.f - std::pow(1.f - mix, 3.f);
    for (int i = contentFirst; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos.x += 9.f * (1.f - eased);
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00FFFFFFu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }

    popupLayer(d);

    // Clicking the shell background closes an open select.
    if (g_popup.open && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() &&
        !ImGui::IsAnyItemHovered()) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool inShell = mp.x >= b.x && mp.x <= b.x + kShellW &&
                             mp.y >= b.y && mp.y <= b.y + kShellH;
        if (!inShell) g_popup.open = false;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return changed;
}

} // namespace menu
