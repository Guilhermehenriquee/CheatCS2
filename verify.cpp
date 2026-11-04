// verify.cpp - live offset verifier for cs2ext.
//
// Standalone console probe. Attaches to cs2.exe, walks the tree the overlay
// walks, and prints the result of every read. Run it with the game at the main
// menu or in a bot match - it proves the offsets in offsets.h resolve to sane
// data instead of trusting a signature scan.
//
// Exit code 0 = every critical offset resolved, non-zero = something is stale.

#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>

#include "proc.h"
#include "offsets.h"
#include "math.h"
#include "nav.h"

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, bool ok, const char* detail = "") {
    if (ok) {
        ++g_pass;
        printf("  [ ok ] %-34s %s\n", what, detail);
    } else {
        ++g_fail;
        printf("  [FAIL] %-34s %s\n", what, detail);
    }
}

static void checkPtr(const char* what, uintptr_t p) {
    char buf[64];
    sprintf_s(buf, "%p", (void*)p);
    check(what, p != 0, buf);
}

int main() {
    Proc p;
    if (!p.attach(L"cs2.exe")) {
        printf("cs2.exe not running or not readable (error %lu)\n", GetLastError());
        return 2;
    }
    printf("cs2.exe  pid=%lu  image=%p\n", p.pid, (void*)p.exeBase);

    ModInfo client = p.base_of(L"client.dll");
    printf("client.dll base=%p size=0x%zX\n", (void*)client.base, client.size);
    if (!client.valid()) {
        printf("client.dll not loaded\n");
        return 3;
    }

    Nav nav(p);
    nav.refresh();

    printf("\n-- module offsets --\n");
    checkPtr("dwEntityList", nav.entityList());
    checkPtr("dwLocalPlayerPawn", nav.localPawn());

    float vm[16]{};
    bool haveVm = nav.viewMatrix(vm);
    check("dwViewMatrix", haveVm);
    if (haveVm) {
        printf("         m[0]=%.3f m[5]=%.3f m[15]=%.3f\n", vm[0], vm[5], vm[15]);
    }

    QAngle va = nav.viewAngles();
    char vabuf[96];
    sprintf_s(vabuf, "pitch=%.2f yaw=%.2f", va.pitch, va.yaw);
    check("dwViewAngles", va.finite(), vabuf);

    printf("\n-- local player --\n");
    uintptr_t lp = nav.localPawn();
    checkPtr("dwLocalPlayerPawn", lp);
    if (!lp) {
        printf("\nNot in a match - connect to a bot match and rerun.\n");
        return 1;
    }

    Player local;
    bool haveLocal = nav.readPlayer(lp, local, true);
    char lbuf[160];
    sprintf_s(lbuf, "hp=%d team=%d life=%d flags=0x%X",
              local.health, local.team, (int)local.lifeState, local.flags);
    check("local readPlayer", haveLocal, lbuf);
    if (haveLocal) {
        char obuf[128];
        sprintf_s(obuf, "%.1f %.1f %.1f", local.origin.x, local.origin.y, local.origin.z);
        check("local origin sane", local.origin.len() > 1.f && local.origin.len() < 20000.f, obuf);
        check("local team 2 or 3", local.team == 2 || local.team == 3);
    }

    printf("\n-- bone array --\n");
    uintptr_t bones = nav.boneArray(lp);
    checkPtr("m_pGameSceneNode -> m_modelState -> bones", bones);
    if (bones) {
        Vec3 head;
        bool okHead = nav.bone(lp, Bone::HEAD, local.origin, head);
        char hbuf[160];
        sprintf_s(hbuf, "head=%.1f %.1f %.1f", head.x, head.y, head.z);
        check("head bone (idx 6)", okHead, hbuf);
        if (okHead) {
            float dz = head.z - local.origin.z;
            char dbuf[96];
            sprintf_s(dbuf, "dz=%.1f (expect ~60-80)", dz);
            check("head above origin", dz > 40.f && dz < 100.f, dbuf);
        }
        Vec3 foot;
        bool okFoot = nav.bone(lp, Bone::R_FOOT, local.origin, foot);
        check("right foot bone (idx 21)", okFoot);
    }

    printf("\n-- entities --\n");
    std::vector<Player> players;
    nav.scanPlayers(players, true, local.team);
    printf("  %zu player records\n", players.size());
    for (const Player& pl : players) {
        if (pl.pawn == lp) continue;
        printf("  idx %2d  %-20s hp=%3d team=%d spotted=%d\n",
               pl.index, pl.name[0] ? pl.name : "(noname)",
               pl.health, pl.team, (int)pl.spotted);
    }

    int enemies = 0, teammates = 0;
    for (const Player& pl : players) {
        if (pl.pawn == lp) continue;
        if (pl.team == local.team) ++teammates; else ++enemies;
    }
    check("at least one other player", players.size() > 1);

    printf("\n-- crosshair handle --\n");
    uint32_t idEnt = p.rd<uint32_t>(lp + Off::m_iIDEntIndex);
    char cbuf[128];
    sprintf_s(cbuf, "handle=0x%X index=%u", idEnt, idEnt & 0x7FFF);
    uintptr_t cross = nav.entityFromHandle(idEnt);
    sprintf_s(cbuf + strlen(cbuf), sizeof(cbuf) - strlen(cbuf), " ent=%p", (void*)cross);
    check("m_iIDEntIndex resolves", idEnt == 0 || cross != 0, cbuf);

    printf("\n-- glow --\n");
    int glowType = p.rd<int32_t>(lp + Off::m_Glow + Off::m_iGlowType);
    char gbuf[96];
    sprintf_s(gbuf, "local m_iGlowType=%d", glowType);
    check("m_Glow readable", glowType >= 0 && glowType < 100, gbuf);

    printf("\n-- write handle --\n");
    bool canWrite = p.ensureWrite();
    check("PROCESS_VM_WRITE", canWrite,
          canWrite ? "" : "run as administrator if elevated game");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
