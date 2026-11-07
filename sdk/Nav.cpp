#include "Nav.h"

#include "../core/Log.h"

void Nav::refresh() {
    ModInfo mi = mem_.getModule(L"client.dll");
    client_ = mi.base;
    if (client_) {
        logx::line("Nav: client.dll base=%p size=0x%zX build=%s",
                   (void*)mi.base, mi.size, g_off.build);
    } else {
        logx::line("Nav: client.dll not resolved yet");
    }
}

uintptr_t Nav::entityList() const {
    if (!client_) return 0;
    return mem_.rd<uintptr_t>(client_ + g_off.dwEntityList);
}

uintptr_t Nav::entity(int index) const {
    if (index <= 0 || index > g_off.ENTITY_INDEX_MAX) return 0;

    uintptr_t list = entityList();
    if (!IMemory::plausible(list)) return 0;

    uintptr_t chunk = mem_.rd<uintptr_t>(
        list + g_off.ENTITY_LIST_HASH +
        g_off.ENTITY_CHUNK_STRIDE * (uintptr_t)(index >> 9));
    if (!IMemory::plausible(chunk)) return 0;

    uintptr_t ent = mem_.rd<uintptr_t>(
        chunk + g_off.ENTITY_ENTRY_STRIDE * (uintptr_t)(index & 0x1FF));
    if (!IMemory::plausible(ent)) return 0;
    return ent;
}

uintptr_t Nav::entityFromHandle(uint32_t handle) const {
    if (!handle) return 0;
    return entity((int)(handle & 0x7FFF));
}

uintptr_t Nav::localPawn() const {
    if (!client_) return 0;
    uintptr_t p = mem_.rd<uintptr_t>(client_ + g_off.dwLocalPlayerPawn);
    return IMemory::plausible(p) ? p : 0;
}

uintptr_t Nav::localController() const {
    if (!client_) return 0;
    uintptr_t c = mem_.rd<uintptr_t>(client_ + g_off.dwLocalPlayerController);
    return IMemory::plausible(c) ? c : 0;
}

QAngle Nav::viewAngles() const {
    if (!client_) return {};
    QAngle a = mem_.rd<QAngle>(client_ + g_off.dwViewAngles);
    return a.finite() ? a : QAngle{};
}

bool Nav::viewMatrix(float out[16]) const {
    if (!client_) return false;
    return mem_.rdBytes(client_ + g_off.dwViewMatrix, out, sizeof(float) * 16);
}

bool Nav::readOrigin(uintptr_t ent, Vec3& out) const {
    uintptr_t node = mem_.rd<uintptr_t>(ent + g_off.m_pGameSceneNode);
    if (!IMemory::plausible(node)) return false;

    Vec3 v = mem_.rd<Vec3>(node + g_off.m_vecAbsOrigin);
    if (!v.finite() || v.zero()) {
        v = mem_.rd<Vec3>(node + g_off.m_vecOrigin);
    }
    if (!v.finite() || v.zero()) return false;
    out = v;
    return true;
}

bool Nav::readVelocity(uintptr_t ent, Vec3& out) const {
    if (!offKnown(g_off.m_vecAbsVelocity)) return false;
    Vec3 v = mem_.rd<Vec3>(ent + g_off.m_vecAbsVelocity);
    if (!v.finite()) return false;
    // A velocity above 4000 units/s is a bad read, not a bunny hop.
    if (v.len() > 4000.f) return false;
    out = v;
    return true;
}

bool Nav::isDormant(uintptr_t ent) const {
    uintptr_t node = mem_.rd<uintptr_t>(ent + g_off.m_pGameSceneNode);
    if (!IMemory::plausible(node)) return true;
    return mem_.rd<uint8_t>(node + g_off.m_bDormant) != 0;
}

uintptr_t Nav::boneArray(uintptr_t ent) const {
    uintptr_t node = mem_.rd<uintptr_t>(ent + g_off.m_pGameSceneNode);
    if (!IMemory::plausible(node)) return 0;

    // m_pBoneArray is a pointer inside CModelState, which is inline in the
    // skeleton instance.
    uintptr_t arr = mem_.rd<uintptr_t>(node + g_off.m_modelState + g_off.m_pBoneArray);
    if (!IMemory::plausible(arr)) {
        // Fall back to treating it as an inline array.
        arr = node + g_off.m_modelState + g_off.m_pBoneArray;
    }
    return arr;
}

bool Nav::bone(uintptr_t ent, int index, const Vec3& origin, Vec3& out) const {
    if (index < 0 || index >= Bone::MAX_BONES) return false;
    uintptr_t arr = boneArray(ent);
    if (!arr) return false;

    Transform t = mem_.rd<Transform>(arr + (uintptr_t)index * g_off.BONE_SIZE);
    if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) return false;
    if (!std::isfinite(t.qw)) return false;

    Vec3 world = bonePos(t, origin);
    if (!world.finite() || world.zero()) return false;

    // A bone more than 200 units from the skeleton origin is a corrupt read.
    Vec3 d = world - origin;
    if (std::fabs(d.x) > 200.f || std::fabs(d.y) > 200.f || std::fabs(d.z) > 200.f) {
        return false;
    }

    out = world;
    return true;
}

