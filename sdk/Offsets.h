// muei 無影 - offsets.
//
// One mutable table, seeded from a cs2-dumper pass, overridable at runtime from
// offsets.cfg so a game update is a text edit instead of a rebuild.
//
// Provenance: the module RVAs and the schema field offsets below come from the
// same public cs2-dumper snapshot, taken as one set. Using them together is what
// makes the pair consistent - mixing an old RVA set with a new schema, or the
// reverse, silently reads the wrong memory. If one field ever looks wrong after
// an update, refresh the whole file rather than patching one number.
//
// Only fields the code actually reads are listed. A field nobody reads is a
// field nobody maintains, and it invites someone to "fix" it later and wonder
// why nothing changed.
#pragma once

#include <cstdint>
#include <string>

// Bone indices - CS2 / Source 2 humanoid skeleton. Engine constants, not schema.
namespace Bone {
    constexpr int HEAD       = 6;
    constexpr int NECK       = 5;
    constexpr int SPINE      = 4;
    constexpr int CHEST      = 23;
    constexpr int PELVIS     = 0;
    constexpr int L_SHOULDER = 8;
    constexpr int L_ELBOW    = 9;
    constexpr int L_HAND     = 10;
    constexpr int R_SHOULDER = 13;
    constexpr int R_ELBOW    = 14;
    constexpr int R_HAND     = 15;
    constexpr int L_HIP      = 16;
    constexpr int L_KNEE     = 17;
    constexpr int L_FOOT     = 18;
    constexpr int R_HIP      = 19;
    constexpr int R_KNEE     = 20;
    constexpr int R_FOOT     = 21;
    constexpr int MAX_BONES  = 128;
}

// Engine geometry, not schema.
namespace Geo {
    constexpr float EYE_OFFSET_Z        = 64.0f;
    constexpr float DUCK_EYE_OFFSET_Z   = 46.0f;
    constexpr uintptr_t MAX_PLAYER_NAME = 128;
}

struct Offsets {
    // Provenance tag, shown in the menu and the log.
    const char* build = "2026-11-21";

    // ------------------------------------------------- client.dll RVAs -----
    uintptr_t dwEntityList            = 0x270D5B0;
    uintptr_t dwLocalPlayerPawn       = 0x2557AB8;
    uintptr_t dwLocalPlayerController = 0x252F300;
    uintptr_t dwViewMatrix            = 0x255D3D0;
    uintptr_t dwViewAngles            = 0x256DBE8;

    // ------------------------------------------- entity list structure -----
    // CGameEntitySystem is a UtlTsHash: buckets at +0x10, each chunk entry 0x78
    // apart. Structure, not schema - stable across builds.
    uintptr_t ENTITY_LIST_HASH    = 0x10;
    uintptr_t ENTITY_CHUNK_STRIDE = 0x08;
    uintptr_t ENTITY_ENTRY_STRIDE = 0x78;
    int       ENTITY_INDEX_MAX    = 64;

    // -------------------------------------- CGameSceneNode / skeleton ------
    uintptr_t m_vecOrigin     = 0x080;
    uintptr_t m_vecAbsOrigin  = 0x0C8;
    uintptr_t m_bDormant      = 0x103;
    uintptr_t m_modelState    = 0x140;   // CSkeletonInstance
    uintptr_t m_pBoneArray    = 0x080;   // inside CModelState, INLINE array
    uintptr_t BONE_SIZE       = 0x20;    // sizeof(CTransform)

    // --------------------------------------------- C_BaseEntity -----------
    uintptr_t m_iMaxHealth     = 0x348;
    uintptr_t m_iHealth        = 0x34C;
    uintptr_t m_lifeState      = 0x354;
    uintptr_t m_pGameSceneNode = 0x330;
    uintptr_t m_iTeamNum       = 0x3E7;
    uintptr_t m_fFlags         = 0x3F4;
    uintptr_t m_vecAbsVelocity = 0x3F8;

    // ---------------------------------------- C_BaseModelEntity glow ------
    uintptr_t m_Glow                        = 0xDE8;  // inline CGlowProperty
    uintptr_t m_iGlowType                   = 0x030;
    uintptr_t m_glowColorOverride           = 0x040;
    uintptr_t m_bEligibleForScreenHighlight = 0x050;

    // --------------------------------------------- player pawn ------------
    uintptr_t m_pWeaponServices   = 0x12F0;
    uintptr_t m_pCameraServices   = 0x1328;
    uintptr_t m_hController       = 0x14BC;

    // View punch lives on the camera services, not on the pawn.
    uintptr_t m_vecCsViewPunchAngle = 0x048;

    uintptr_t m_entitySpottedState = 0x1EB8;   // EntitySpottedState_t
    uintptr_t m_bSpottedByMask     = 0x00C;    // relative to it
    uintptr_t m_iShotsFired        = 0x1EE8;
    uintptr_t m_angEyeAngles       = 0x3630;
    uintptr_t m_iIDEntIndex        = 0x370C;

    uintptr_t m_flFlashBangTime    = 0x14FC;
    uintptr_t m_flFlashDuration    = 0x1510;

    // --------------------------------------------- controllers ------------
    uintptr_t m_iszPlayerName         = 0x6FC;
    uintptr_t m_sSanitizedPlayerName  = 0x878;

    // --------------------------------- weapon / economy (skin changer) ----
    // pawn + m_pWeaponServices -> CPlayer_WeaponServices
    //   + m_hMyWeapons: CNetworkUtlVectorBase (size at +0, pointer at +8)
    // weapon + m_pAttributeManager -> C_AttributeContainer (INLINE)
    //   + m_Item -> C_EconItemView (inline)
    uintptr_t m_hMyWeapons           = 0x048;
    uintptr_t m_pAttributeManager    = 0x1290;
    uintptr_t m_Item                 = 0x050;
    uintptr_t m_iItemDefinitionIndex = 0x1BA;
    uintptr_t m_bItemInitialized     = 0x1E8;
    uintptr_t m_iEntityQuality       = 0x1BC;
    uintptr_t m_iItemIDHigh          = 0x1D0;
    uintptr_t m_szCustomName         = 0x2F8;

    uintptr_t m_nFallbackPaintKit    = 0x18A8;
    uintptr_t m_nFallbackSeed        = 0x18AC;
    uintptr_t m_flFallbackWear       = 0x18B0;
    uintptr_t m_nFallbackStatTrak    = 0x18B4;

    uintptr_t m_MeshGroupMask        = 0x208;  // inside CModelState
};

// The live table. Every feature reads g_off, never the literals above.
inline Offsets g_off{};

inline void OffsetsResetDefaults() { g_off = Offsets{}; }

// Apply offsets.cfg (JSON, one key per field name) over the defaults.
// Returns how many fields were applied; 0 when the file is absent.
int loadOffsetOverrides(const std::wstring& path);

inline bool offKnown(uintptr_t v) { return v != 0; }
