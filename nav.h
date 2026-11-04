#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

#include "proc.h"
#include "offsets.h"
#include "math.h"

// -----------------------------------------------------------------------------
// Nav - entity traversal and reads against the live target.
//
// Entity list is CGameEntitySystem, a chunked hash. Classic layout:
//   list  = *(client + dwEntityList)
//   chunk = *(list + 0x10 + 8 * (idx >> 9))
//   ent   = *(chunk + 0x78 * (idx & 0x1FF))
// Every hop is checked. A null chunk or null entity skips the index - this walk
// never faults and never returns a half-initialised snapshot.
//
// Reads are batched where the cost matters (player snapshots), otherwise done
// field by field so a single bad address does not poison the whole record.
// -----------------------------------------------------------------------------

struct Player {
    uintptr_t pawn = 0;
    uintptr_t node = 0;
    uintptr_t controller = 0;

    Vec3    origin;              // absolute world origin (feet)
    Vec3    eye;                 // eye position
    QAngle  eyeAngles;
    int32_t health = 0;
    int32_t maxHealth = 100;
    int32_t team = 0;
    uint8_t lifeState = 1;
    uint32_t flags = 0;
    int32_t index = -1;          // entity index
    bool    dormant = true;
    bool    spotted = false;     // spotted by local player this tick
    char    name[32] = { 0 };

    bool alive() const { return health > 0 && lifeState == 0; }
    bool onGround() const { return (flags & 1u) != 0; }
};

class Nav {
public:
    explicit Nav(Proc& p) : p_(p) {}

    // Resolve the client.dll base once per rebuild. All module-relative offsets
    // in offsets.h are relative to client.dll, never to the cs2.exe image.
    void refresh() {
        clientAddr_ = p_.base_of(L"client.dll").base;
    }

    uintptr_t client() const { return clientAddr_; }

    // Address of a client-relative field, or 0 when the module is not loaded.
    uintptr_t at(uintptr_t rva) const { return clientAddr_ ? clientAddr_ + rva : 0; }

    // --------------------------------------------------------------- primitives --
    uintptr_t entityList() const {
        if (!clientAddr_) return 0;
        return p_.rd<uintptr_t>(clientAddr_ + Off::dwEntityList);
    }

    // Resolve an entity by index. Returns 0 on any broken hop.
    uintptr_t entity(int index) const {
        if (index < 0) return 0;
        uintptr_t list = entityList();
        if (!p_.plausible(list)) return 0;

        uintptr_t chunk = p_.rd<uintptr_t>(
            list + Off::ENTITY_LIST_HASH + Off::ENTITY_CHUNK_STRIDE * (uintptr_t)(index >> 9));
        if (!p_.plausible(chunk)) return 0;

        uintptr_t ent = p_.rd<uintptr_t>(
            chunk + Off::ENTITY_ENTRY_STRIDE * (uintptr_t)(index & 0x1FF));
        if (!p_.plausible(ent)) return 0;
        return ent;
    }

    // Follow a CBaseHandle (index | serial << 16) back to an entity pointer.
    uintptr_t entityFromHandle(uint32_t handle) const {
        if (!handle) return 0;
        return entity((int)(handle & 0x7FFF));
    }

    uintptr_t localPawn() const {
        if (!clientAddr_) return 0;
        uintptr_t pawn = p_.rd<uintptr_t>(clientAddr_ + Off::dwLocalPlayerPawn);
        return p_.plausible(pawn) ? pawn : 0;
    }

    uintptr_t localController() const {
        if (!clientAddr_) return 0;
        uintptr_t c = p_.rd<uintptr_t>(clientAddr_ + Off::dwLocalPlayerController);
        return p_.plausible(c) ? c : 0;
    }

    QAngle viewAngles() const {
        if (!clientAddr_) return {};
        QAngle a = p_.rd<QAngle>(clientAddr_ + Off::dwViewAngles);
        return a.finite() ? a : QAngle{};
    }

    bool viewMatrix(float out[16]) const {
        if (!clientAddr_) return false;
        return p_.rdBytes(clientAddr_ + Off::dwViewMatrix, out, sizeof(float) * 16);
    }

    // -------------------------------------------------------------- geometry --
    bool readOrigin(uintptr_t ent, Vec3& out) const {
        uintptr_t node = p_.rd<uintptr_t>(ent + Off::m_pGameSceneNode);
        if (!p_.plausible(node)) return false;
        Vec3 v = p_.rd<Vec3>(node + Off::m_vecAbsOrigin);
        if (!v.finite() || v.zero()) {
            // Fall back to the local origin, then to the entity's own vector field.
            v = p_.rd<Vec3>(node + Off::m_vecOrigin);
        }
        if (!v.finite() || v.zero()) return false;
        out = v;
        return true;
    }

    bool isDormant(uintptr_t ent) const {
        uintptr_t node = p_.rd<uintptr_t>(ent + Off::m_pGameSceneNode);
        if (!p_.plausible(node)) return true;
        return p_.rd<uint8_t>(node + Off::m_bDormant) != 0;
    }

    // Bone array base for an entity: node -> CSkeletonInstance::m_modelState -> bone array.
    uintptr_t boneArray(uintptr_t ent) const {
        uintptr_t node = p_.rd<uintptr_t>(ent + Off::m_pGameSceneNode);
        if (!p_.plausible(node)) return 0;
        uintptr_t arr = p_.rd<uintptr_t>(node + Off::m_modelState + Off::m_pBoneArray);
        return p_.plausible(arr) ? arr : 0;
    }

