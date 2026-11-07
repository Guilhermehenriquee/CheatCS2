// muei 無影 - entity traversal.
//
// The entity list is a CGameEntitySystem UtlTsHash. The classic chunk walk still
// describes the same memory the hash uses:
//
//     list  = *(client + dwEntityList)
//     chunk = *(list + ENTITY_LIST_HASH + ENTITY_CHUNK_STRIDE * (idx >> 9))
//     ent   = *(chunk + ENTITY_ENTRY_STRIDE * (idx & 0x1FF))
//
// Every hop is checked against IMemory::plausible() so a mid-update list returns
// "no entity" instead of faulting or handing back a garbage pointer.
//
// All offsets come from g_off, never from literals, so offsets.cfg takes effect
// without a rebuild.
#pragma once

#include <cstdint>
#include <vector>

#include "../core/IMemory.h"
#include "Entity.h"
#include "Math.h"
#include "Offsets.h"

class Nav {
public:
    // Non-owning: the backend outlives Nav and may be swapped by the loader.
    explicit Nav(IMemory& mem) : mem_(mem) {}

    // Re-resolve the client.dll base. Call after attach and after a restart.
    void refresh();
    void invalidate() { client_ = 0; }

    uintptr_t client() const { return client_; }

    // Address of a client-relative field, or 0 when the module is not loaded.
    uintptr_t at(uintptr_t rva) const {
        return (client_ && rva) ? client_ + rva : 0;
    }

    // --------------------------------------------------------- primitives ---
    uintptr_t entityList() const;
    uintptr_t entity(int index) const;
    uintptr_t entityFromHandle(uint32_t handle) const;

    uintptr_t localPawn() const;
    uintptr_t localController() const;

    QAngle viewAngles() const;
    bool   viewMatrix(float out[16]) const;

    // ----------------------------------------------------------- geometry ---
    bool readOrigin(uintptr_t ent, Vec3& out) const;
    bool readVelocity(uintptr_t ent, Vec3& out) const;
    bool isDormant(uintptr_t ent) const;

    uintptr_t boneArray(uintptr_t ent) const;
    bool bone(uintptr_t ent, int index, const Vec3& origin, Vec3& out) const;

    // ------------------------------------------------------- player record ---
    bool readPlayer(uintptr_t ent, Player& out, bool wantName) const;
    bool readName(uintptr_t controller, char* dst, size_t cap) const;
    bool spottedByLocal(uintptr_t ent, int index) const;

    // Collect every live player pawn. The local pawn is included; callers filter.
    void scanPlayers(std::vector<Player>& out, bool wantName) const;

    IMemory& mem() const { return mem_; }

private:
    IMemory&  mem_;
    uintptr_t client_ = 0;
};
