#include "Offsets.h"
#include "../core/Log.h"

#include "../vendor/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace {

struct Field {
    const char* name;
    uintptr_t*  slot;
};

// Every mutable uintptr_t field, addressable by its own name. Keeping this list
// next to the struct is the only maintenance cost of the override system: add a
// field, add one line here.
std::vector<Field> fieldTable() {
    return {
        { "dwEntityList",            &g_off.dwEntityList },
        { "dwLocalPlayerPawn",       &g_off.dwLocalPlayerPawn },
        { "dwLocalPlayerController", &g_off.dwLocalPlayerController },
        { "dwViewMatrix",            &g_off.dwViewMatrix },
        { "dwViewAngles",            &g_off.dwViewAngles },
        { "dwGameRules",             &g_off.dwGameRules },
        { "dwPlantedC4",             &g_off.dwPlantedC4 },

        { "ENTITY_LIST_HASH",        &g_off.ENTITY_LIST_HASH },
        { "ENTITY_CHUNK_STRIDE",     &g_off.ENTITY_CHUNK_STRIDE },
        { "ENTITY_ENTRY_STRIDE",     &g_off.ENTITY_ENTRY_STRIDE },

        { "m_vecOrigin",             &g_off.m_vecOrigin },
        { "m_vecAbsOrigin",          &g_off.m_vecAbsOrigin },
        { "m_angAbsRotation",        &g_off.m_angAbsRotation },
        { "m_bDormant",              &g_off.m_bDormant },
        { "m_modelState",            &g_off.m_modelState },
        { "m_pBoneArray",            &g_off.m_pBoneArray },
        { "BONE_SIZE",               &g_off.BONE_SIZE },

        { "m_iMaxHealth",            &g_off.m_iMaxHealth },
        { "m_iHealth",               &g_off.m_iHealth },
        { "m_lifeState",             &g_off.m_lifeState },
        { "m_pGameSceneNode",        &g_off.m_pGameSceneNode },
        { "m_iTeamNum",              &g_off.m_iTeamNum },
        { "m_fFlags",                &g_off.m_fFlags },

        { "m_Glow",                  &g_off.m_Glow },
        { "m_fGlowColor",            &g_off.m_fGlowColor },
        { "m_iGlowType",             &g_off.m_iGlowType },
        { "m_iGlowTeam",             &g_off.m_iGlowTeam },
        { "m_nGlowRange",            &g_off.m_nGlowRange },
        { "m_nGlowRangeMin",         &g_off.m_nGlowRangeMin },
        { "m_glowColorOverride",     &g_off.m_glowColorOverride },
        { "m_flGlowTime",            &g_off.m_flGlowTime },
        { "m_flGlowStartTime",       &g_off.m_flGlowStartTime },
        { "m_bEligibleForScreenHighlight", &g_off.m_bEligibleForScreenHighlight },
        { "m_bGlowing",              &g_off.m_bGlowing },

        { "m_pWeaponServices",       &g_off.m_pWeaponServices },
        { "m_pMovementServices",     &g_off.m_pMovementServices },
        { "m_hController",           &g_off.m_hController },
        { "v_angle",                 &g_off.v_angle },
        { "v_anglePrevious",         &g_off.v_anglePrevious },
        { "m_hActiveWeapon",         &g_off.m_hActiveWeapon },

        { "m_pBulletServices",       &g_off.m_pBulletServices },
        { "m_pAimPunchServices",     &g_off.m_pAimPunchServices },
        { "m_entitySpottedState",    &g_off.m_entitySpottedState },
        { "m_bSpotted",              &g_off.m_bSpotted },
        { "m_bSpottedByMask",        &g_off.m_bSpottedByMask },
        { "m_iShotsFired",           &g_off.m_iShotsFired },
        { "m_bIsScoped",             &g_off.m_bIsScoped },
        { "m_angEyeAngles",          &g_off.m_angEyeAngles },
        { "m_iIDEntIndex",           &g_off.m_iIDEntIndex },

        { "m_flFlashBangTime",       &g_off.m_flFlashBangTime },
        { "m_flFlashDuration",       &g_off.m_flFlashDuration },
        { "m_flFlashMaxAlpha",       &g_off.m_flFlashMaxAlpha },
        { "m_flFlashOverlayAlpha",   &g_off.m_flFlashOverlayAlpha },

        { "m_hPawn",                 &g_off.m_hPawn },
        { "m_iszPlayerName",         &g_off.m_iszPlayerName },
        { "m_bIsLocalPlayerController", &g_off.m_bIsLocalPlayerController },
        { "m_sSanitizedPlayerName",  &g_off.m_sSanitizedPlayerName },
        { "m_hPlayerPawn",           &g_off.m_hPlayerPawn },

        { "m_pCameraServices",       &g_off.m_pCameraServices },
        { "m_vecCsViewPunchAngle",   &g_off.m_vecCsViewPunchAngle },
        { "m_flCsViewPunchTickRatio",&g_off.m_flCsViewPunchTickRatio },
        { "m_vecAbsVelocity",        &g_off.m_vecAbsVelocity },
        { "m_hMyWeapons",            &g_off.m_hMyWeapons },
        { "m_pAttributeManager",     &g_off.m_pAttributeManager },
        { "m_Item",                  &g_off.m_Item },
        { "m_iItemDefinitionIndex",  &g_off.m_iItemDefinitionIndex },
        { "m_bItemInitialized",      &g_off.m_bItemInitialized },
        { "m_iEntityQuality",        &g_off.m_iEntityQuality },
        { "m_iItemIDHigh",           &g_off.m_iItemIDHigh },
        { "m_szCustomName",          &g_off.m_szCustomName },
        { "m_nFallbackPaintKit",     &g_off.m_nFallbackPaintKit },
        { "m_nFallbackSeed",         &g_off.m_nFallbackSeed },
        { "m_flFallbackWear",        &g_off.m_flFallbackWear },
        { "m_nFallbackStatTrak",     &g_off.m_nFallbackStatTrak },
        { "m_MeshGroupMask",         &g_off.m_MeshGroupMask },
    };
}

} // namespace

