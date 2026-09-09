#pragma once

#include <cstdint>

namespace JfgFloydHud
{
// The native type-13 line writer draws two green pixels per row. Its fixed
// diagonal stride assumes abs(dx) == abs(dy), which no longer holds after
// widescreen compression. The private 0x40 tag selects an integer DDA while
// retaining the original inclusive row count and saturating green blend.
// Untagged lines and calls outside fxOutputLines retain their original raster.
const uint32_t InitStub = 0x800678CC;
const uint32_t StepStub = 0x80067928;
const uint32_t StepTailStub = 0x80067340;
const uint32_t InitEntry = 0x8006DC90;
const uint32_t InitOriginal = 0x000B2840;
const uint32_t InitDelayOriginal = 0x94F80000;
const uint32_t InitJump = 0x08019E33;
const uint32_t StepEntry = 0x8006DD04;
const uint32_t StepOriginal = 0x1540FFE3;
const uint32_t StepDelayOriginal = 0x254AFFFF;
const uint32_t StepBranch = 0x1540E708;
const uint32_t GuardStub = 0x800678C4;
const uint32_t GuardCode[] = { 0x03E00008, 0x00000000 };

// US ROM words from diCpuTraceMallocFault, including its unreachable padding
// and epilogue. A recognized patched save state must restore this image rather
// than preserve our helpers as the supposedly original diagnostic body.
const uint32_t OriginalDiagnosticCode[] =
{
    0x27BDFDB8, 0xAFBF0014, 0xAFA40248, 0xAFA5024C,
    0xAFA60250, 0x24050230, 0x0C0260D8, 0x27A40018,
    0x8FAE0248, 0x8FA8024C, 0x8FA90250, 0xAFAE0134,
    0x240C0000, 0x240EFFFF, 0x000857C3, 0xAFAA0050,
    0xAFAC0058, 0xAFAE0138, 0x27A40018, 0xAFA80054,
    0x0C019E65, 0xAFA9005C, 0x0C026284, 0x00000000,
    0x1000FFFF, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x8FBF0014,
    0x27BD0248, 0x03E00008, 0x00000000,
};

struct WordPatch
{
    uint32_t Address;
    uint32_t Original;
    uint32_t Replacement;
};

// Retire the malloc-fault diagnostic before its body is used as code storage.
// Retain the untouched loop delay as a signature so the branch cannot be
// installed over a foreign renderer with a different counter update.
const WordPatch FixedPatches[] =
{
    { 0x800678C4, 0x27BDFDB8, 0x03E00008 },
    { 0x800678C8, 0xAFBF0014, 0x00000000 },
    { 0x8006DC90, 0x000B2840, 0x08019E33 },
    { 0x8006DC94, 0x94F80000, 0x94F80000 },
    { 0x8006DD04, 0x1540FFE3, 0x1540E708 },
    { 0x8006DD08, 0x254AFFFF, 0x254AFFFF },
};

// At entry t2 = abs(dy), because the stock inclusive count was decremented
// in the branch delay slot at 0x8006DC8C. s0/s1 are callee-saved and restored
// by the original epilogue; t3/t4/t5 are no longer endpoint inputs afterwards.
// s1 = 0 selects the unmodified constant stride on subsequent iterations.
const uint32_t InitCode[] =
{
    0x8FAE001C, // lw t6,0x1c(sp)
    0x3C0F8007, // lui t7,0x8007
    0x25EFE2AC, // addiu t7,t7,0xe2ac
    0x15CF000F, // bne t6,t7,stock
    0x00000000, // nop
    0x8FAE00F0, // lw t6,0xf0(sp)
    0x91CE0007, // lbu t6,7(t6)
    0x31CE004F, // andi t6,t6,0x4f
    0x240F004D, // addiu t7,zero,0x4d
    0x15CF0009, // bne t6,t7,stock
    0x00000000, // nop
    0x11400007, // beq t2,zero,stock
    0x01908023, // subu s0,t4,s0
    0x00106FC3, // sra t5,s0,31
    0x020D6026, // xor t4,s0,t5
    0x018D6023, // subu t4,t4,t5
    0x01408825, // move s1,t2
    0x08019E4A, // j step
    0x00005825, // move t3,zero
    0x00008825, // stock: move s1,zero
    0x000B2840, // sll a1,t3,1
    0x0801B725, // j 0x8006dc94
    0x00000000, // nop
};

// Advance exactly abs(dx) columns across abs(dy) rows. The tagged ring only
// uses vertical and Y-major diagonal segments; horizontal strokes stay native.
const uint32_t StepCode[] =
{
    0x1220FFFB, // beq s1,zero,stock
    0x00000000, // nop
    0x016C5821, // addu t3,t3,t4
    0x0171702A, // slt t6,t3,s1
    0x8FA5003C, // lw a1,0x3c(sp)
    0x15C0FE83, // bne t6,zero,return
    0x00052840, // sll a1,a1,1
    0x01715823, // subu t3,t3,s1
    0x08019CD0, // j step_tail
    0x001077C3, // sra t6,s0,31
};

// This tail occupies the retired digital-number advance trampoline. Its
// legacy hook must be removed before the cave image can install these words.
const uint32_t StepTailCode[] =
{
    0x35CE0001, // ori t6,t6,1
    0x000E7040, // sll t6,t6,1
    0x00AE2821, // addu a1,a1,t6
    0x0801B725, // return: j 0x8006dc94
    0x00000000, // nop
};

static_assert(InitStub + sizeof(InitCode) == StepStub, "Floyd line helpers overlap");
static_assert(GuardStub + sizeof(OriginalDiagnosticCode) == 0x80067950, "Floyd diagnostic image has the wrong extent");
static_assert(StepStub + sizeof(StepCode) == 0x80067950, "Floyd line helper leaves its cave");
static_assert(StepTailStub + sizeof(StepTailCode) <= 0x80067360, "Floyd line tail leaves its cave");
}
