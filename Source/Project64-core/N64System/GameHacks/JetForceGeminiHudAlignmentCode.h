// Draw-only JFG US HUD alignment. Included by JetForceGemini.cpp.
// Keep this segment separate from the widescreen trampolines and HUD wrapper.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace JfgHudAlignmentCode
{
constexpr uint32_t CaveStart = 0x800679A0;
constexpr uint32_t CaveEnd = 0x80067D20;
constexpr uint32_t SpriteWeaponEntry = 0x800679A0;
constexpr uint32_t SpriteHealthEntry = 0x800679C0;
constexpr uint32_t SpriteCommonEntry = 0x800679E0;
constexpr uint32_t SpriteMatrixEntry = 0x80067A30;
constexpr uint32_t MatrixWeaponEntry = 0x80067B50;
constexpr uint32_t MatrixHealthEntry = 0x80067B80;
constexpr uint32_t ActiveKindAddress = 0x80102552;
constexpr uint32_t WeaponDxNdcAddress = 0x80067D00;
constexpr uint32_t WeaponDyNdcAddress = 0x80067D04;
constexpr uint32_t HealthArcDxNdcAddress = 0x80067D08;
constexpr uint32_t HealthArcDyNdcAddress = 0x80067D0C;
constexpr uint32_t HealthSpriteDxNdcAddress = 0x80067D10;
constexpr uint32_t HealthSpriteDyNdcAddress = 0x80067D14;
constexpr uint32_t HalfWidthAddress = 0x80067D18;
constexpr uint32_t MatrixDispatchEntry = 0x80067A6C;
constexpr uint32_t SpriteMatrixHookAddress = 0x800418D8;
constexpr uint32_t SpriteMatrixHookOriginal = 0x0C012361;
constexpr uint32_t SpriteMatrixHookDelay = 0x02402025;
constexpr uint32_t HealthMatrixHookOffset = 0x0000045C;
constexpr uint32_t HealthMatrixHookDelay = 0x02602025;

// Kinds: 0=stock, 1=weapon, 2=health sprite, 3=health arc (explicit entry only).
// The kind byte is alignment padding next to cpuTraceTrackBufStatus, on a data
// page. Per-draw writes must not invalidate the page containing these stubs.
// The host owns this byte and installs the entry redirects only after all code
// and parameters are ready. Matrix entries never change the active sprite kind.
//
// Explicit matrices receive m[12/13] += dx/dyNdc*m[15]. Sprites instead use
// m[15]+HalfWidth: ucode05 billboarding adds the ortho anchor vertex, whose W is
// the framebuffer half-width, before the homogeneous divide. Widescreen changes
// the anchor X, but preserves its W. Apply immediately before mathMtxF2L, then
// restore the source bit-for-bit; only the fixed-point destination keeps the shift.
// No game object coordinates, animation values, or source vertices are changed.
// Scratch FPU registers f4/f6/f8/f10 are caller-saved; f20..f31 and FCSR are preserved.

const uint32_t SpriteWeaponCode[] =
{
    0x27BDFFD0, // addiu sp,sp,-0x30
    0xAFBF002C, // sw ra,0x2C(sp)
    0xAFB80020, // sw t8,0x20(sp)
    0xAFB90024, // sw t9,0x24(sp)
    0x24180001, // addiu t8,zero,1
    0x08019E78, // j SpriteCommonEntry
    0x00000000, // nop
};

const uint32_t SpriteHealthCode[] =
{
    0x27BDFFD0, // addiu sp,sp,-0x30
    0xAFBF002C, // sw ra,0x2C(sp)
    0xAFB80020, // sw t8,0x20(sp)
    0xAFB90024, // sw t9,0x24(sp)
    0x24180002, // addiu t8,zero,2
    0x08019E78, // j SpriteCommonEntry
    0x00000000, // nop
};

const uint32_t SpriteCommonCode[] =
{
    0x3C198010, // lui t9,0x8010
    0x93392552, // lbu t9,0x2552(t9)
    0xAFB90028, // sw t9,0x28(sp) ; previous kind
    0x3C198010, // lui t9,0x8010
    0xA3382552, // sb t8,0x2552(t9)
    0x0C016834, // jal frontDrawObj
    0x00000000, // nop
    0x3C198010, // lui t9,0x8010
    0x8FB80028, // lw t8,0x28(sp)
    0xA3382552, // sb t8,0x2552(t9)
    0x8FB80020, // lw t8,0x20(sp)
    0x8FB90024, // lw t9,0x24(sp)
    0x8FBF002C, // lw ra,0x2C(sp)
    0x03E00008, // jr ra
    0x27BD0030, // addiu sp,sp,0x30
};

const uint32_t SpriteMatrixCode[] =
{
    0x27BDFFC0, // addiu sp,sp,-0x40
    0xAFBF003C, // sw ra,0x3C(sp)
    0xAFA80020, // sw t0,0x20(sp)
    0xAFA90024, // sw t1,0x24(sp)
    0xAFB90028, // sw t9,0x28(sp)
    0xAFA4002C, // sw a0,0x2C(sp)
    0xAFA50030, // sw a1,0x30(sp)
    0x3C198006, // lui t9,0x8006
    0x8F287D18, // lw t0,HalfWidth(t9)
    0xAFA80034, // sw t0,0x34(sp) ; billboarding adds the anchor vertex W
    0x3C198010, // lui t9,0x8010
    0x93282552, // lbu t0,0x2552(t9)
    0x2D090003, // sltiu t1,t0,3 ; only weapon/health kinds are valid for sprites
    0x11200034, // beq t1,zero,stock
    0x00000000, // nop
    0x3C198006, // lui t9,0x8006 ; MatrixDispatchEntry
    0x11000031, // beq t0,zero,stock
    0x24090001, // addiu t1,zero,1
    0x1109000C, // beq t0,t1,weapon
    0x24090002, // addiu t1,zero,2
    0x11090006, // beq t0,t1,health_sprite
    0x24090003, // addiu t1,zero,3
    0x1509002B, // bne t0,t1,stock
    0x00000000, // nop
    0xC7247D08, // lwc1 f4,HealthArcDxNdc(t9)
    0x10000007, // b adjust
    0xC7267D0C, // lwc1 f6,HealthArcDyNdc(t9)
    0xC7247D10, // lwc1 f4,HealthSpriteDxNdc(t9)
    0xC7267D14, // lwc1 f6,HealthSpriteDyNdc(t9)
    0x10000003, // b adjust
    0x00000000, // nop
    0xC7247D00, // lwc1 f4,WeaponDxNdc(t9)
    0xC7267D04, // lwc1 f6,WeaponDyNdc(t9)
    0x4449F800, // cfc1 t1,FCSR ; preserve rounding mode/exception flags
    0xAFA90018, // sw t1,0x18(sp)
    0x8C880030, // lw t0,0x30(a0)
    0xAFA80010, // sw t0,0x10(sp) ; original m[12] bits
    0x8C880034, // lw t0,0x34(a0)
    0xAFA80014, // sw t0,0x14(sp) ; original m[13] bits
    0xC488003C, // lwc1 f8,0x3C(a0)
    0xC7AA0034, // lwc1 f10,0x34(sp) ; zero for explicit matrices, HalfWidth for sprites
    0x460A4200, // add.s f8,f8,f10
    0xC48A0030, // lwc1 f10,0x30(a0)
    0x46082102, // mul.s f4,f4,f8
    0x46045100, // add.s f4,f10,f4
    0xE4840030, // swc1 f4,0x30(a0)
    0xC48A0034, // lwc1 f10,0x34(a0)
    0x46083182, // mul.s f6,f6,f8
    0x46065180, // add.s f6,f10,f6
    0xE4860034, // swc1 f6,0x34(a0)
    0x0C012361, // jal mathMtxF2L
    0x00000000, // nop
    0x8FA4002C, // lw a0,0x2C(sp)
    0x8FA50030, // lw a1,0x30(sp)
    0x8FA80010, // lw t0,0x10(sp)
    0xAC880030, // sw t0,0x30(a0)
    0x8FA80014, // lw t0,0x14(sp)
    0xAC880034, // sw t0,0x34(a0)
    0x8FA90018, // lw t1,0x18(sp)
    0x44C9F800, // ctc1 t1,FCSR
    0x8FA80020, // lw t0,0x20(sp)
    0x8FA90024, // lw t1,0x24(sp)
    0x8FB90028, // lw t9,0x28(sp)
    0x8FBF003C, // lw ra,0x3C(sp)
    0x03E00008, // jr ra
    0x27BD0040, // addiu sp,sp,0x40
    0x8FA80020, // lw t0,0x20(sp)
    0x8FA90024, // lw t1,0x24(sp)
    0x8FB90028, // lw t9,0x28(sp)
    0x8FBF003C, // lw ra,0x3C(sp)
    0x08012361, // j mathMtxF2L
    0x27BD0040, // addiu sp,sp,0x40
};

const uint32_t MatrixWeaponCode[] =
{
    0x27BDFFC0, // addiu sp,sp,-0x40
    0xAFBF003C, // sw ra,0x3C(sp)
    0xAFA80020, // sw t0,0x20(sp)
    0xAFA90024, // sw t1,0x24(sp)
    0xAFB90028, // sw t9,0x28(sp)
    0xAFA4002C, // sw a0,0x2C(sp)
    0xAFA50030, // sw a1,0x30(sp)
    0xAFA00034, // sw zero,0x34(sp) ; explicit matrices do not add a billboard anchor
    0x24080001, // addiu t0,zero,1 ; explicit matrix kind
    0x08019E9B, // j MatrixDispatchEntry
    0x00000000, // nop
};

const uint32_t MatrixHealthCode[] =
{
    0x27BDFFC0, // addiu sp,sp,-0x40
    0xAFBF003C, // sw ra,0x3C(sp)
    0xAFA80020, // sw t0,0x20(sp)
    0xAFA90024, // sw t1,0x24(sp)
    0xAFB90028, // sw t9,0x28(sp)
    0xAFA4002C, // sw a0,0x2C(sp)
    0xAFA50030, // sw a1,0x30(sp)
    0xAFA00034, // sw zero,0x34(sp) ; explicit matrices do not add a billboard anchor
    0x24080003, // addiu t0,zero,3 ; explicit matrix kind
    0x08019E9B, // j MatrixDispatchEntry
    0x00000000, // nop
};

static_assert(SpriteWeaponEntry + sizeof(SpriteWeaponCode) <= SpriteHealthEntry, "Sprite entries overlap");
static_assert(SpriteHealthEntry + sizeof(SpriteHealthCode) <= SpriteCommonEntry, "Sprite entries overlap");
static_assert(SpriteCommonEntry + sizeof(SpriteCommonCode) <= SpriteMatrixEntry, "Sprite wrapper overlaps matrix code");
static_assert(SpriteMatrixEntry + sizeof(SpriteMatrixCode) <= MatrixWeaponEntry, "Matrix code overlaps explicit entries");
static_assert(MatrixWeaponEntry + sizeof(MatrixWeaponCode) <= MatrixHealthEntry, "Explicit matrix entries overlap");
static_assert(MatrixHealthEntry + sizeof(MatrixHealthCode) <= WeaponDxNdcAddress, "Matrix entries overlap parameters");
static_assert(HalfWidthAddress + sizeof(uint32_t) <= CaveEnd, "Alignment segment exceeds reserved range");

inline bool BuildImage(std::vector<uint32_t> & Image, float WeaponDxNdc,
                       float WeaponDyNdc, float HealthArcDxNdc,
                       float HealthArcDyNdc, float HealthSpriteDxNdc,
                       float HealthSpriteDyNdc, float HalfWidth)
{
    Image.assign((CaveEnd - CaveStart) / sizeof(uint32_t), 0);
    auto Place = [&](uint32_t Address, const uint32_t * Words, size_t Count) {
        if ((Address & 3) != 0 || Address < CaveStart || Address > CaveEnd ||
            Count > (CaveEnd - Address) / sizeof(uint32_t))
        {
            return false;
        }
        const size_t Index = (Address - CaveStart) / sizeof(uint32_t);
        for (size_t i = 0; i < Count; i++)
        {
            Image[Index + i] = Words[i];
        }
        return true;
    };
    if (!Place(SpriteWeaponEntry, SpriteWeaponCode, sizeof(SpriteWeaponCode) / sizeof(SpriteWeaponCode[0])))
    {
        return false;
    }
    if (!Place(SpriteHealthEntry, SpriteHealthCode, sizeof(SpriteHealthCode) / sizeof(SpriteHealthCode[0])))
    {
        return false;
    }
    if (!Place(SpriteCommonEntry, SpriteCommonCode, sizeof(SpriteCommonCode) / sizeof(SpriteCommonCode[0])))
    {
        return false;
    }
    if (!Place(SpriteMatrixEntry, SpriteMatrixCode, sizeof(SpriteMatrixCode) / sizeof(SpriteMatrixCode[0])))
    {
        return false;
    }
    if (!Place(MatrixWeaponEntry, MatrixWeaponCode, sizeof(MatrixWeaponCode) / sizeof(MatrixWeaponCode[0])))
    {
        return false;
    }
    if (!Place(MatrixHealthEntry, MatrixHealthCode, sizeof(MatrixHealthCode) / sizeof(MatrixHealthCode[0])))
    {
        return false;
    }
    const float Values[] = { WeaponDxNdc, WeaponDyNdc, HealthArcDxNdc,
                             HealthArcDyNdc, HealthSpriteDxNdc, HealthSpriteDyNdc, HalfWidth };
    uint32_t Words[7] = {};
    static_assert(sizeof(float) == sizeof(uint32_t), "Guest parameters require 32-bit float");
    std::memcpy(Words, Values, sizeof(Words));
    return Place(WeaponDxNdcAddress, Words, 7);
}
} // namespace JfgHudAlignmentCode
