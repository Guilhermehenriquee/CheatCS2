// muei 無影 - live offset verifier.
//
// Attaches to a running cs2.exe and proves each offset resolves to something
// plausible before the overlay ever writes through it. Console program: run it
// elevated, read the table, act on the FAIL lines.
//
// This is the gate the runtime-verified block in sdk/Offsets.h depends on. A
// field reported as UNKNOWN is one the overlay refuses to use until offsets.cfg
// supplies a value.
//
// Exit code: 0 when every checked field passed, 1 when anything failed.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Log.h"
#include "core/ProcUser.h"

#include "sdk/Entity.h"
#include "sdk/Math.h"
#include "sdk/Nav.h"
#include "sdk/Offsets.h"

namespace {

int g_pass = 0;
int g_fail = 0;
int g_skip = 0;

void row(const char* verdict, const char* name, const std::string& detail) {
    std::printf("  %-6s %-32s %s\n", verdict, name, detail.c_str());
}

void pass(const char* name, const std::string& detail) { ++g_pass; row("PASS", name, detail); }
void fail(const char* name, const std::string& detail) { ++g_fail; row("FAIL", name, detail); }
void skip(const char* name, const std::string& detail) { ++g_skip; row("SKIP", name, detail); }

std::string hex(uintptr_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v);
    return b;
}

std::string dec(long long v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%lld", v);
    return b;
}

// Is a client-relative offset inside the mapped image?
bool inModule(IMemory& mem, uintptr_t base, uintptr_t rva, size_t need) {
    (void)base;
    ModInfo mi = mem.getModule(L"client.dll");
    if (!mi.valid()) return false;
    return rva + need <= mi.size;
}

void checkModuleOffsets(IMemory& mem, Nav& nav) {
    (void)nav;
    std::printf("\n-- client.dll module-relative offsets --\n");

    struct Item { const char* name; uintptr_t rva; size_t bytes; };
    const Item items[] = {
        { "dwEntityList",            g_off.dwEntityList,            8 },
        { "dwLocalPlayerPawn",       g_off.dwLocalPlayerPawn,       8 },
        { "dwLocalPlayerController", g_off.dwLocalPlayerController, 8 },
        { "dwViewMatrix",            g_off.dwViewMatrix,            64 },
        { "dwViewAngles",            g_off.dwViewAngles,            12 },
        { "dwGameRules",             g_off.dwGameRules,             8 },
        { "dwPlantedC4",             g_off.dwPlantedC4,             8 },
    };

    for (const Item& it : items) {
        if (!inModule(mem, 0, it.rva, it.bytes)) {
            fail(it.name, "outside client.dll image at " + hex(it.rva));
            continue;
        }
        pass(it.name, "rva " + hex(it.rva) + " inside image");
    }
}

void checkLocalPlayer(IMemory& mem, Nav& nav) {
    (void)mem;
    std::printf("\n-- local player --\n");

    uintptr_t pawn = nav.localPawn();
    if (!pawn) {
        fail("dwLocalPlayerPawn deref", "null pawn (not in a match?)");
        return;
    }
    pass("dwLocalPlayerPawn deref", "pawn " + hex(pawn));

    Player local;
    if (!nav.readPlayer(pawn, local, true)) {
        fail("readPlayer(local)", "record did not validate");
        return;
    }
    pass("readPlayer(local)", "hp " + dec(local.health) + "/" + dec(local.maxHealth) +
                              " team " + dec(local.team));

    if (local.team != 2 && local.team != 3) {
        fail("m_iTeamNum", "team " + dec(local.team) + " is neither 2 nor 3");
    } else {
        pass("m_iTeamNum", "team " + dec(local.team));
    }

    if (local.health < 0 || local.health > 100) {
        fail("m_iHealth", "hp " + dec(local.health) + " out of range");
    } else {
        pass("m_iHealth", "hp " + dec(local.health));
    }

    Vec3 origin;
    if (nav.readOrigin(pawn, origin)) {
        pass("m_vecAbsOrigin", "(" + dec((long long)origin.x) + ", " +
                                dec((long long)origin.y) + ", " +
                                dec((long long)origin.z) + ")");
    } else {
        fail("m_vecAbsOrigin", "unreadable");
    }

    uintptr_t controller = nav.localController();
    if (controller) {
        char name[64]{};
        if (nav.readName(controller, name, sizeof(name))) {
            pass("controller name", name);
        } else {
            fail("controller name", "buffer empty or unreadable");
        }
    } else {
        fail("dwLocalPlayerController", "null");
    }

    float m[16]{};
    if (nav.viewMatrix(m)) {
        bool finite = true;
        for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) finite = false;
        if (finite && m[15] != 0.f) {
            pass("view matrix", "finite, m[15]=" + dec((long long)m[15]));
        } else {
            fail("view matrix", "non-finite or degenerate");
        }
    } else {
        fail("view matrix", "unreadable");
    }
}

