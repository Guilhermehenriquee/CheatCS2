#pragma once
#include <cstdint>

// -----------------------------------------------------------------------------
// CS2 offsets - dumped from live client.dll
// build_number : 14195
// dumped_at    : 2026-11-03T23:17:37Z
// source       : cs2-dumper 0.1.3 analysis pass (output/offsets.json + output/*_dll.json)
//
// Two patterns in that pass were flagged OUTDATED and are NOT used here:
//   dwSensitivity, dwSoundSystem_engineViewData
// Neither is referenced by this project. Everything below came from patterns that
// resolved clean, or from the schema dump (authoritative for field offsets).
//
// Module-relative values are added to the base of the module named in the comment.
// proc.h resolves client.dll and engine2.dll bases independently - do not use
// cs2.exe's process base for these.
// -----------------------------------------------------------------------------

namespace Off {

    constexpr const char* BUILD_NUMBER = "14195";

    // ---------------------------- module relative -----------------------------

    // client.dll
    constexpr uintptr_t dwEntityList            = 0x271EBA8; // CGameEntitySystem*  (chunked hash, see Nav::Entity)
    constexpr uintptr_t dwLocalPlayerPawn       = 0x2569948; // C_CSPlayerPawn*
    constexpr uintptr_t dwLocalPlayerController = 0x2541538; // C_CSPlayerController*
    constexpr uintptr_t dwViewMatrix            = 0x256F250; // float[4][4]  view-projection
    constexpr uintptr_t dwViewAngles            = 0x2580098; // QAngle       camera angles the engine reads
    constexpr uintptr_t dwCSGOInput             = 0x257FA10; // C_CSGOInput* (unused, kept for reference)
    constexpr uintptr_t dwGlowManager           = 0x2566120; // C_GlowManager* (unused, per-entity glow is used)
    constexpr uintptr_t dwGameRules             = 0x25693F8; // C_CSGameRules*
    constexpr uintptr_t dwGameEntitySystem      = 0x271EBA8; // same address as dwEntityList
    constexpr uintptr_t dwPrediction            = 0x2569850; // C_Prediction*
    constexpr uintptr_t dwPlantedC4             = 0x24CFB60; // C_PlantedC4*
    constexpr uintptr_t dwWeaponC4              = 0x24CE108; // C_C4*

    // ---------------------------- entity list --------------------------------
    // CGameEntitySystem is a UtlTsHash<C_BaseEntity*, 256, u64>.
    //   gs + 0x10                       -> UtlTsHash
    //   tsHash + 0x00                   -> buckets[0]  (each bucket is 0x18 bytes)
    //   bucket + 0x08                   -> UtlTsHashFixedData* (chained via +0x08)
    //   node   + 0x10                   -> C_BaseEntity*
    // The classic chunk layout (chunk = *(list + 0x10 + 8*(i>>9)); ent = *(chunk + 0x78*(i&0x1FF)))
    // still walks the same bucket memory and is what this project uses - see nav.h.
    constexpr uintptr_t ENTITY_LIST_HASH       = 0x10;   // dwEntityList -> UtlTsHash
    constexpr uintptr_t ENTITY_BUCKET_STRIDE   = 0x18;   // sizeof(UtlTsHashBucket)
    constexpr uintptr_t ENTITY_NODE_NEXT       = 0x08;   // UtlTsHashFixedData::next
    constexpr uintptr_t ENTITY_NODE_DATA       = 0x10;   // UtlTsHashFixedData::data
    constexpr uintptr_t ENTITY_CHUNK_STRIDE    = 0x08;   // list + 0x10 + 8*(index >> 9)
    constexpr uintptr_t ENTITY_ENTRY_STRIDE    = 0x78;   // chunk + 0x78 * (index & 0x1FF)
    constexpr int       ENTITY_INDEX_MAX       = 64;     // 0..64 covers public + casual slots

    // ---------------------------- schema: CGameSceneNode ---------------------
    constexpr uintptr_t m_vecOrigin             = 0x080; // Vec3   local origin (not absolute)
    constexpr uintptr_t m_vecAbsOrigin          = 0x0C8; // Vec3   world origin - use this one
    constexpr uintptr_t m_angAbsRotation        = 0x0D4; // QAngle world rotation
    constexpr uintptr_t m_bDormant               = 0x103; // bool   entity not networked in

    // ---------------------------- schema: CSkeletonInstance ------------------
    // CSkeletonInstance : CGameSceneNode
    constexpr uintptr_t m_modelState            = 0x140; // CModelState
    // CModelState.m_pBoneArray / m_boneArray are NOT in the schema - fixed layout:
    constexpr uintptr_t m_pBoneArray            = 0x080; // CTransform[] inside CModelState
    constexpr uintptr_t m_boneArray             = 0x080; // alias, same address
    constexpr uintptr_t BONE_SIZE               = 0x20;  // sizeof(CTransform) = quat(16) + vec(12) + pad(4)

    // ---------------------------- schema: C_BaseEntity -----------------------
    constexpr uintptr_t m_iMaxHealth            = 0x348; // int32
    constexpr uintptr_t m_iHealth               = 0x34C; // int32
    constexpr uintptr_t m_lifeState             = 0x354; // uint8  0 == alive
    constexpr uintptr_t m_pGameSceneNode        = 0x330; // CGameSceneNode*
    constexpr uintptr_t m_iTeamNum              = 0x3E7; // uint8  2 = T, 3 = CT
    constexpr uintptr_t m_fFlags                = 0x3F4; // uint32 bit0 = FL_ONGROUND

