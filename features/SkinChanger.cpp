#include "SkinChanger.h"

#include "../core/Log.h"

#include <cstdio>
#include <vector>

namespace {

struct Req {
    const char* name;
    uintptr_t   value;
};

std::vector<Req> requirements() {
    return {
        { "m_pWeaponServices",    g_off.m_pWeaponServices },
        { "m_hMyWeapons",         g_off.m_hMyWeapons },
        { "m_pAttributeManager",  g_off.m_pAttributeManager },
        { "m_Item",               g_off.m_Item },
        { "m_iItemDefinitionIndex", g_off.m_iItemDefinitionIndex },
        { "m_nFallbackPaintKit",  g_off.m_nFallbackPaintKit },
    };
}

// The EconItemView written below is a real object only for actual weapon
// entities; a def index outside the shipped range means the chain landed
// somewhere else and the write must not happen.
bool plausibleDefinition(int32_t def) {
    return def > 0 && def < 6000;
}

} // namespace

bool SkinChanger::available() {
    for (const Req& r : requirements()) {
        if (!offKnown(r.value)) return false;
    }
    return true;
}

std::string SkinChanger::missing() {
    std::string out;
    for (const Req& r : requirements()) {
        if (offKnown(r.value)) continue;
        if (!out.empty()) out += ", ";
        out += r.name;
    }
    return out;
}

void SkinChanger::applyToWeapon(IMemory& mem, uintptr_t weapon, const Config& cfg) {
    // C_EconEntity::m_AttributeManager is an INLINE C_AttributeContainer in this
    // schema, so the item view sits at weapon + m_pAttributeManager + m_Item.
    // Older layouts had a pointer there, so both are probed and the one whose
    // EconItemView carries a plausible definition index wins. Guessing silently
    // is what writes a paint kit into the wrong object.
    uintptr_t item = 0;
    int32_t   def  = 0;

    const uintptr_t inlineItem = weapon + g_off.m_pAttributeManager + g_off.m_Item;
    def = mem.rd<int32_t>(inlineItem + g_off.m_iItemDefinitionIndex);
    if (plausibleDefinition(def)) {
        item = inlineItem;
    } else {
        const uintptr_t container = mem.rd<uintptr_t>(weapon + g_off.m_pAttributeManager);
        if (IMemory::plausible(container)) {
            const uintptr_t pointedItem = container + g_off.m_Item;
            const int32_t pointedDef = mem.rd<int32_t>(pointedItem + g_off.m_iItemDefinitionIndex);
            if (plausibleDefinition(pointedDef)) {
                item = pointedItem;
                def = pointedDef;
            }
        }
    }

    if (!item) return;

    // An EconItemView that has not been initialised yet belongs to a weapon the
    // client is still spawning; writing now would be overwritten a tick later.
    if (offKnown(g_off.m_bItemInitialized)) {
        if (mem.rd<uint8_t>(item + g_off.m_bItemInitialized) == 0) return;
    }

    // ---------------------------------------------------- fallback finish ----
    mem.wr<int32_t>(weapon + g_off.m_nFallbackPaintKit, cfg.skinPaintKit);
    if (offKnown(g_off.m_nFallbackSeed)) {
        mem.wr<int32_t>(weapon + g_off.m_nFallbackSeed, cfg.skinSeed);
    }
    if (offKnown(g_off.m_flFallbackWear)) {
        mem.wr<float>(weapon + g_off.m_flFallbackWear, cfg.skinWear);
    }
    if (offKnown(g_off.m_nFallbackStatTrak)) {
        mem.wr<int32_t>(weapon + g_off.m_nFallbackStatTrak, cfg.skinStatTrak);
    }

    // ----------------------------------------------------- item metadata ----
    if (offKnown(g_off.m_iEntityQuality)) {
        mem.wr<int32_t>(item + g_off.m_iEntityQuality, cfg.skinQuality);
    }
    if (offKnown(g_off.m_iItemIDHigh)) {
        // -1 flags the item as client-side only, which is what keeps the client
        // from reconciling it against the server inventory view.
        mem.wr<int32_t>(item + g_off.m_iItemIDHigh, -1);
    }
    if (offKnown(g_off.m_szCustomName) && cfg.skinName[0]) {
        char name[64]{};
        strncpy_s(name, sizeof(name), cfg.skinName, _TRUNCATE);
        mem.wrBytes(item + g_off.m_szCustomName, name, sizeof(name));
    }

    // -------------------------------------------------------- model mask ----
    // CModelState lives at m_modelState inside the skeleton instance, and
    // m_MeshGroupMask is relative to it.
    if (cfg.skinSetMask && offKnown(g_off.m_MeshGroupMask) &&
        offKnown(g_off.m_modelState) && offKnown(g_off.m_pGameSceneNode)) {
        uintptr_t node = mem.rd<uintptr_t>(weapon + g_off.m_pGameSceneNode);
        if (IMemory::plausible(node)) {
            mem.wr<int32_t>(node + g_off.m_modelState + g_off.m_MeshGroupMask, 2);
        }
    }
}

void SkinChanger::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    applied_ = 0;

    if (!cfg.skinChanger) return;

    if (!available()) {
        if (!warned_) {
            warned_ = true;
            logx::line("SkinChanger: disabled, missing offsets: %s",
                       missing().c_str());
        }
        return;
    }

    if (!frame.haveLocal || !frame.local.alive()) return;

    IMemory& mem = nav.mem();
    if (!mem.canWrite()) return;

    uintptr_t pawn = frame.local.pawn;

    uintptr_t services = mem.rd<uintptr_t>(pawn + g_off.m_pWeaponServices);
    if (!IMemory::plausible(services)) return;

    // CNetworkUtlVectorBase: size at +0, element pointer at +8.
    uintptr_t vec = services + g_off.m_hMyWeapons;
    int32_t count = mem.rd<int32_t>(vec);
    if (count <= 0 || count > 64) return;

    uintptr_t elements = mem.rd<uintptr_t>(vec + 8);
    if (!IMemory::plausible(elements)) return;

    for (int i = 0; i < count; ++i) {
        uint32_t handle = mem.rd<uint32_t>(elements + (uintptr_t)i * 4);
        if (!handle) continue;

        uintptr_t weapon = nav.entityFromHandle(handle);
        if (!IMemory::plausible(weapon) || weapon == pawn) continue;

        applyToWeapon(mem, weapon, cfg);
        ++applied_;
    }
}
