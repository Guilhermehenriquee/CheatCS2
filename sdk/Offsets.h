// muei 無影 - offsets.
//
// Every offset lives in one mutable struct, g_off, seeded from the compile-time
// defaults below. That is deliberate: an external overlay breaks on every game
// update, and rebuilding is a much worse workflow than dropping a new JSON in
// beside the exe. loadOffsetOverrides() applies offsets.cfg on top of the
// defaults at startup, field by field, so a partial file only overrides what it
// mentions.
//
// Defaults come from a live cs2-dumper pass against build 14195. Fields marked
// "verify" could not be confirmed against that build and are gated at runtime:
// the feature that uses them refuses to act until offsetVerify() says the value
// resolves to something plausible. That turns a wrong offset into a disabled
// feature instead of a write into random memory.
#pragma once

#include <cstdint>
#include <string>

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

// Values are engine geometry, not schema, so they live outside the mutable set.
namespace Geo {
    constexpr float EYE_OFFSET_Z       = 64.0f;
    constexpr float DUCK_EYE_OFFSET_Z  = 46.0f;
    constexpr uintptr_t MAX_PLAYER_NAME = 128;
}

struct Offsets {
    // ------------------------------------------------------- provenance ------
    // Human-readable build tag, printed in the menu and the log.
    const char* build = "14195";

    // ------------------------------------------------ client.dll modules -----
    uintptr_t dwEntityList            = 0x271EBA8;
    uintptr_t dwLocalPlayerPawn       = 0x2569948;
    uintptr_t dwLocalPlayerController = 0x2541538;
    uintptr_t dwViewMatrix            = 0x256F250;
    uintptr_t dwViewAngles            = 0x2580098;
    uintptr_t dwGameRules             = 0x25693F8;
    uintptr_t dwPlantedC4             = 0x24CFB60;

    // ------------------------------------------------------ entity list ------
    // CGameEntitySystem is a UtlTsHash: list -> buckets at +0x10, each bucket
    // 0x18 wide, entries 0x78 apart inside a chunk.
    uintptr_t ENTITY_LIST_HASH     = 0x10;
    uintptr_t ENTITY_CHUNK_STRIDE  = 0x08;
    uintptr_t ENTITY_ENTRY_STRIDE  = 0x78;
    int       ENTITY_INDEX_MAX     = 64;

    // ------------------------------------------- CGameSceneNode / skeleton ---
    uintptr_t m_vecOrigin          = 0x080;
    uintptr_t m_vecAbsOrigin       = 0x0C8;
    uintptr_t m_angAbsRotation     = 0x0D4;
    uintptr_t m_bDormant           = 0x103;
    uintptr_t m_modelState         = 0x140;
    uintptr_t m_pBoneArray         = 0x080;
    uintptr_t BONE_SIZE            = 0x20;

    // ------------------------------------------------- C_BaseEntity ----------
    uintptr_t m_iMaxHealth         = 0x348;
    uintptr_t m_iHealth            = 0x34C;
    uintptr_t m_lifeState          = 0x354;
    uintptr_t m_pGameSceneNode     = 0x330;
    uintptr_t m_iTeamNum           = 0x3E7;
    uintptr_t m_fFlags             = 0x3F4;

    // ---------------------------------------------- C_BaseModelEntity --------
    uintptr_t m_Glow                        = 0xDE8;
    uintptr_t m_fGlowColor                  = 0x008;
    uintptr_t m_iGlowType                   = 0x030;
    uintptr_t m_iGlowTeam                   = 0x034;
    uintptr_t m_nGlowRange                  = 0x038;
    uintptr_t m_nGlowRangeMin               = 0x03C;
    uintptr_t m_glowColorOverride           = 0x040;
    uintptr_t m_flGlowTime                  = 0x048;
    uintptr_t m_flGlowStartTime             = 0x04C;
    uintptr_t m_bEligibleForScreenHighlight = 0x050;
    uintptr_t m_bGlowing                    = 0x051;

    // ------------------------------------------------- player pawn -----------
    uintptr_t m_pWeaponServices    = 0x12F0;
    uintptr_t m_pMovementServices  = 0x1330;
    uintptr_t m_hController        = 0x14BC;
    uintptr_t v_angle              = 0x13A8;
    uintptr_t v_anglePrevious      = 0x13B4;

    uintptr_t m_hActiveWeapon      = 0x060;   // inside CPlayer_WeaponServices