    // ---------------------------- schema: C_BaseModelEntity ------------------
    constexpr uintptr_t m_Glow                  = 0xDE8; // CGlowProperty (inline, NOT a pointer)

    // CGlowProperty fields, relative to m_Glow
    constexpr uintptr_t m_fGlowColor            = 0x008; // Vec3
    constexpr uintptr_t m_iGlowType             = 0x030; // int32  3 = always on
    constexpr uintptr_t m_iGlowTeam             = 0x034; // int32
    constexpr uintptr_t m_nGlowRange            = 0x038; // int32
    constexpr uintptr_t m_nGlowRangeMin         = 0x03C; // int32
    constexpr uintptr_t m_glowColorOverride     = 0x040; // Color32
    constexpr uintptr_t m_flGlowTime            = 0x048; // float
    constexpr uintptr_t m_flGlowStartTime       = 0x04C; // float
    constexpr uintptr_t m_bEligibleForScreenHighlight = 0x050; // bool
    constexpr uintptr_t m_bGlowing              = 0x051; // bool

    // ---------------------------- schema: C_BasePlayerPawn -------------------
    constexpr uintptr_t m_pWeaponServices       = 0x12F0; // CPlayer_WeaponServices*
    constexpr uintptr_t m_pMovementServices     = 0x1330; // CPlayer_MovementServices*
    constexpr uintptr_t m_hController           = 0x14BC; // CBaseHandle
    constexpr uintptr_t v_angle                 = 0x13A8; // QAngle  (5032) server-side view angles
    constexpr uintptr_t v_anglePrevious         = 0x13B4; // QAngle  (5044)

    // CPlayer_WeaponServices
    constexpr uintptr_t m_hActiveWeapon         = 0x060; // CBaseHandle (C_CSWeaponBase*)

    // ---------------------------- schema: C_CSPlayerPawn ---------------------
    constexpr uintptr_t m_pBulletServices       = 0x1570; // C_CSPlayerBulletServices*
    constexpr uintptr_t m_pAimPunchServices     = 0x1598; // C_CSPlayerAimPunchServices*
    constexpr uintptr_t m_entitySpottedState    = 0x1E88; // EntitySpottedState_t
    constexpr uintptr_t m_bSpotted              = 0x008; // relative to m_entitySpottedState
    constexpr uintptr_t m_bSpottedByMask        = 0x00C; // relative, uint64[2] - bit i => entity i+1 spotted
    constexpr uintptr_t m_iShotsFired           = 0x1EB8; // int32
    constexpr uintptr_t m_bIsScoped              = 0x1EA0; // bool
    constexpr uintptr_t m_angEyeAngles          = 0x3600; // QAngle  client eye angles (pitch, yaw, roll)
    constexpr uintptr_t m_iIDEntIndex           = 0x36DC; // CBaseHandle crosshair entity

    // ---------------------------- schema: C_CSPlayerPawnBase ------------------
    constexpr uintptr_t m_flFlashBangTime       = 0x14FC; // float   flash start timestamp
    constexpr uintptr_t m_flFlashDuration       = 0x1510; // float   flash length in seconds
    constexpr uintptr_t m_flFlashMaxAlpha       = 0x150C; // float
    constexpr uintptr_t m_flFlashOverlayAlpha   = 0x1504; // float

    // ---------------------------- schema: controllers ------------------------
    constexpr uintptr_t m_hPawn                 = 0x6BC;  // CBasePlayerController
    constexpr uintptr_t m_iszPlayerName         = 0x6FC;  // CBasePlayerController, char[128]
    constexpr uintptr_t m_bIsLocalPlayerController = 0x790; // CBasePlayerController
    constexpr uintptr_t m_iCompTeammateColor    = 0x858;  // CCSPlayerController
    constexpr uintptr_t m_sSanitizedPlayerName  = 0x878;  // CCSPlayerController, char[128]
    constexpr uintptr_t m_hPlayerPawn           = 0x92C;  // CCSPlayerController

    // ---------------------------- misc ---------------------------------------
    constexpr uintptr_t MAX_PLAYER_NAME         = 128;    // name buffers are fixed 128 bytes

    // Hardcoded-by-engine geometry (not in schema, stable across builds)
    constexpr float EYE_OFFSET_Z                = 64.0f;  // stand eye height above origin
    constexpr float DUCK_EYE_OFFSET_Z           = 46.0f;  // crouched eye height

} // namespace Off

// -----------------------------------------------------------------------------
// Bone indices - CS2 / Source 2 standard humanoid skeleton.
// Used by aimbot targeting and skeleton ESP.
// -----------------------------------------------------------------------------
namespace Bone {
    constexpr int HEAD        = 6;   // head
    constexpr int NECK        = 5;
    constexpr int SPINE       = 4;   // upper spine
    constexpr int CHEST       = 23;  // spine_02 / chest
    constexpr int PELVIS      = 0;   // root / pelvis
    constexpr int L_SHOULDER  = 8;
    constexpr int L_ELBOW     = 9;
    constexpr int L_HAND      = 10;
    constexpr int R_SHOULDER  = 13;
    constexpr int R_ELBOW     = 14;
    constexpr int R_HAND      = 15;
    constexpr int L_HIP       = 16;
    constexpr int L_KNEE      = 17;
    constexpr int L_FOOT      = 18;
    constexpr int R_HIP       = 19;
    constexpr int R_KNEE      = 20;
    constexpr int R_FOOT      = 21;
    constexpr int MAX_BONES   = 128;
}
