#pragma once
#if defined(__amd64__) || defined(_M_X64)

#include <Project64-core/N64System/N64Types.h>
#include <stdint.h>
#include <vector>

class R4300iOp;

// A block compiler for the x64 interpreter.
//
// A block is a run of instructions in one 4 KiB page of RDRAM, reached through
// KSEG0 or KSEG1, up to the first branch or jump and its delay slot. Simple
// opcodes, loads, stores and branches are compiled to x64; every other opcode
// calls the interpreter's own function for it. The compiled code keeps the
// interpreter's order of events: the cycles of the instructions run are taken
// off the timer before any call into the interpreter, which may read it, and
// after a jump a block goes straight on to the next one only where
// R4300iOp::ExecuteOps would not stop either (no timer due, no event pending,
// no permanent loop, an aligned target). Otherwise it returns and the
// interpreter carries on from the same point, so running blocks gives exactly
// the same emulation as interpreting them.
//
// Every block checks on entry that memory still holds its instructions, and
// is compiled again when it does not (game hacks, DMA, loaded save states).
class CInterpreterJit
{
public:
    // Where the compiled code finds the interpreter state
    struct Context
    {
        R4300iOp * Interpreter;
        MIPS_DWORD * GPR;
        MIPS_DWORD * HI;
        MIPS_DWORD * LO;
        uint64_t * ProgramCounter;
        int32_t * NextTimer;
        PIPELINE_STAGE * PipelineStage;
        uint64_t * JumpToLocation;
        bool * TestTimer;
        const bool * DoSomething;
        uint32_t * Opcode;
        const uint32_t * Status;
        float ** FPR_S;
        size_t * const * ReadMap;
        size_t * const * WriteMap;
        uint8_t * const * Rdram;
        const uint32_t * RdramSize;
        bool Force32bit;
    };

    // What Execute ran
    enum Result
    {
        // Nothing: the caller interprets the instruction
        Result_None = -1,
        // Up to the body of the instruction the program counter is on; the
        // caller finishes that instruction
        Result_InstructionBody = 0,
        // Whole instructions: the program counter is on the next one
        Result_Instructions = 1,
    };

    explicit CInterpreterJit(const Context & State);
    ~CInterpreterJit();

    // Runs compiled code from ProgramCounter, at the start of an instruction
    // in the normal pipeline stage
    Result Execute(uint64_t ProgramCounter);
    void Reset(void);

private:
    CInterpreterJit(const CInterpreterJit &) = delete;
    CInterpreterJit & operator=(const CInterpreterJit &) = delete;

    struct Runtime;
    typedef int (*BlockCode)(void);
    struct Block
    {
        // Called from C++; jumps to Inner
        BlockCode Code;
        // Where the previous block jumps to, registers already set up
        const void * Inner;
        uint64_t StartPC;
        uint32_t NumWords;
        // The block's RUNTIME_FUNCTION, so that exceptions and debuggers can
        // walk the stack through compiled code
        uint32_t Unwind[3];
        bool UnwindRegistered;
    };

    Block * Lookup(uint64_t ProgramCounter);
    Block * Compile(uint64_t StartPC, const uint32_t * Code);
    void Release(Block * CompiledBlock);

    Context m_State;
    Runtime * m_Runtime;
    std::vector<Block *> m_Blocks;
    uint8_t * m_BlocksRdram;
    uint32_t m_CountPerOp;
};

#endif
