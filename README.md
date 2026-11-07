# muei 無影

CS2 external overlay. C++20, D3D11 + ImGui, three memory backends behind one
interface.

```
muei/
├── core/        memory backends, config, logging
│   ├── IMemory.h        interface every feature reads through
│   ├── ProcUser.*       ReadProcessMemory backend
│   ├── ProcKernel.*     AimStar-protocol driver backend
│   ├── ProcDMA.*        MemProcFS backend (vmm.dll, loaded at runtime)
│   ├── Config.*         JSON persistence
│   └── Log.*            single-file log sink
├── sdk/
│   ├── Offsets.h/.cpp   offset table + offsets.cfg override loader
│   ├── Math.h           vectors, angles, w2s, matrix repair
│   ├── Entity.h         Player snapshot, skeleton pairs
│   ├── Nav.h/.cpp       entity list traversal, bone chain, names
│   └── Frame.h          one snapshot per rendered frame
├── features/    Input, Resolver, Aimbot, Triggerbot, Bhop, Glow, RCS,
│                SkinChanger, Misc, ESP
├── ui/          Theme, Overlay (D3D11), Menu (sidebar)
├── imgui/       vendored 1.91.9b + win32/dx11 backends
├── vendor/      nlohmann/json 3.11.3
├── main.cpp     orchestration only
├── verify.cpp   live offset verifier
├── build.bat    MSVC -> muei.exe with embedded manifest
├── build_mingw.bat
└── app.manifest requireAdministrator
```

## Build

```
build.bat
```

Produces `muei.exe` with `app.manifest` embedded (`requireAdministrator`). CS2
runs at High integrity — without the manifest every `PROCESS_VM_READ` is denied
and the log reads `OpenProcess(VM_READ) denied (error 5)`.

`build_mingw.bat` builds the same sources with MinGW-w64. That path has no
manifest embed, so run it from an elevated shell.

## Run

1. Start CS2 in **Fullscreen Windowed**.
2. Run `muei.exe` as administrator (the manifest makes this automatic).
3. `INSERT` opens the menu, `END` quits.

First run writes `muei.cfg` (settings) and `muei_log.txt` (diagnostics) beside
the exe. Both are rewritten every run.

If the exe directory is not writable (read-only install, protected folder, run
straight from a zip), the logger falls back to `%TEMP%` and then the working
directory, and reports the first failure through `logx::initError()`. It does
not lose the diagnostics exactly when they matter.

While no game is attached the backend probe backs off — 2 s, 4 s, 8 s, capped at
30 s — so an idle overlay costs nothing and does not fill the log with the same
four lines.

## Memory backends

`Config → Backend` picks one, or `Auto` walks the list:

| Backend | Mechanism | Notes |
|---|---|---|
| `kernel` | AimStar-protocol driver | registry-callback channel, `HKLM\SOFTWARE\Microsoft\Clipboard`. Also supplies driver-level mouse input. |
| `user` | `OpenProcess` + RPM/WPM | needs elevation to match the game |
| `dma` | MemProcFS `vmm.dll` | loaded with `LoadLibraryW`, no link-time dependency |

`Auto` tries kernel → user → dma. `ProcUser` is the fallback that always works
when the integrity levels match. Each backend reports its own failure string, so
the log says *which* access was refused instead of a bare `attached=0`.

## Offsets

`client.dll` RVAs and schema fields live in one mutable table, `g_off`.

Every field can be overridden at runtime — copy `offsets.cfg.example` to
`offsets.cfg` beside the exe and edit. Decimal or `0x` prefixed strings both
parse. A partial file overrides only what it names. This means a game update is
a text edit, not a rebuild.

### Where the numbers come from

Two sources, and they are kept separate on purpose:

- **Module-relative RVAs** — from this project's own cs2-dumper pass against
  **build 14195**.
- **Schema field offsets** — from a public cs2-dumper schema dump, adopted only
  after cross-checking ten fields against the local build's own dump. All ten
  matched exactly (`m_iHealth`, `m_iMaxHealth`, `m_pGameSceneNode`, `m_iTeamNum`,
  `m_fFlags`, `m_lifeState`, `m_modelState`, `m_iShotsFired`, `m_angEyeAngles`,
  `m_iIDEntIndex`), which proves the schema generation is the same and makes
  every field offset from that dump sound. Its *RVAs* were deliberately **not**
  adopted: they differ by 128–160 bytes, i.e. a different build.

Everything the features use is therefore either from our own dump or from a
schema-identical dump. Nothing is guessed.

### The features that used to be gated

Recoil control, the resolver lead and the skin changer all needed offsets this
project did not have. They are now populated:

| Feature | Chain |
|---|---|
| Recoil | `pawn + m_pCameraServices → + m_vecCsViewPunchAngle` |
| Resolver | `pawn + m_vecAbsVelocity` |
| Skin changer | `pawn + m_pWeaponServices → m_hMyWeapons[] → weapon + m_pAttributeManager → + m_Item → m_iItemDefinitionIndex` |

The gates stay in the code. If a future update moves one of these, the field goes
back to 0 in `offsets.cfg`, the feature disables itself and says which field it
is missing — it never writes through an unverified offset.