    uintptr_t m_pBulletServices    = 0x1570;
    uintptr_t m_pAimPunchServices  = 0x1598;
    uintptr_t m_entitySpottedState = 0x1E88;
    uintptr_t m_bSpotted           = 0x008;
    uintptr_t m_bSpottedByMask     = 0x00C;
    uintptr_t m_iShotsFired        = 0x1EB8;
    uintptr_t m_bIsScoped          = 0x1EA0;
    uintptr_t m_angEyeAngles       = 0x3600;
    uintptr_t m_iIDEntIndex        = 0x36DC;

    uintptr_t m_flFlashBangTime    = 0x14FC;
    uintptr_t m_flFlashDuration    = 0x1510;
    uintptr_t m_flFlashMaxAlpha    = 0x150C;
    uintptr_t m_flFlashOverlayAlpha= 0x1504;

    // ------------------------------------------------- controllers -----------
    uintptr_t m_hPawn                    = 0x6BC;
    uintptr_t m_iszPlayerName            = 0x6FC;
    uintptr_t m_bIsLocalPlayerController = 0x790;
    uintptr_t m_sSanitizedPlayerName     = 0x878;
    uintptr_t m_hPlayerPawn              = 0x92C;

    // -----------------------------------------------------------------------
    //  EXTENDED FIELDS
    //
    //  Sourced from a public cs2-dumper schema dump cross-checked field by field
    //  against this build's own dump: m_iHealth, m_iMaxHealth, m_pGameSceneNode,
    //  m_iTeamNum, m_fFlags, m_lifeState, m_modelState, m_iShotsFired,
    //  m_angEyeAngles and m_iIDEntIndex all matched exactly, so the schema
    //  generation is the same and these offsets are sound. Module-relative RVAs
    //  from that dump were NOT adopted - they differ by a few hundred bytes,
    //  which means a different build.
    //
    //  Still overridable through offsets.cfg like everything else.
    // -----------------------------------------------------------------------

    // Recoil. CS2 applies the view punch through the camera services, not as a
    // field on the pawn:
    //   pawn + m_pCameraServices -> + m_vecCsViewPunchAngle
    uintptr_t m_pCameraServices         = 0x1328;
    uintptr_t m_vecCsViewPunchAngle     = 0x048;
    uintptr_t m_flCsViewPunchTickRatio  = 0x058;

    // Absolute velocity, for the resolver's lead.
    uintptr_t m_vecAbsVelocity          = 0x3F8;

    // Weapon / economy chain, for the skin changer:
    //   pawn + m_pWeaponServices -> CPlayer_WeaponServices
    //     + m_hMyWeapons: CNetworkUtlVectorBase (size at +0, pointer at +8)
    //   weapon entity + m_pAttributeManager -> C_AttributeContainer (INLINE)
    //     + m_Item -> C_EconItemView (inline)
    //     + m_iItemDefinitionIndex / m_bInitialized
    //   weapon + m_nFallbackPaintKit .. m_nFallbackStatTrak
    uintptr_t m_hMyWeapons              = 0x048;
    uintptr_t m_pAttributeManager       = 0x1290;
    uintptr_t m_Item                    = 0x050;
    uintptr_t m_iItemDefinitionIndex    = 0x1BA;
    uintptr_t m_bItemInitialized        = 0x1E8;
    uintptr_t m_iEntityQuality          = 0x1BC;
    uintptr_t m_iItemIDHigh             = 0x1D0;
    uintptr_t m_szCustomName            = 0x2F8;
    uintptr_t m_nFallbackPaintKit       = 0x18A8;
    uintptr_t m_nFallbackSeed           = 0x18AC;
    uintptr_t m_flFallbackWear          = 0x18B0;
    uintptr_t m_nFallbackStatTrak       = 0x18B4;

    // CModelState::m_MeshGroupMask, relative to CModelState (which sits at
    // m_modelState inside the skeleton instance).
    uintptr_t m_MeshGroupMask           = 0x208;
};

// The live table. Every feature reads g_off, never the literals above.
inline Offsets g_off{};

inline void OffsetsResetDefaults() { g_off = Offsets{}; }

// Apply offsets.cfg (JSON: { "m_iHealth": 844, ... }) over the defaults.
// Returns the number of fields applied; 0 when the file is absent. Unknown keys
// and wrong types are reported but never abort the load.
int loadOffsetOverrides(const std::wstring& path);

// True when the field is present (non-zero) and therefore usable.
inline bool offKnown(uintptr_t v) { return v != 0; }
