// muei 無影 - menu.
//
// Fixed-size window with a left navigation rail and a scrolling content pane.
// The rail owns the brand and the tab list; the pane owns the controls; the
// footer owns the truth about what the process is currently doing.
//
// draw() returns true when any control changed, which the caller uses to arm the
// debounced config save.
#pragma once

#include <string>

#include "../core/Config.h"
#include "../sdk/Frame.h"

namespace menu {

enum Tab {
    TabVisuals = 0,
    TabAimbot,
    TabMisc,
    TabConfig,
    TabCount
};

// Live state the footer reports. Filled by main each frame.
struct Status {
    bool        attached   = false;
    bool        elevated   = false;
    const char* backend    = "none";
    unsigned long pid      = 0;
    int         players    = 0;
    int         aimTarget  = -1;
    bool        rcsReady   = false;
    bool        skinReady  = false;
    std::string skinMissing;
    const char* build      = "?";
};

struct State {
    int  tab    = TabVisuals;
    bool open   = true;
};

// Returns true when cfg changed this frame.
bool draw(State& state, Config& cfg, const Frame& frame, const Status& status);

} // namespace menu