int loadOffsetOverrides(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in.good()) {
        logx::line("offsets: no override file, using build %s defaults", g_off.build);
        return 0;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (text.empty()) return 0;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        logx::line("offsets: parse failed (%s), keeping defaults", e.what());
        return 0;
    }
    if (!j.is_object()) {
        logx::line("offsets: root is not an object, keeping defaults");
        return 0;
    }

    int applied = 0;
    for (auto& f : fieldTable()) {
        auto it = j.find(f.name);
        if (it == j.end()) continue;

        // Accept 123, 0x1F (already a JSON int), or a "0x1F" / "31" string.
        uintptr_t value = 0;
        bool have = false;

        if (it->is_number_unsigned()) {
            value = (uintptr_t)it->get<unsigned long long>();
            have = true;
        } else if (it->is_number_integer()) {
            long long v = it->get<long long>();
            if (v >= 0) { value = (uintptr_t)v; have = true; }
        } else if (it->is_string()) {
            const std::string s = it->get<std::string>();
            try {
                value = (uintptr_t)std::stoull(s, nullptr, 0);  // base 0 honours 0x
                have = true;
            } catch (...) {
                logx::line("offsets: bad number for %s: \"%s\"", f.name, s.c_str());
            }
        }

        if (!have) continue;
        *f.slot = value;
        ++applied;
    }

    // Integer field with its own type.
    auto it = j.find("ENTITY_INDEX_MAX");
    if (it != j.end() && it->is_number_integer()) {
        int v = it->get<int>();
        if (v > 0 && v <= 4096) g_off.ENTITY_INDEX_MAX = v;
    }

    auto bt = j.find("build");
    if (bt != j.end() && bt->is_string()) {
        // The string must outlive the call; store it in a static.
        static std::string buildTag;
        buildTag = bt->get<std::string>();
        g_off.build = buildTag.c_str();
    }

    logx::line("offsets: applied %d overrides from file (build %s)", applied, g_off.build);
    return applied;
}