bool Nav::readName(uintptr_t controller, char* dst, size_t cap) const {
    if (!IMemory::plausible(controller) || !dst || cap < 2) return false;

    char buf[Geo::MAX_PLAYER_NAME]{};
    if (mem_.rdBytes(controller + g_off.m_sSanitizedPlayerName, buf, sizeof(buf))) {
        buf[sizeof(buf) - 1] = '\0';
        if (buf[0] >= 0x20 && buf[0] < 0x7F) {
            strncpy_s(dst, cap, buf, _TRUNCATE);
            return true;
        }
    }
    if (mem_.rdBytes(controller + g_off.m_iszPlayerName, buf, sizeof(buf))) {
        buf[sizeof(buf) - 1] = '\0';
        if (buf[0] >= 0x20 && buf[0] < 0x7F) {
            strncpy_s(dst, cap, buf, _TRUNCATE);
            return true;
        }
    }
    return false;
}

bool Nav::readPlayer(uintptr_t ent, Player& out, bool wantName) const {
    if (!IMemory::plausible(ent)) return false;

    out = Player{};
    out.pawn = ent;

    uintptr_t node = mem_.rd<uintptr_t>(ent + g_off.m_pGameSceneNode);
    if (!IMemory::plausible(node)) return false;
    out.node = node;

    out.health    = mem_.rd<int32_t>(ent + g_off.m_iHealth);
    out.maxHealth = mem_.rd<int32_t>(ent + g_off.m_iMaxHealth);
    out.team      = (int)mem_.rd<uint8_t>(ent + g_off.m_iTeamNum);
    out.lifeState = mem_.rd<uint8_t>(ent + g_off.m_lifeState);
    out.flags     = mem_.rd<uint32_t>(ent + g_off.m_fFlags);
    out.eyeAngles = mem_.rd<QAngle>(ent + g_off.m_angEyeAngles);
    out.shotsFired = mem_.rd<int32_t>(ent + g_off.m_iShotsFired);

    if (out.maxHealth <= 0 || out.maxHealth > 1000) out.maxHealth = 100;
    if (out.team != 2 && out.team != 3) return false;

    Vec3 o{};
    if (!readOrigin(ent, o)) return false;
    out.origin = o;
    out.dormant = mem_.rd<uint8_t>(node + g_off.m_bDormant) != 0;

    // Eye height. Standing origin is at the feet; crouch lowers it.
    float eyeZ = (out.flags & 2u) ? Geo::DUCK_EYE_OFFSET_Z : Geo::EYE_OFFSET_Z;
    out.eye = { o.x, o.y, o.z + eyeZ };

    Vec3 vel{};
    if (readVelocity(ent, vel)) out.velocity = vel;

    // View punch lives on the camera services in CS2, not on the pawn.
    if (offKnown(g_off.m_pCameraServices) && offKnown(g_off.m_vecCsViewPunchAngle)) {
        uintptr_t cam = mem_.rd<uintptr_t>(ent + g_off.m_pCameraServices);
        if (IMemory::plausible(cam)) {
            QAngle p = mem_.rd<QAngle>(cam + g_off.m_vecCsViewPunchAngle);
            if (p.finite() && std::fabs(p.pitch) < 90.f && std::fabs(p.yaw) <= 180.f) {
                out.aimPunch = p;
            }
        }
    }

    // A dormant entity never carries usable bone data this tick.
    if (out.dormant) return false;

    uintptr_t ctrl = mem_.rd<uintptr_t>(ent + g_off.m_hController);
    if (IMemory::plausible(ctrl)) out.controller = ctrl;

    if (wantName && out.controller) {
        readName(out.controller, out.name, sizeof(out.name));
    }
    return true;
}

bool Nav::spottedByLocal(uintptr_t ent, int index) const {
    if (!IMemory::plausible(ent) || index <= 0) return false;

    uint64_t mask[2]{};
    if (!mem_.rdBytes(ent + g_off.m_entitySpottedState + g_off.m_bSpottedByMask,
                      mask, sizeof(mask))) {
        return false;
    }

    int word = (index - 1) / 64;
    int bit  = (index - 1) % 64;
    if (word < 0 || word > 1) return false;
    return ((mask[word] >> bit) & 1ull) != 0;
}

void Nav::scanPlayers(std::vector<Player>& out, bool wantName) const {
    out.clear();
    out.reserve((size_t)g_off.ENTITY_INDEX_MAX);

    for (int i = 1; i <= g_off.ENTITY_INDEX_MAX; ++i) {
        uintptr_t ent = entity(i);
        if (!ent) continue;

        Player pl;
        if (!readPlayer(ent, pl, wantName)) continue;
        if (!pl.alive()) continue;

        pl.index   = i;
        pl.spotted = spottedByLocal(ent, i);
        out.push_back(pl);
    }
}