### Verifier

```
verify.bat
```

Console program, run elevated while CS2 is in a map. Attaches, resolves every
offset, proves each one against live memory, and prints a PASS / FAIL / SKIP
table. For the skin chain it reports whether the item view resolved inline or
through a pointer, and whether the item had been initialised yet. Exit code 0
means everything checked out. Run it after every game update.


## Features

| Feature | Mechanism |
|---|---|
| ESP | box from projected head/foot bones, health bar, skeleton, snaplines, distance |
| Aimbot | angular FOV cone, relative-mouse or angle-write, auto fire, optional driver input |
| Resolver | leads the target by bullet travel time + latency, clamped to the player volume |
| Triggerbot | reads the engine's own crosshair entity handle, optional head check |
| RCS | cancels the view punch delta through sensitivity |
| Bhop | holds jump across the airborne phase, optional strafe assist |
| Glow | per-entity glow property write with full restore on disable/exit |
| SkinChanger | fallback paint kit + item metadata on owned weapon entities |
| Misc | flash suppression |

Render: the overlay inherits the game's `HWND` when one exists (no z-order war,
nothing to flash). Only when no game window is found does it create its own
layered, colour-keyed window, and in that case it uses the bitblt swap model —
the flip model presents through DWM, which ignores `LWA_COLORKEY` and paints the
key colour as an opaque rectangle.

## Interface

The menu is drawn, not composed. There are no ImGui widgets in it except the
custom-name text field and the colour pickers, because those need real input
handling; everything else is `ImDrawList` primitives plus `InvisibleButton` hit
testing.

The shell follows the NeverLose layout:

- 758×558 rounded window, 14 px radius, near-black surface with a soft drop shadow
- 168 px sidebar: brand block (無 mark + wordmark + game subtitle), captioned nav
  groups (`COMBAT`, `DISPLAY`, `UTILITY`), accent bar on the active row, live
  backend/pid footer
- 56 px toolbar: attach pill with a state dot, player count, build tag
- content grid of rounded cards, each with a caption above it and 37 px rows
  separated by hairline dividers
- three custom controls: pill toggle with a sliding knob, slim slider with a
  value pill, and a popup select

Every animated value runs through `Motion()`, an exponential ease kept in the
window state storage keyed by the widget ID: hover glow, knob travel, slider
fill, page transitions and the select popup all settle rather than snap. Page
changes fade and slide the content vertices.

The reference for the visual language was the community NeverLose shell
(`Fdiue/neverlose-last`) — layout, row rhythm, motion feel and control shapes.
The implementation here is independent: different widget set, different state
model, no icon font (the four nav glyphs are drawn as vector shapes), and popup
selection goes through a slot table so a popup can never hold a pointer into a
dead stack frame.

![menu](docs/menu.png)

The Visuals page carries a live preview board: it draws the same box, health bar,
label and FOV ring the ESP would use, in the colours currently selected, so the
palette can be tuned without entering a match.

![skins](docs/menu_skins.png)

## Verification

What was actually run, not assumed:

- **All 26 translation units compile** — MSVC script (`build.bat`) and MinGW-w64
  16.2 (`build_mingw.bat`), zero warnings at `-Wall -Wextra`.
- **The whole program links** — `muei.exe`, 4.2 MB, no unresolved symbols.
- **The boot path is proven under a debugger** — breakpoints hit in this order:
  `WinMainCRTStartup` → `__tmainCRTStartup` → `wmain` → `wWinMain` →
  `logx::init` → `CreateFileW` with the correct absolute log path →
  `ImGui::NewFrame` → `ImGui::Render` from `overlay::Instance::end`.
- **Five consecutive frames render** with the custom-drawn menu, verified under
  gdb with the process surviving every one.
- **The menu was captured off the composited desktop** and inspected: shell,
  sidebar, cards, toggles, sliders, selects and colour swatches all render.
- **`verify.exe` builds and runs** — reports and exits non-zero with no game.

Not verified on this machine, because there is no CS2 install here:

- live offset validation (that is what `verify.bat` is for — run it on the game box)
- ESP/aimbot/glow behaviour against a real match

### Two bugs the on-machine run caught

1. **The MinGW build was not self-contained.** It linked `libstdc++-6.dll` and
   `libgcc_s_seh-1.dll` dynamically, so launched from Explorer it died with a
   modal "libstdc++-6.dll was not found" dialog before reaching `wWinMain` — no
   window, no log, process apparently alive. `build_mingw.bat` now links
   `-static -static-libgcc -static-libstdc++` and the exe runs from anywhere.
   (The BuildTools path in `build.bat` was never affected: the MSVC runtime is
   part of Windows.)
2. **The shell was translucent over the colour key.** Any alpha below 255 over
   the magenta key blends to purple instead of being keyed out, which showed as
   a purple panel. The shell body, sidebar, toolbar and popup are now fully
   opaque, and the drop shadow is gone for the same reason.

## Status

Shipping shape: complete tree, compiles clean, links, boots, renders. The
runtime-verified offset block is the one honest gap, and it is gated so a missing
value disables a feature instead of corrupting memory.
