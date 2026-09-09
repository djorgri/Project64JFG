#pragma once

#include <cstdint>

namespace JfgHudAlignmentSites
{
struct CallSite
{
    uint32_t Offset;
    uint32_t Original;
    uint32_t Delay;
};

// US overlay 14. These calls belong to the weapon group at +0x292C,
// including its pickup banner, weapon selector and tribal counter banner.
// Region statistics reuse front-end objects but have different call sites.
const CallSite WeaponSpriteCalls[] =
{
    { 0x190C, 0x0C016834, 0xA42EF786 }, // selector: object 0, texture selected in delay
    { 0x24C4, 0x0C016834, 0xE45201B8 }, // tribal counter: first object 13
    { 0x24F8, 0x0C016834, 0xE42AF938 }, // tribal counter: second object 13
    { 0x2528, 0x0C016834, 0xE426F938 }, // tribal counter: third object 13
    { 0x2900, 0x0C016834, 0x24040004 }, // selector cap: object 4
    { 0x2908, 0x0C016834, 0x24040005 }, // pickup cap: object 5
    { 0x2910, 0x0C016834, 0x24040006 }, // tribal counter cap: object 6
    { 0x29E8, 0x0C016834, 0x24040001 }, // current weapon: object 1
    { 0x2BA0, 0x0C016834, 0x24040007 }, // weapon panel indicator: object 7
};

// mathMtxF2L consumes the final float matrix after translation and scaling.
const CallSite WeaponMatrixCalls[] =
{
    { 0x1418, 0x0C012361, 0x02202025 }, // selector backing
    { 0x1B58, 0x0C012361, 0x27A40120 }, // pickup backing
    { 0x202C, 0x0C012361, 0x27A40120 }, // tribal counter backing
    { 0x2670, 0x0C012361, 0x27A40120 }, // weapon frame
};

// US overlay 6, instDrawHealth at +0x0000. Its multiplayer path shares
// the matrix call, so the caller must still enforce the single-player guard.
const CallSite HealthSpriteCall = { 0x0C34, 0x0C016834, 0xAE190000 };
const CallSite HealthMatrixCall = { 0x045C, 0x0C012361, 0x02602025 };
const uint32_t HealthFunctionOffset = 0x0000;
const uint32_t HealthFunctionPrologue[] = { 0x27BDFEE8, 0xAFBF003C };

// The local JAL at overlay 14 +0xC9C must be relocated from the current
// overlay base. Wrapping this group excludes other instrument renderers.
const uint32_t WeaponGroupCallOffset = 0x0C9C;
const uint32_t WeaponGroupCallDelay = 0x00000000;
const uint32_t WeaponGroupFunctionOffset = 0x292C;
const uint32_t WeaponGroupFunctionPrologue[] = { 0x27BDFFA0, 0xAFBF0024 };
}