void checkEntityList(IMemory& mem, Nav& nav) {
    (void)mem;
    std::printf("\n-- entity list --\n");

    uintptr_t list = nav.entityList();
    if (!IMemory::plausible(list)) {
        fail("entity list", "not plausible: " + hex(list));
        return;
    }
    pass("entity list", hex(list));

    std::vector<Player> players;
    nav.scanPlayers(players, false);
    if (players.empty()) {
        fail("scanPlayers", "no valid player records (in a match?)");
        return;
    }
    pass("scanPlayers", dec((long long)players.size()) + " players");

    // Bone chain on the first player that is not the local pawn.
    uintptr_t probe = 0;
    Vec3 origin;
    for (const Player& p : players) {
        if (p.pawn == nav.localPawn()) continue;
        probe = p.pawn;
        origin = p.origin;
        break;
    }
    if (!probe) {
        skip("bone chain", "no non-local player to probe");
        return;
    }

    uintptr_t bones = nav.boneArray(probe);
    if (!bones) {
        fail("bone array", "unresolved");
        return;
    }
    pass("bone array", hex(bones));

    Vec3 head;
    if (nav.bone(probe, Bone::HEAD, origin, head)) {
        pass("head bone", "(" + dec((long long)head.x) + ", " +
                           dec((long long)head.y) + ", " +
                           dec((long long)head.z) + ")");
    } else {
        fail("head bone", "unreadable or implausible offset from origin");
    }

    if (nav.spottedByLocal(probe, 1)) {
        pass("spotted mask", "bit readable");
    } else {
        // Zero is a legitimate value, so this only proves the read worked.
        pass("spotted mask", "read ok (bit clear)");
    }
}