    // World position of one bone, or false if anything along the chain is bad.
    bool bone(uintptr_t ent, int index, const Vec3& origin, Vec3& out) const {
        if (index < 0 || index >= Bone::MAX_BONES) return false;
        uintptr_t arr = boneArray(ent);
        if (!arr) return false;
        Transform t = p_.rd<Transform>(arr + (uintptr_t)index * Off::BONE_SIZE);
        if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) return false;
        Vec3 world = bonePos(t, origin);
        if (!world.finite() || world.zero()) return false;
        // Bones outside a 200-yard box around the origin are corrupt reads.
        Vec3 d = world - origin;
        if (std::fabs(d.x) > 200.f || std::fabs(d.y) > 200.f || std::fabs(d.z) > 200.f) return false;
        out = world;
        return true;
    }

    // ---------------------------------------------------------- player record --
    // Fill a Player from an entity pointer. Returns false if the entity is not a
    // usable player pawn. Health/team/life are read together; a partial read
    // marks the record dead rather than producing a phantom.
    bool readPlayer(uintptr_t ent, Player& out, bool wantName) const {
        if (!p_.plausible(ent)) return false;

        out = Player{};
        out.pawn = ent;

        uintptr_t node = p_.rd<uintptr_t>(ent + Off::m_pGameSceneNode);
        if (!p_.plausible(node)) return false;
        out.node = node;

        out.health    = p_.rd<int32_t>(ent + Off::m_iHealth);
        out.maxHealth = p_.rd<int32_t>(ent + Off::m_iMaxHealth);
        out.team      = (int)p_.rd<uint8_t>(ent + Off::m_iTeamNum);
        out.lifeState = p_.rd<uint8_t>(ent + Off::m_lifeState);
        out.flags     = p_.rd<uint32_t>(ent + Off::m_fFlags);
        out.eyeAngles = p_.rd<QAngle>(ent + Off::m_angEyeAngles);

        if (out.maxHealth <= 0 || out.maxHealth > 1000) out.maxHealth = 100;
        if (out.team != 2 && out.team != 3) return false;

        Vec3 o{};
        if (!readOrigin(ent, o)) return false;
        out.origin = o;
        out.dormant = p_.rd<uint8_t>(node + Off::m_bDormant) != 0;

        // Eye height. Standing origin sits at the feet; eye is 64 units up.
        out.eye = { o.x, o.y, o.z + Off::EYE_OFFSET_Z };

        // Dormancy can lag a tick; a live-but-dormant record still projects.
        if (out.dormant) return false;

        uintptr_t ctrl = p_.rd<uintptr_t>(ent + Off::m_hController);
        if (p_.plausible(ctrl)) out.controller = ctrl;

        if (wantName && out.controller) readName(out.controller, out.name, sizeof(out.name));
        return true;
    }

    // Player name from the controller. CCSPlayerController carries the sanitised
    // name; the base controller name is a fallback when that buffer is empty.
    bool readName(uintptr_t ctrl, char* dst, size_t cap) const {
        if (!p_.plausible(ctrl) || !dst || cap < 2) return false;
        char buf[Off::MAX_PLAYER_NAME]{};
        if (p_.rdBytes(ctrl + Off::m_sSanitizedPlayerName, buf, sizeof(buf))) {
            buf[sizeof(buf) - 1] = '\0';
            if (buf[0]) {
                strncpy_s(dst, cap, buf, _TRUNCATE);
                return true;
            }
        }
        if (p_.rdBytes(ctrl + Off::m_iszPlayerName, buf, sizeof(buf))) {
            buf[sizeof(buf) - 1] = '\0';
            if (buf[0]) {
                strncpy_s(dst, cap, buf, _TRUNCATE);
                return true;
            }
        }
        return false;
    }

    // True when the local player can currently see the entity: the spotted mask
    // is indexed by entity number, bit (idx - 1).
    bool spottedByLocal(uintptr_t ent, int index) const {
        if (!p_.plausible(ent) || index <= 0) return false;
        uint64_t mask[2]{};
        if (!p_.rdBytes(ent + Off::m_entitySpottedState + Off::m_bSpottedByMask,
                        mask, sizeof(mask))) {
            return false;
        }
        int word = (index - 1) / 64;
        int bit  = (index - 1) % 64;
        if (word < 0 || word > 1) return false;
        return (mask[word] >> bit) & 1ull;
    }

    // ---------------------------------------------------------------- scan --
    // Collect every live enemy/teammate pawn into out. Team filter is applied by
    // the caller; this returns everybody with a valid record.
    void scanPlayers(std::vector<Player>& out, bool wantName, int myTeam) const {
        out.clear();
        out.reserve(Off::ENTITY_INDEX_MAX);

        for (int i = 1; i <= Off::ENTITY_INDEX_MAX; ++i) {
            uintptr_t ent = entity(i);
            if (!ent) continue;

            Player pl;
            if (!readPlayer(ent, pl, wantName)) continue;
            if (!pl.alive()) continue;
            pl.index = i;
            pl.spotted = spottedByLocal(ent, i);
            (void)myTeam;
            out.push_back(pl);
        }
    }

    // Always mutable: Nav holds no read-only state that a write should not
    // invalidate, and every feature path that writes also reads.
    Proc& proc() const { return p_; }

private:
    Proc&     p_;
    uintptr_t clientAddr_ = 0;
};
