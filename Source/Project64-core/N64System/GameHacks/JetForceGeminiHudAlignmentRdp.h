#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace JfgHudAlignmentRdp
{
constexpr uint32_t CaveStart = 0x80067D20;
constexpr uint32_t CaveEnd = 0x800680A0;
constexpr uint32_t Entry = 0x80067D20;
constexpr uint32_t ParamsStart = 0x80068080;
constexpr uint32_t DxQAddress = 0x80068080;
constexpr uint32_t DyQAddress = 0x80068084;
constexpr uint32_t WidthQAddress = 0x80068088;
constexpr uint32_t HeightQAddress = 0x8006808C;
constexpr uint32_t MagicAddress = 0x80068090;
constexpr uint32_t MagicValue = 0x4A464752; // JFGR
constexpr uint32_t BodyCallAddress = 0x80067D54;
constexpr uint32_t DisplayListCursorAddress = 0x800FF398;

// Wrap only overlay 14's weapon group (+0xC9C -> +0x292C). Matrices and
// SP triangles receive their shift elsewhere; this pass adjusts only inline
// TextureRectangle/TextureRectangleFlip/FillRectangle/SetScissor commands.
// The body's four register arguments and four stack arguments are forwarded.
// Saved registers, SP and RA survive; v0/v1 retain the body's full 64-bit return.
// The scanner uses only caller-saved GPRs and does not touch the FPU or HI/LO.
//
// Validate both cursors before reading the stream: ordered, 8-byte aligned,
// cached RDRAM [0x80000000, 0x80800000], with a maximum span of 0x20000 bytes.
// A full-frame scissor reset is retained. All other matching commands translate
// each 12-bit coordinate by signed 10.2 offsets and saturate to [0,4095].
// 64-bit additions avoid wrapping even for extreme signed parameter values.
// No command opcode, texture selector, UV, texture step or unrelated word changes.
const uint32_t Code[] =
{
    0x27BDFFA0, // addiu sp, sp, -0x60
    0xFFBF0058, // sd ra, 0x58, sp
    0x3C088010, // lui t0, 0x8010
    0x8D09F398, // lw t1, -0xC68, t0
    0xAFA90020, // sw t1, 0x20, sp
    0x8FA80070, // lw t0, 0x70, sp
    0xAFA80010, // sw t0, 0x10, sp
    0x8FA80074, // lw t0, 0x74, sp
    0xAFA80014, // sw t0, 0x14, sp
    0x8FA80078, // lw t0, 0x78, sp
    0xAFA80018, // sw t0, 0x18, sp
    0x8FA8007C, // lw t0, 0x7C, sp
    0xAFA8001C, // sw t0, 0x1C, sp
    // body_call:
    0x0C000000, // jal_body
    0x00000000, // nop
    0xFFA20030, // sd v0, 0x30, sp
    0xFFA30038, // sd v1, 0x38, sp
    0x3C088010, // lui t0, 0x8010
    0x8D09F398, // lw t1, -0xC68, t0
    0x8FA80020, // lw t0, 0x20, sp
    0x01095025, // or t2, t0, t1
    0x314A0007, // andi t2, t2, 0x7
    0x1540006A, // bne t2, zero, finish
    0x00000000, // nop
    0x3C0A8000, // lui t2, 0x8000
    0x010A582B, // sltu t3, t0, t2
    0x15600066, // bne t3, zero, finish
    0x00000000, // nop
    0x0128582B, // sltu t3, t1, t0
    0x15600063, // bne t3, zero, finish
    0x00000000, // nop
    0x3C0A8080, // lui t2, 0x8080
    0x0149582B, // sltu t3, t2, t1
    0x1560005F, // bne t3, zero, finish
    0x00000000, // nop
    0x01285023, // subu t2, t1, t0
    0x3C0B0002, // lui t3, 0x2
    0x016A582B, // sltu t3, t3, t2
    0x1560005A, // bne t3, zero, finish
    0x00000000, // nop
    0x11090058, // beq t0, t1, finish
    0x00000000, // nop
    0x3C0A8007, // lui t2, 0x8007
    0x8D588080, // lw t8, -0x7F80, t2
    0x8D598084, // lw t9, -0x7F7C, t2
    0x8D4E8088, // lw t6, -0x7F78, t2
    0x8D4F808C, // lw t7, -0x7F74, t2
    // loop:
    0x8D0A0000, // lw t2, 0x0, t0
    0x8D0B0004, // lw t3, 0x4, t0
    0x000A6602, // srl t4, t2, 0x18
    0x240D00E4, // addiu t5, zero, 0xE4
    0x118D0014, // beq t4, t5, translate
    0x00000000, // nop
    0x240D00E5, // addiu t5, zero, 0xE5
    0x118D0011, // beq t4, t5, translate
    0x00000000, // nop
    0x240D00F6, // addiu t5, zero, 0xF6
    0x118D000E, // beq t4, t5, translate
    0x00000000, // nop
    0x240D00ED, // addiu t5, zero, 0xED
    0x158D0041, // bne t4, t5, next
    0x00000000, // nop
    0x000A6A00, // sll t5, t2, 0x8
    0x15A00008, // bne t5, zero, translate
    0x00000000, // nop
    0x000B6B02, // srl t5, t3, 0xC
    0x31AD0FFF, // andi t5, t5, 0xFFF
    0x15AE0004, // bne t5, t6, translate
    0x00000000, // nop
    0x316D0FFF, // andi t5, t3, 0xFFF
    0x11AF0037, // beq t5, t7, next
    0x00000000, // nop
    // translate:
    0x000A2302, // srl a0, t2, 0xC
    0x30840FFF, // andi a0, a0, 0xFFF
    0x31450FFF, // andi a1, t2, 0xFFF
    0x0098202D, // daddu a0, a0, t8
    0x00B9282D, // daddu a1, a1, t9
    0x0080302A, // slt a2, a0, zero
    0x10C00002, // beq a2, zero, x0_upper
    0x00000000, // nop
    0x00002025, // or a0, zero, zero
    // x0_upper:
    0x28861000, // slti a2, a0, 0x1000
    0x14C00002, // bne a2, zero, x0_done
    0x00000000, // nop
    0x24040FFF, // addiu a0, zero, 0xFFF
    // x0_done:
    0x00A0302A, // slt a2, a1, zero
    0x10C00002, // beq a2, zero, y0_upper
    0x00000000, // nop
    0x00002825, // or a1, zero, zero
    // y0_upper:
    0x28A61000, // slti a2, a1, 0x1000
    0x14C00002, // bne a2, zero, y0_done
    0x00000000, // nop
    0x24050FFF, // addiu a1, zero, 0xFFF
    // y0_done:
    0x00042300, // sll a0, a0, 0xC
    0x00852025, // or a0, a0, a1
    0x3C07FF00, // lui a3, 0xFF00
    0x01473824, // and a3, t2, a3
    0x00E45025, // or t2, a3, a0
    0xAD0A0000, // sw t2, 0x0, t0
    0x000B2302, // srl a0, t3, 0xC
    0x30840FFF, // andi a0, a0, 0xFFF
    0x31650FFF, // andi a1, t3, 0xFFF
    0x0098202D, // daddu a0, a0, t8
    0x00B9282D, // daddu a1, a1, t9
    0x0080302A, // slt a2, a0, zero
    0x10C00002, // beq a2, zero, x1_upper
    0x00000000, // nop
    0x00002025, // or a0, zero, zero
    // x1_upper:
    0x28861000, // slti a2, a0, 0x1000
    0x14C00002, // bne a2, zero, x1_done
    0x00000000, // nop
    0x24040FFF, // addiu a0, zero, 0xFFF
    // x1_done:
    0x00A0302A, // slt a2, a1, zero
    0x10C00002, // beq a2, zero, y1_upper
    0x00000000, // nop
    0x00002825, // or a1, zero, zero
    // y1_upper:
    0x28A61000, // slti a2, a1, 0x1000
    0x14C00002, // bne a2, zero, y1_done
    0x00000000, // nop
    0x24050FFF, // addiu a1, zero, 0xFFF
    // y1_done:
    0x00042300, // sll a0, a0, 0xC
    0x00852025, // or a0, a0, a1
    0x3C07FF00, // lui a3, 0xFF00
    0x01673824, // and a3, t3, a3
    0x00E45825, // or t3, a3, a0
    0xAD0B0004, // sw t3, 0x4, t0
    // next:
    0x25080008, // addiu t0, t0, 0x8
    0x1509FFAF, // bne t0, t1, loop
    0x00000000, // nop
    // finish:
    0xDFA20030, // ld v0, 0x30, sp
    0xDFA30038, // ld v1, 0x38, sp
    0xDFBF0058, // ld ra, 0x58, sp
    0x03E00008, // jr ra
    0x27BD0060, // addiu sp, sp, 0x60
};
static_assert(CaveStart + sizeof(Code) <= ParamsStart, "RDP wrapper overlaps parameters");
static_assert(MagicAddress + sizeof(uint32_t) <= CaveEnd, "RDP parameters exceed cave");

inline bool BuildImage(std::vector<uint32_t> & Image, uint32_t OriginalBodyAddress,
                       int32_t DxQ, int32_t DyQ, uint32_t WidthQ, uint32_t HeightQ)
{
    if ((OriginalBodyAddress & 3) != 0 || OriginalBodyAddress < 0x80000000 ||
        OriginalBodyAddress >= 0x80800000 ||
        (OriginalBodyAddress >= CaveStart && OriginalBodyAddress < CaveEnd) ||
        WidthQ == 0 || WidthQ > 0xFFF || HeightQ == 0 || HeightQ > 0xFFF)
    {
        return false;
    }
    Image.assign((CaveEnd - CaveStart) / sizeof(uint32_t), 0);
    for (std::size_t i = 0; i < sizeof(Code) / sizeof(Code[0]); i++)
    {
        Image[i] = Code[i];
    }
    Image[(BodyCallAddress - CaveStart) / sizeof(uint32_t)] =
        0x0C000000 | ((OriginalBodyAddress >> 2) & 0x03FFFFFF);
    Image[(DxQAddress - CaveStart) / sizeof(uint32_t)] = (uint32_t)DxQ;
    Image[(DyQAddress - CaveStart) / sizeof(uint32_t)] = (uint32_t)DyQ;
    Image[(WidthQAddress - CaveStart) / sizeof(uint32_t)] = WidthQ;
    Image[(HeightQAddress - CaveStart) / sizeof(uint32_t)] = HeightQ;
    Image[(MagicAddress - CaveStart) / sizeof(uint32_t)] = MagicValue;
    return true;
}
} // namespace JfgHudAlignmentRdp