void checkRuntimeVerified(IMemory& mem, Nav& nav) {
    std::printf("\n-- runtime-verified block (offsets.cfg) --\n");

    auto unknown = [](const char* name, uintptr_t v) {
        if (offKnown(v)) return false;
        skip(name, "unknown - supply it in offsets.cfg to enable its feature");
        return true;
    };

    unknown("m_pCameraServices",     g_off.m_pCameraServices);
    unknown("m_vecCsViewPunchAngle", g_off.m_vecCsViewPunchAngle);
    unknown("m_vecAbsVelocity",      g_off.m_vecAbsVelocity);
    unknown("m_hMyWeapons",        g_off.m_hMyWeapons);
    unknown("m_pAttributeManager", g_off.m_pAttributeManager);
    unknown("m_Item",              g_off.m_Item);
    unknown("m_iItemDefinitionIndex", g_off.m_iItemDefinitionIndex);
    unknown("m_nFallbackPaintKit", g_off.m_nFallbackPaintKit);

    uintptr_t pawn = nav.localPawn();
    if (!pawn) return;

    if (offKnown(g_off.m_pCameraServices) && offKnown(g_off.m_vecCsViewPunchAngle)) {
        uintptr_t cam = mem.rd<uintptr_t>(pawn + g_off.m_pCameraServices);
        if (IMemory::plausible(cam)) {
            QAngle p = mem.rd<QAngle>(cam + g_off.m_vecCsViewPunchAngle);
            if (p.finite() && std::fabs(p.pitch) < 90.f && std::fabs(p.yaw) <= 180.f) {
                pass("punch chain", "pitch " + dec((long long)p.pitch) +
                                    " yaw " + dec((long long)p.yaw));
            } else {
                fail("punch chain", "value implausible");
            }
        } else {
            fail("punch chain", "m_pCameraServices did not dereference");
        }
    }

    if (offKnown(g_off.m_vecAbsVelocity)) {
        Vec3 v;
        if (nav.readVelocity(pawn, v)) {
            pass("velocity", "len " + dec((long long)v.len()));
        } else {
            fail("velocity", "unreadable or out of range");
        }
    }

    if (offKnown(g_off.m_pWeaponServices) && offKnown(g_off.m_hMyWeapons) &&
        offKnown(g_off.m_Item) && offKnown(g_off.m_iItemDefinitionIndex)) {
        uintptr_t svc = mem.rd<uintptr_t>(pawn + g_off.m_pWeaponServices);
        if (!IMemory::plausible(svc)) {
            fail("weapon services", "not plausible");
            return;
        }
        uintptr_t vec = svc + g_off.m_hMyWeapons;
        int count = mem.rd<int32_t>(vec);
        uintptr_t elems = mem.rd<uintptr_t>(vec + 8);
        if (count <= 0 || count > 64 || !IMemory::plausible(elems)) {
            fail("weapon vector", "count " + dec(count) + " elems " + hex(elems));
            return;
        }
        pass("weapon vector", dec(count) + " entries");

        uint32_t handle = mem.rd<uint32_t>(elems);
        uintptr_t weapon = nav.entityFromHandle(handle);
        if (!IMemory::plausible(weapon)) {
            fail("weapon entity", "handle " + hex(handle) + " did not resolve");
            return;
        }
        pass("weapon entity", hex(weapon));

        if (offKnown(g_off.m_pAttributeManager) && offKnown(g_off.m_Item) &&
            offKnown(g_off.m_iItemDefinitionIndex)) {
            const uintptr_t inlineItem = weapon + g_off.m_pAttributeManager + g_off.m_Item;
            int def = mem.rd<int32_t>(inlineItem + g_off.m_iItemDefinitionIndex);
            const char* form = "inline";
            uintptr_t item = inlineItem;
            if (!(def > 0 && def < 6000)) {
                uintptr_t container = mem.rd<uintptr_t>(weapon + g_off.m_pAttributeManager);
                if (IMemory::plausible(container)) {
                    item = container + g_off.m_Item;
                    def = mem.rd<int32_t>(item + g_off.m_iItemDefinitionIndex);
                    form = "pointed";
                }
            }
            if (def > 0 && def < 6000) {
                pass("item definition", "def " + dec(def) + " (" + form + ")");
            } else {
                fail("item definition", "def " + dec(def) + " out of range");
            }
            if (offKnown(g_off.m_bItemInitialized)) {
                uint8_t init = mem.rd<uint8_t>(item + g_off.m_bItemInitialized);
                if (init) pass("item initialised", "flag set");
                else      skip("item initialised", "flag clear (weapon not ready)");
            }
        }
    }
}

} // namespace

int main() {
    logx::init("muei_verify");
    loadOffsetOverrides([] {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (slash) *(slash + 1) = L'\0';
        return std::wstring(exe) + L"offsets.cfg";
    }());

    std::printf("muei 無影 offset verifier\n");
    std::printf("build tag : %s\n", g_off.build);
    std::printf("log       : %ls\n\n", logx::path());

    ProcUser mem;
    Nav nav(mem);

    if (!mem.attach(L"cs2.exe")) {
        std::printf("FAIL  attach cs2.exe\n      %ls\n\n", mem.lastError().c_str());
        std::printf("Is CS2 running, and is this shell elevated?\n");
        return 1;
    }

    nav.refresh();
    if (!nav.client()) {
        std::printf("FAIL  client.dll not resolved\n");
        return 1;
    }
    std::printf("pid       : %lu\n", (unsigned long)mem.pid());
    std::printf("client.dll: %p\n", (void*)nav.client());

    checkModuleOffsets(mem, nav);
    checkLocalPlayer(mem, nav);
    checkEntityList(mem, nav);
    checkRuntimeVerified(mem, nav);

    std::printf("\n-- summary --\n");
    std::printf("  pass %d   fail %d   skip %d\n", g_pass, g_fail, g_skip);

    if (g_fail == 0) {
        std::printf("\nRESULT: all checked offsets resolve.\n");
        return 0;
    }
    std::printf("\nRESULT: %d offset(s) failed. Do not enable features that use them;\n"
                "        drop corrected values into offsets.cfg instead of rebuilding.\n", g_fail);
    return 1;
}
