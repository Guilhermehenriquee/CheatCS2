# muei ??

CS2 external overlay - C++20, D3D11 + Dear ImGui 1.91.9b, three memory backends
behind one interface.

Structure:

    core/      memory backends (user, kernel driver, DMA), config, logging
    sdk/       offsets + runtime override loader, math, entity model, traversal
    features/  ESP, aimbot, resolver, triggerbot, RCS, bhop, glow, skins, misc
    ui/        theme, D3D11 overlay, custom-drawn menu
    imgui/     vendored 1.91.9b + win32/dx11 backends
    vendor/    nlohmann/json 3.11.3

Build:

    build.bat          MSVC -> muei.exe with embedded requireAdministrator manifest
    build_mingw.bat    MinGW-w64, statically linked

Run:

    build.bat
    muei.exe           INSERT opens the menu, END quits

Then, with CS2 in a map and running elevated:

    verify.bat         proves every offset against live memory, PASS/FAIL per field

Offsets can be overridden without a rebuild: copy offsets.cfg.example to
offsets.cfg next to the exe.

Full documentation, including the UI notes and what has been verified, is in
[README.md](README.md).

---

This tree replaces the previous flat-header build (main.cpp + proc.h +

av.h + math.h + offsets.h + eatures.h). Those single-file headers were
split into the modules above; the game-facing behaviour is the same, and the
memory layer gained two additional backends.
