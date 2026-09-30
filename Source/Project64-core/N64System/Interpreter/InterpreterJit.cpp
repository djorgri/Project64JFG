#include "stdafx.h"
#if defined(__amd64__) || defined(_M_X64)

#include <Project64-core/N64System/Interpreter/InterpreterJit.h>
#include <Project64-core/N64System/Interpreter/InterpreterOps.h>
#include <Project64-core/N64System/Mips/R4300iInstruction.h>
#include <Project64-core/N64System/Recompiler/asmjit.h>
#include <Project64-core/Settings/GameSettings.h>
#include <stddef.h>
#include <string.h>
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace asmjit;

// Registers the compiled code keeps from block to block (all callee-saved):
//   rbx  the GPRs           rsi  the interpreter     rdi  the program counter
//   r12  the timer count    r13  the read map        r14  the write map
//   r15  the pipeline stage
//
// A block returns 0 (CInterpreterJit::Result_InstructionBody) or 1
// (Result_Instructions), or 2 when the block it was entering no longer
// matches memory: then nothing of that block ran and the program counter is
// on its first instruction.
namespace
{
const uint32_t MaxBlockInstructions = 256;
const uint32_t Cop1UsableBit = 0x20000000;
const int BlockInvalid = 2;

// Every block starts with the same prologue, push rbx, rsi, rdi, r12, r13,
// r14, r15 then sub rsp, 32 (15 bytes), described once for the unwinder: a
// block entered from another one runs in that block's identical frame.
const uint8_t PrologueBytes[15] = {0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
const uint8_t PrologueUnwindInfo[20] = {
    0x01, 15, 8, 0x00, // version 1, prologue size, unwind codes, no frame register
    15, 0x32,          // sub rsp, 32
    11, 0xF0,          // push r15
    9, 0xE0,           // push r14
    7, 0xD0,           // push r13
    5, 0xC0,           // push r12
    3, 0x70,           // push rdi
    2, 0x60,           // push rsi
    1, 0x30,           // push rbx
};

class JitErrors : public ErrorHandler
{
public:
    bool Failed = false;

    void handleError(Error /*err*/, const char * /*message*/, BaseEmitter * /*origin*/) override
    {
        Failed = true;
    }
};

x86::Mem Gpr(uint32_t Reg)
{
    return x86::qword_ptr(x86::rbx, (int32_t)(Reg * 8));
}

x86::Mem GprLo(uint32_t Reg)
{
    return x86::dword_ptr(x86::rbx, (int32_t)(Reg * 8));
}

uint64_t Address(const void * Pointer)
{
    return (uint64_t)(uintptr_t)Pointer;
}

// A program counter as the sign-extended 32-bit immediate x64 takes
Imm PCImm(uint64_t PC)
{
    return imm((int32_t)(uint32_t)PC);
}

enum BranchCondition
{
    Branch_Always,
    Branch_Equal,
    Branch_NotEqual,
    Branch_LessEqualZero,
    Branch_GreaterZero,
    Branch_LessZero,
    Branch_GreaterEqualZero,
};

struct BranchInfo
{
    BranchCondition Condition;
    bool Likely;
    bool Link;
    uint64_t Target;
};

// The branches and jumps compiled here: their target is known and they only
// set the pipeline stage and the jump target (and $ra for JAL)
bool NativeBranch(const R4300iOpcode & Opcode, uint64_t PC, BranchInfo & Info)
{
    uint64_t Relative = PC + ((int64_t)(int16_t)Opcode.offset << 2) + 4;
    Info.Likely = false;
    Info.Link = false;
    Info.Target = Relative;
    switch (Opcode.op)
    {
    case R4300i_J:
    case R4300i_JAL:
        Info.Condition = Branch_Always;
        Info.Link = Opcode.op == R4300i_JAL;
        Info.Target = (PC & 0xFFFFFFFFF0000000ULL) + ((uint64_t)Opcode.target << 2);
        break;
    case R4300i_BEQ: Info.Condition = Branch_Equal; break;
    case R4300i_BNE: Info.Condition = Branch_NotEqual; break;
    case R4300i_BLEZ: Info.Condition = Branch_LessEqualZero; break;
    case R4300i_BGTZ: Info.Condition = Branch_GreaterZero; break;
    case R4300i_BEQL: Info.Condition = Branch_Equal; Info.Likely = true; break;
    case R4300i_BNEL: Info.Condition = Branch_NotEqual; Info.Likely = true; break;
    case R4300i_BLEZL: Info.Condition = Branch_LessEqualZero; Info.Likely = true; break;
    case R4300i_BGTZL: Info.Condition = Branch_GreaterZero; Info.Likely = true; break;
    case R4300i_REGIMM:
        switch (Opcode.rt)
        {
        case R4300i_REGIMM_BLTZ: Info.Condition = Branch_LessZero; break;
        case R4300i_REGIMM_BGEZ: Info.Condition = Branch_GreaterEqualZero; break;
        case R4300i_REGIMM_BLTZL: Info.Condition = Branch_LessZero; Info.Likely = true; break;
        case R4300i_REGIMM_BGEZL: Info.Condition = Branch_GreaterEqualZero; Info.Likely = true; break;
        default: return false;
        }
        break;
    default:
        return false;
    }
    // A branch to itself is a permanent loop the interpreter detects
    return Info.Target != PC;
}

class BlockCompiler
{
public:
    BlockCompiler(x86::Assembler & Assembler, const CInterpreterJit::Context & State, uint32_t CountPerOp, void * const * Table, uint32_t TableWords) :
        a(Assembler),
        m_State(State),
        m_CountPerOp(CountPerOp),
        m_Table(Table),
        m_TableWords(TableWords),
        m_Pending(0),
        m_Code(nullptr),
        m_Count(0),
        m_Index(0),
        m_Inner(Assembler.newLabel()),
        m_Epilogue(Assembler.newLabel()),
        m_ExitBody(Assembler.newLabel()),
        m_ExitInstructions(Assembler.newLabel()),
        m_Invalid(Assembler.newLabel()),
        m_UnwindInfo(Assembler.newLabel())
    {
    }

    const Label & Inner() const
    {
        return m_Inner;
    }

    void Prologue(const uint32_t * Code, uint32_t Count)
    {
        m_Code = Code;
        m_Count = Count;
        a.push(x86::rbx);
        a.push(x86::rsi);
        a.push(x86::rdi);
        a.push(x86::r12);
        a.push(x86::r13);
        a.push(x86::r14);
        a.push(x86::r15);
        a.sub(x86::rsp, 32);
        a.mov(x86::rbx, imm(Address(m_State.GPR)));
        a.mov(x86::rsi, imm(Address(m_State.Interpreter)));
        a.mov(x86::rdi, imm(Address(m_State.ProgramCounter)));
        a.mov(x86::r12, imm(Address(m_State.NextTimer)));
        a.mov(x86::r13, imm(Address(m_State.ReadMap)));
        a.mov(x86::r13, x86::qword_ptr(x86::r13));
        a.mov(x86::r14, imm(Address(m_State.WriteMap)));
        a.mov(x86::r14, x86::qword_ptr(x86::r14));
        a.mov(x86::r15, imm(Address(m_State.PipelineStage)));

        // Entered from the previous block with the registers above set
        a.bind(m_Inner);
        a.mov(x86::rax, imm(Address(Code)));
        uint32_t i = 0;
        for (; i + 1 < Count; i += 2)
        {
            a.mov(x86::rcx, imm((uint64_t)Code[i] | ((uint64_t)Code[i + 1] << 32)));
            a.cmp(x86::qword_ptr(x86::rax, (int32_t)(i * 4)), x86::rcx);
            a.jne(m_Invalid);
        }
        if (i < Count)
        {
            a.cmp(x86::dword_ptr(x86::rax, (int32_t)(i * 4)), Code[i]);
            a.jne(m_Invalid);
        }
    }

    void Epilogue()
    {
        a.bind(m_ExitBody);
        a.xor_(x86::eax, x86::eax);
        a.jmp(m_Epilogue);
        a.bind(m_ExitInstructions);
        a.mov(x86::eax, 1);
        a.jmp(m_Epilogue);
        a.bind(m_Invalid);
        a.mov(x86::eax, BlockInvalid);
        a.bind(m_Epilogue);
        a.add(x86::rsp, 32);
        a.pop(x86::r15);
        a.pop(x86::r14);
        a.pop(x86::r13);
        a.pop(x86::r12);
        a.pop(x86::rdi);
        a.pop(x86::rsi);
        a.pop(x86::rbx);
        a.ret();
        for (size_t i = 0; i < m_SlowPaths.size(); i++)
        {
            EmitSlowPath(m_SlowPaths[i]);
        }
        a.align(AlignMode::kData, 4);
        a.bind(m_UnwindInfo);
        a.embed(PrologueUnwindInfo, sizeof(PrologueUnwindInfo));
    }

    // Where the unwind data follows the code
    const Label & UnwindInfo() const
    {
        return m_UnwindInfo;
    }

    // One instruction the interpreter completed, not yet taken off the timer
    void InstructionDone()
    {
        m_Pending += 1;
    }

    // Which instruction of the block is being compiled
    void SetIndex(uint32_t Index)
    {
        m_Index = Index;
    }

    void Instruction(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot)
    {
        if (!Native(Opcode, PC, Handler, DelaySlot))
        {
            CallInterpreter(Opcode, PC, Handler, DelaySlot);
        }
    }

    // After the last instruction of a page (or of a long block), in the
    // normal stage: the interpreter would just go on with the next one
    void EndOfPage(uint64_t LastPC)
    {
        InstructionDone();
        FlushCycles();
        a.mov(x86::rax, PCImm(LastPC + 4));
        a.mov(x86::qword_ptr(x86::rdi), x86::rax);
        Chain();
    }

    // The branch runs in the interpreter; the delay slot, when the block has
    // it, runs here with the stage the interpreter would have moved to.
    void InterpretedBranch(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, const R4300iOpcode * DelaySlot, uint64_t DelaySlotHandler)
    {
        FlushCycles();
        SetPC(PC);
        Call(Opcode, Handler);
        if (DelaySlot == nullptr)
        {
            a.jmp(m_ExitBody);
            return;
        }
        Label NotDelaySlot = a.newLabel(), RunDelaySlot = a.newLabel(), BranchDone = a.newLabel();
        a.mov(x86::eax, x86::dword_ptr(x86::r15));
        a.cmp(x86::eax, (uint32_t)PIPELINE_STAGE_DELAY_SLOT);
        a.jne(NotDelaySlot);
        a.mov(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_JUMP);
        a.jmp(RunDelaySlot);
        a.bind(NotDelaySlot);
        a.cmp(x86::eax, (uint32_t)PIPELINE_STAGE_PERMLOOP_DO_DELAY);
        a.jne(BranchDone);
        a.mov(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_PERMLOOP_DELAY_DONE);
        a.bind(RunDelaySlot);
        InstructionDone();
        Instruction(*DelaySlot, PC + 4, DelaySlotHandler, true);
        JumpTail(PC + 4);
        // A likely branch not taken: the jump is to after the delay slot
        a.bind(BranchDone);
        m_Pending = 0;
        JumpTail(PC);
    }

    void CompiledBranch(const R4300iOpcode & Opcode, uint64_t PC, const BranchInfo & Info, const R4300iOpcode & DelaySlot, uint64_t DelaySlotHandler)
    {
        const uint32_t PendingBefore = m_Pending;
        Label NotTaken = a.newLabel();
        a.mov(x86::r8, imm(Address(m_State.JumpToLocation)));
        if (Info.Condition == Branch_Always)
        {
            a.mov(x86::qword_ptr(x86::r8), PCImm(Info.Target));
        }
        else
        {
            if (Info.Condition == Branch_Equal || Info.Condition == Branch_NotEqual)
            {
                a.mov(x86::rax, Gpr(Opcode.rs));
                a.cmp(x86::rax, Gpr(Opcode.rt));
            }
            else
            {
                a.cmp(Gpr(Opcode.rs), 0);
            }
            if (Info.Likely)
            {
                switch (Info.Condition)
                {
                case Branch_Equal: a.jne(NotTaken); break;
                case Branch_NotEqual: a.je(NotTaken); break;
                case Branch_LessEqualZero: a.jg(NotTaken); break;
                case Branch_GreaterZero: a.jle(NotTaken); break;
                case Branch_LessZero: a.jge(NotTaken); break;
                default: a.jl(NotTaken); break;
                }
                a.mov(x86::qword_ptr(x86::r8), PCImm(Info.Target));
            }
            else
            {
                a.mov(x86::rcx, PCImm(Info.Target));
                a.mov(x86::rdx, PCImm(PC + 8));
                switch (Info.Condition)
                {
                case Branch_Equal: a.cmove(x86::rdx, x86::rcx); break;
                case Branch_NotEqual: a.cmovne(x86::rdx, x86::rcx); break;
                case Branch_LessEqualZero: a.cmovle(x86::rdx, x86::rcx); break;
                case Branch_GreaterZero: a.cmovg(x86::rdx, x86::rcx); break;
                case Branch_LessZero: a.cmovl(x86::rdx, x86::rcx); break;
                default: a.cmovge(x86::rdx, x86::rcx); break;
                }
                a.mov(x86::qword_ptr(x86::r8), x86::rdx);
            }
        }
        if (Info.Link)
        {
            a.mov(Gpr(31), PCImm(PC + 8));
        }
        // The branch set the delay slot stage, which its end turns into a jump
        a.mov(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_JUMP);
        InstructionDone();
        Instruction(DelaySlot, PC + 4, DelaySlotHandler, true);
        JumpTail(PC + 4);

        if (Info.Likely)
        {
            // Not taken: straight to after the delay slot
            a.bind(NotTaken);
            m_Pending = PendingBefore;
            a.mov(x86::qword_ptr(x86::r8), PCImm(PC + 8));
            a.mov(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_JUMP);
            JumpTail(PC);
        }
    }

private:
    struct SlowPath
    {
        Label Entry;
        Label Resume;
        R4300iOpcode Opcode;
        uint64_t PC;
        uint64_t Handler;
        uint32_t Pending;
        uint32_t Next;
        bool DelaySlot;
    };

    void FlushCycles()
    {
        if (m_Pending != 0)
        {
            a.sub(x86::dword_ptr(x86::r12), m_Pending * m_CountPerOp);
            m_Pending = 0;
        }
    }

    void SetPC(uint64_t PC)
    {
        a.mov(x86::qword_ptr(x86::rdi), PCImm(PC));
    }

    void Call(const R4300iOpcode & Opcode, uint64_t Handler)
    {
        a.mov(x86::rax, imm(Address(m_State.Opcode)));
        a.mov(x86::dword_ptr(x86::rax), Opcode.Value);
        a.mov(x86::rcx, x86::rsi);
        a.mov(x86::rax, imm(Handler));
        a.call(x86::rax);
        a.mov(x86::qword_ptr(x86::rbx), 0);
    }

    void CallInterpreter(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot)
    {
        FlushCycles();
        SetPC(PC);
        Call(Opcode, Handler);
        if (!DelaySlot)
        {
            // An exception, or an instruction that changes the flow
            a.cmp(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_NORMAL);
            a.jne(m_ExitBody);
            if (Opcode.op >= R4300i_LB)
            {
                RecheckRest(m_Index + 1);
            }
        }
    }

    // A memory access through the interpreter may have written code: a DMA,
    // or the game hacks, which patch code when the controllers are read. The
    // interpreter would run the new instructions, so leave if the rest of the
    // block changed (the cycles and the program counter are up to date here).
    void RecheckRest(uint32_t Next)
    {
        if (Next >= m_Count)
        {
            return;
        }
        a.mov(x86::rax, imm(Address(m_Code)));
        uint32_t i = Next;
        for (; i + 1 < m_Count; i += 2)
        {
            a.mov(x86::rcx, imm((uint64_t)m_Code[i] | ((uint64_t)m_Code[i + 1] << 32)));
            a.cmp(x86::qword_ptr(x86::rax, (int32_t)(i * 4)), x86::rcx);
            a.jne(m_ExitBody);
        }
        if (i < m_Count)
        {
            a.cmp(x86::dword_ptr(x86::rax, (int32_t)(i * 4)), m_Code[i]);
            a.jne(m_ExitBody);
        }
    }

    // The end of the last instruction, in the jump stage: what ExecuteOps
    // does, when it would neither run the timers nor the events
    void JumpTail(uint64_t LastPC)
    {
        Label Check = a.newLabel(), Continue = a.newLabel();
        FlushCycles();
        SetPC(LastPC);
        a.cmp(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_JUMP);
        a.jne(m_ExitBody);
        a.mov(x86::r8, imm(Address(m_State.JumpToLocation)));
        a.mov(x86::rax, x86::qword_ptr(x86::r8));
        a.test(x86::al, 3);
        a.jnz(m_ExitBody);
        a.mov(x86::r9, imm(Address(m_State.TestTimer)));
        a.mov(x86::rcx, PCImm(LastPC));
        a.cmp(x86::rax, x86::rcx);
        a.jb(Check);
        a.cmp(x86::byte_ptr(x86::r9), 0);
        a.je(Continue);
        a.bind(Check);
        a.mov(x86::ecx, x86::dword_ptr(x86::r12));
        a.sub(x86::ecx, m_CountPerOp);
        a.js(m_ExitBody);
        a.mov(x86::rdx, imm(Address(m_State.DoSomething)));
        a.cmp(x86::byte_ptr(x86::rdx), 0);
        a.jne(m_ExitBody);
        a.mov(x86::byte_ptr(x86::r9), 0);
        a.bind(Continue);
        a.sub(x86::dword_ptr(x86::r12), m_CountPerOp);
        a.mov(x86::qword_ptr(x86::rdi), x86::rax);
        a.mov(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_NORMAL);
        Chain();
    }

    // rax: the program counter, whole instructions done. Goes on in the block
    // compiled for it, or returns to the interpreter.
    void Chain()
    {
        a.movsxd(x86::rcx, x86::eax);
        a.cmp(x86::rcx, x86::rax);
        a.jne(m_ExitInstructions);
        a.mov(x86::ecx, x86::eax);
        a.and_(x86::ecx, 0xC0000000);
        a.cmp(x86::ecx, 0x80000000);
        a.jne(m_ExitInstructions);
        a.mov(x86::ecx, x86::eax);
        a.and_(x86::ecx, 0x1FFFFFFC);
        a.shr(x86::ecx, 2);
        a.cmp(x86::ecx, m_TableWords);
        a.jae(m_ExitInstructions);
        a.mov(x86::rdx, imm(Address(m_Table)));
        a.mov(x86::rdx, x86::qword_ptr(x86::rdx, x86::rcx, 3));
        a.test(x86::rdx, x86::rdx);
        a.jz(m_ExitInstructions);
        a.cmp(x86::qword_ptr(x86::rdx, (int32_t)offsetof(CInterpreterJitBlockLayout, StartPC)), x86::rax);
        a.jne(m_ExitInstructions);
        a.jmp(x86::qword_ptr(x86::rdx, (int32_t)offsetof(CInterpreterJitBlockLayout, Inner)));
    }

public:
    // Mirror of CInterpreterJit::Block, which is private
    struct CInterpreterJitBlockLayout
    {
        void * Code;
        const void * Inner;
        uint64_t StartPC;
        uint32_t NumWords;
    };

private:
    // The interpreter's own function when the fast path cannot do the access.
    // Both paths meet with the same cycles still owed to the timer.
    void EmitSlowPath(const SlowPath & Path)
    {
        a.bind(Path.Entry);
        if (Path.Pending != 0)
        {
            a.sub(x86::dword_ptr(x86::r12), Path.Pending * m_CountPerOp);
        }
        SetPC(Path.PC);
        Call(Path.Opcode, Path.Handler);
        if (!Path.DelaySlot)
        {
            a.cmp(x86::dword_ptr(x86::r15), (uint32_t)PIPELINE_STAGE_NORMAL);
            a.jne(m_ExitBody);
            RecheckRest(Path.Next);
        }
        if (Path.Pending != 0)
        {
            a.add(x86::dword_ptr(x86::r12), Path.Pending * m_CountPerOp);
        }
        a.jmp(Path.Resume);
    }

    Label AddSlowPath(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot, Label & Resume)
    {
        SlowPath Path;
        Path.Entry = a.newLabel();
        Path.Resume = a.newLabel();
        Path.Opcode = Opcode;
        Path.PC = PC;
        Path.Handler = Handler;
        Path.Pending = m_Pending;
        Path.Next = m_Index + 1;
        Path.DelaySlot = DelaySlot;
        m_SlowPaths.push_back(Path);
        Resume = Path.Resume;
        return Path.Entry;
    }

    // rax = the 32-bit address, rdx = its page in the read or write map, as
    // the start of CMipsMemoryVM's *_Memory functions
    void DirectAccess(const R4300iOpcode & Opcode, uint32_t AlignMask, bool Write, const Label & Slow)
    {
        a.mov(x86::rax, Gpr(Opcode.base));
        if ((int16_t)Opcode.offset != 0)
        {
            a.add(x86::rax, (int32_t)(int16_t)Opcode.offset);
        }
        a.movsxd(x86::rdx, x86::eax);
        a.cmp(x86::rdx, x86::rax);
        a.jne(Slow);
        if (AlignMask != 0)
        {
            a.test(x86::al, AlignMask);
            a.jnz(Slow);
        }
        a.mov(x86::ecx, x86::eax);
        a.shr(x86::ecx, 12);
        a.mov(x86::rdx, x86::qword_ptr(Write ? x86::r14 : x86::r13, x86::rcx, 3));
        a.cmp(x86::rdx, -1);
        a.je(Slow);
        a.mov(x86::eax, x86::eax);
    }

    void TestCop1Usable(const Label & Slow)
    {
        a.mov(x86::rcx, imm(Address(m_State.Status)));
        a.test(x86::dword_ptr(x86::rcx), Cop1UsableBit);
        a.jz(Slow);
    }

    bool Load(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot)
    {
        Label Resume;
        Label Slow = AddSlowPath(Opcode, PC, Handler, DelaySlot, Resume);
        bool Cop1 = Opcode.op == R4300i_LWC1;
        if (Cop1)
        {
            TestCop1Usable(Slow);
        }
        switch (Opcode.op)
        {
        case R4300i_LB:
        case R4300i_LBU:
            DirectAccess(Opcode, 0, false, Slow);
            a.xor_(x86::eax, 3);
            if (Opcode.op == R4300i_LB)
            {
                a.movsx(x86::rcx, x86::byte_ptr(x86::rdx, x86::rax));
            }
            else
            {
                a.movzx(x86::ecx, x86::byte_ptr(x86::rdx, x86::rax));
            }
            break;
        case R4300i_LH:
        case R4300i_LHU:
            DirectAccess(Opcode, 1, false, Slow);
            a.xor_(x86::eax, 2);
            if (Opcode.op == R4300i_LH)
            {
                a.movsx(x86::rcx, x86::word_ptr(x86::rdx, x86::rax));
            }
            else
            {
                a.movzx(x86::ecx, x86::word_ptr(x86::rdx, x86::rax));
            }
            break;
        case R4300i_LW:
            DirectAccess(Opcode, 3, false, Slow);
            a.movsxd(x86::rcx, x86::dword_ptr(x86::rdx, x86::rax));
            break;
        default: // LWU, LWC1
            DirectAccess(Opcode, 3, false, Slow);
            a.mov(x86::ecx, x86::dword_ptr(x86::rdx, x86::rax));
            break;
        }
        if (Cop1)
        {
            a.mov(x86::rax, imm(Address(&m_State.FPR_S[Opcode.ft])));
            a.mov(x86::rax, x86::qword_ptr(x86::rax));
            a.mov(x86::dword_ptr(x86::rax), x86::ecx);
        }
        else if (Opcode.rt != 0)
        {
            a.mov(Gpr(Opcode.rt), x86::rcx);
        }
        a.bind(Resume);
        return true;
    }

    bool Store(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot)
    {
        Label Resume;
        Label Slow = AddSlowPath(Opcode, PC, Handler, DelaySlot, Resume);
        if (Opcode.op == R4300i_SWC1)
        {
            TestCop1Usable(Slow);
        }
        switch (Opcode.op)
        {
        case R4300i_SB:
            DirectAccess(Opcode, 0, true, Slow);
            a.xor_(x86::eax, 3);
            a.mov(x86::ecx, GprLo(Opcode.rt));
            a.mov(x86::byte_ptr(x86::rdx, x86::rax), x86::cl);
            break;
        case R4300i_SH:
            DirectAccess(Opcode, 1, true, Slow);
            a.xor_(x86::eax, 2);
            a.mov(x86::ecx, GprLo(Opcode.rt));
            a.mov(x86::word_ptr(x86::rdx, x86::rax), x86::cx);
            break;
        case R4300i_SW:
            DirectAccess(Opcode, 3, true, Slow);
            a.mov(x86::ecx, GprLo(Opcode.rt));
            a.mov(x86::dword_ptr(x86::rdx, x86::rax), x86::ecx);
            break;
        default: // SWC1
            DirectAccess(Opcode, 3, true, Slow);
            a.mov(x86::rcx, imm(Address(&m_State.FPR_S[Opcode.ft])));
            a.mov(x86::rcx, x86::qword_ptr(x86::rcx));
            a.mov(x86::ecx, x86::dword_ptr(x86::rcx));
            a.mov(x86::dword_ptr(x86::rdx, x86::rax), x86::ecx);
            break;
        }
        a.bind(Resume);
        return true;
    }

    void StoreResult32(uint32_t Reg)
    {
        a.movsxd(x86::rax, x86::eax);
        a.mov(Gpr(Reg), x86::rax);
    }

    void SetCondition(uint32_t Reg, bool Unsigned)
    {
        if (Unsigned)
        {
            a.setb(x86::cl);
        }
        else
        {
            a.setl(x86::cl);
        }
        a.movzx(x86::ecx, x86::cl);
        a.mov(Gpr(Reg), x86::rcx);
    }

    void MoveToHiLo(const MIPS_DWORD * HiLo, const x86::Gp & Value)
    {
        a.mov(x86::r8, imm(Address(HiLo)));
        a.mov(x86::qword_ptr(x86::r8), Value);
    }

    bool Special(const R4300iOpcode & Opcode)
    {
        uint32_t rd = Opcode.rd;
        switch (Opcode.funct)
        {
        case R4300i_SPECIAL_SLL:
            if (rd != 0)
            {
                a.mov(x86::eax, GprLo(Opcode.rt));
                if (Opcode.sa != 0)
                {
                    a.shl(x86::eax, Opcode.sa);
                }
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_SRL:
            if (rd != 0)
            {
                a.mov(x86::eax, GprLo(Opcode.rt));
                if (Opcode.sa != 0)
                {
                    a.shr(x86::eax, Opcode.sa);
                }
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_SRA:
            if (rd != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rt));
                if (Opcode.sa != 0)
                {
                    a.sar(x86::rax, Opcode.sa);
                }
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_SLLV:
        case R4300i_SPECIAL_SRLV:
            if (rd != 0)
            {
                a.mov(x86::ecx, GprLo(Opcode.rs));
                a.mov(x86::eax, GprLo(Opcode.rt));
                if (Opcode.funct == R4300i_SPECIAL_SLLV)
                {
                    a.shl(x86::eax, x86::cl);
                }
                else
                {
                    a.shr(x86::eax, x86::cl);
                }
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_SRAV:
            if (rd != 0)
            {
                a.mov(x86::ecx, GprLo(Opcode.rs));
                a.and_(x86::ecx, 0x1F);
                a.mov(x86::rax, Gpr(Opcode.rt));
                a.sar(x86::rax, x86::cl);
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_MFHI:
        case R4300i_SPECIAL_MFLO:
            if (rd != 0)
            {
                a.mov(x86::rax, imm(Address(Opcode.funct == R4300i_SPECIAL_MFHI ? m_State.HI : m_State.LO)));
                a.mov(x86::rax, x86::qword_ptr(x86::rax));
                a.mov(Gpr(rd), x86::rax);
            }
            return true;
        case R4300i_SPECIAL_MTHI:
        case R4300i_SPECIAL_MTLO:
            a.mov(x86::rax, Gpr(Opcode.rs));
            MoveToHiLo(Opcode.funct == R4300i_SPECIAL_MTHI ? m_State.HI : m_State.LO, x86::rax);
            return true;
        case R4300i_SPECIAL_MULT:
        case R4300i_SPECIAL_MULTU:
            if (Opcode.funct == R4300i_SPECIAL_MULT)
            {
                a.movsxd(x86::rax, GprLo(Opcode.rs));
                a.movsxd(x86::rcx, GprLo(Opcode.rt));
            }
            else
            {
                a.mov(x86::eax, GprLo(Opcode.rs));
                a.mov(x86::ecx, GprLo(Opcode.rt));
            }
            a.imul(x86::rax, x86::rcx);
            a.movsxd(x86::rdx, x86::eax);
            MoveToHiLo(m_State.LO, x86::rdx);
            a.shr(x86::rax, 32);
            a.movsxd(x86::rax, x86::eax);
            MoveToHiLo(m_State.HI, x86::rax);
            return true;
        case R4300i_SPECIAL_ADDU:
        case R4300i_SPECIAL_SUBU:
            if (rd != 0)
            {
                a.mov(x86::eax, GprLo(Opcode.rs));
                if (Opcode.funct == R4300i_SPECIAL_ADDU)
                {
                    a.add(x86::eax, GprLo(Opcode.rt));
                }
                else
                {
                    a.sub(x86::eax, GprLo(Opcode.rt));
                }
                StoreResult32(rd);
            }
            return true;
        case R4300i_SPECIAL_AND:
        case R4300i_SPECIAL_OR:
        case R4300i_SPECIAL_XOR:
        case R4300i_SPECIAL_NOR:
        case R4300i_SPECIAL_DADDU:
            if (rd != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rs));
                switch (Opcode.funct)
                {
                case R4300i_SPECIAL_AND: a.and_(x86::rax, Gpr(Opcode.rt)); break;
                case R4300i_SPECIAL_XOR: a.xor_(x86::rax, Gpr(Opcode.rt)); break;
                case R4300i_SPECIAL_DADDU: a.add(x86::rax, Gpr(Opcode.rt)); break;
                default: a.or_(x86::rax, Gpr(Opcode.rt)); break;
                }
                if (Opcode.funct == R4300i_SPECIAL_NOR)
                {
                    a.not_(x86::rax);
                }
                a.mov(Gpr(rd), x86::rax);
            }
            return true;
        case R4300i_SPECIAL_SLT:
        case R4300i_SPECIAL_SLTU:
            if (rd != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rs));
                a.cmp(x86::rax, Gpr(Opcode.rt));
                SetCondition(rd, Opcode.funct == R4300i_SPECIAL_SLTU);
            }
            return true;
        case R4300i_SPECIAL_DSLL32:
        case R4300i_SPECIAL_DSRA32:
            if (rd != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rt));
                if (Opcode.funct == R4300i_SPECIAL_DSLL32)
                {
                    a.shl(x86::rax, Opcode.sa + 32);
                }
                else
                {
                    a.sar(x86::rax, Opcode.sa + 32);
                }
                a.mov(Gpr(rd), x86::rax);
            }
            return true;
        }
        return false;
    }

    bool Native(const R4300iOpcode & Opcode, uint64_t PC, uint64_t Handler, bool DelaySlot)
    {
        uint32_t rt = Opcode.rt;
        switch (Opcode.op)
        {
        case R4300i_SPECIAL:
            return Special(Opcode);
        case R4300i_ADDIU:
            if (rt != 0)
            {
                a.mov(x86::eax, GprLo(Opcode.rs));
                a.add(x86::eax, (int32_t)(int16_t)Opcode.immediate);
                StoreResult32(rt);
            }
            return true;
        case R4300i_DADDIU:
            if (rt != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rs));
                a.add(x86::rax, (int32_t)(int16_t)Opcode.immediate);
                a.mov(Gpr(rt), x86::rax);
            }
            return true;
        case R4300i_SLTI:
        case R4300i_SLTIU:
            if (rt != 0)
            {
                a.cmp(Gpr(Opcode.rs), (int32_t)(int16_t)Opcode.immediate);
                SetCondition(rt, Opcode.op == R4300i_SLTIU);
            }
            return true;
        case R4300i_ANDI:
        case R4300i_ORI:
        case R4300i_XORI:
            if (rt != 0)
            {
                a.mov(x86::rax, Gpr(Opcode.rs));
                switch (Opcode.op)
                {
                case R4300i_ANDI: a.and_(x86::rax, (int32_t)Opcode.immediate); break;
                case R4300i_ORI: a.or_(x86::rax, (int32_t)Opcode.immediate); break;
                default: a.xor_(x86::rax, (int32_t)Opcode.immediate); break;
                }
                a.mov(Gpr(rt), x86::rax);
            }
            return true;
        case R4300i_LUI:
            if (rt != 0)
            {
                a.mov(Gpr(rt), imm((int32_t)((uint32_t)Opcode.immediate << 16)));
            }
            return true;
        case R4300i_LB:
        case R4300i_LBU:
        case R4300i_LH:
        case R4300i_LHU:
        case R4300i_LW:
        case R4300i_LWU:
        case R4300i_LWC1:
            return !m_State.Force32bit && Load(Opcode, PC, Handler, DelaySlot);
        case R4300i_SB:
        case R4300i_SH:
        case R4300i_SW:
        case R4300i_SWC1:
            return !m_State.Force32bit && Store(Opcode, PC, Handler, DelaySlot);
        }
        return false;
    }

    x86::Assembler & a;
    const CInterpreterJit::Context & m_State;
    uint32_t m_CountPerOp;
    void * const * m_Table;
    uint32_t m_TableWords;
    uint32_t m_Pending;
    const uint32_t * m_Code;
    uint32_t m_Count;
    uint32_t m_Index;
    Label m_Inner;
    Label m_Epilogue;
    Label m_ExitBody;
    Label m_ExitInstructions;
    Label m_Invalid;
    Label m_UnwindInfo;
    std::vector<SlowPath> m_SlowPaths;
};
} // namespace

struct CInterpreterJit::Runtime : public JitRuntime
{
};

CInterpreterJit::CInterpreterJit(const Context & State) :
    m_State(State),
    m_Runtime(new Runtime()),
    m_BlocksRdram(nullptr),
    m_CountPerOp(0)
{
    static_assert(offsetof(Block, Code) == offsetof(BlockCompiler::CInterpreterJitBlockLayout, Code), "block layout");
    static_assert(offsetof(Block, Inner) == offsetof(BlockCompiler::CInterpreterJitBlockLayout, Inner), "block layout");
    static_assert(offsetof(Block, StartPC) == offsetof(BlockCompiler::CInterpreterJitBlockLayout, StartPC), "block layout");
}

CInterpreterJit::~CInterpreterJit()
{
    Reset();
    delete m_Runtime;
}

void CInterpreterJit::Reset(void)
{
    for (size_t i = 0; i < m_Blocks.size(); i++)
    {
        if (m_Blocks[i] != nullptr)
        {
            Release(m_Blocks[i]);
            m_Blocks[i] = nullptr;
        }
    }
    m_Blocks.clear();
    m_BlocksRdram = nullptr;
}

void CInterpreterJit::Release(Block * CompiledBlock)
{
#ifdef _WIN32
    if (CompiledBlock->UnwindRegistered)
    {
        RtlDeleteFunctionTable((PRUNTIME_FUNCTION)CompiledBlock->Unwind);
    }
#endif
    m_Runtime->release(CompiledBlock->Code);
    delete CompiledBlock;
}

// The block for a program counter in RDRAM through KSEG0 or KSEG1, compiled
// when there is none yet
CInterpreterJit::Block * CInterpreterJit::Lookup(uint64_t ProgramCounter)
{
    if ((uint64_t)(int64_t)(int32_t)ProgramCounter != ProgramCounter || ((uint32_t)ProgramCounter & 0xC0000000) != 0x80000000)
    {
        return nullptr;
    }
    uint32_t Physical = (uint32_t)ProgramCounter & 0x1FFFFFFF;
    if (Physical >= m_Blocks.size() * 4)
    {
        return nullptr;
    }
    Block *& Entry = m_Blocks[Physical >> 2];
    if (Entry != nullptr && Entry->StartPC != ProgramCounter)
    {
        Release(Entry);
        Entry = nullptr;
    }
    if (Entry == nullptr)
    {
        Entry = Compile(ProgramCounter, (const uint32_t *)(m_BlocksRdram + Physical));
    }
    return Entry;
}

CInterpreterJit::Result CInterpreterJit::Execute(uint64_t ProgramCounter)
{
    uint8_t * Rdram = *m_State.Rdram;
    uint32_t RdramSize = *m_State.RdramSize;
    if (Rdram != m_BlocksRdram || m_Blocks.size() != RdramSize / 4 || m_CountPerOp != g_GameSettings.countPerOp)
    {
        Reset();
        m_Blocks.assign(RdramSize / 4, nullptr);
        m_BlocksRdram = Rdram;
        m_CountPerOp = g_GameSettings.countPerOp;
    }

    // A block that no longer matches memory is compiled again. The first try
    // runs at ProgramCounter; later ones where the previous block stopped.
    for (uint32_t Attempt = 0; Attempt < 4; Attempt++)
    {
        uint64_t PC = Attempt == 0 ? ProgramCounter : *m_State.ProgramCounter;
        Block * Current = Lookup(PC);
        if (Current == nullptr)
        {
            return Attempt == 0 ? Result_None : Result_Instructions;
        }
        int Ran = Current->Code();
        if (Ran != BlockInvalid)
        {
            return (Result)Ran;
        }
        Block *& Entry = m_Blocks[((uint32_t)*m_State.ProgramCounter & 0x1FFFFFFF) >> 2];
        if (Entry != nullptr)
        {
            Release(Entry);
            Entry = nullptr;
        }
    }
    return Result_Instructions;
}

CInterpreterJit::Block * CInterpreterJit::Compile(uint64_t StartPC, const uint32_t * Code)
{
    // The instructions: to the end of the page, or a branch and its delay
    // slot when both are in the page and the slot holds no branch
    uint32_t Count = 0;
    int32_t BranchIndex = -1;
    bool HasDelaySlot = false;
    for (;;)
    {
        uint64_t PC = StartPC + Count * 4;
        if (R4300iInstruction(PC, Code[Count]).HasDelaySlot())
        {
            BranchIndex = (int32_t)Count;
            Count += 1;
            if (((PC + 4) & 0xFFF) != 0 && !R4300iInstruction(PC + 4, Code[Count]).HasDelaySlot())
            {
                Count += 1;
                HasDelaySlot = true;
            }
            break;
        }
        Count += 1;
        if (((PC + 4) & 0xFFF) == 0 || Count == MaxBlockInstructions)
        {
            break;
        }
    }

    R4300iOp & Interpreter = *m_State.Interpreter;
    CodeHolder Holder;
    JitErrors Errors;
    Holder.init(m_Runtime->environment());
    Holder.setErrorHandler(&Errors);
    x86::Assembler Assembler(&Holder);
    BlockCompiler Compiler(Assembler, m_State, m_CountPerOp, (void * const *)m_Blocks.data(), (uint32_t)m_Blocks.size());

    Compiler.Prologue(Code, Count);
    for (uint32_t i = 0; i < Count; i++)
    {
        R4300iOpcode Opcode;
        Opcode.Value = Code[i];
        uint64_t PC = StartPC + i * 4;
        Compiler.SetIndex(i);
        if ((int32_t)i == BranchIndex)
        {
            R4300iOpcode DelaySlot;
            DelaySlot.Value = HasDelaySlot ? Code[i + 1] : 0;
            uint64_t DelaySlotHandler = HasDelaySlot ? Interpreter.HandlerAddress(DelaySlot.Value) : 0;
            BranchInfo Info;
            if (HasDelaySlot && NativeBranch(Opcode, PC, Info))
            {
                Compiler.CompiledBranch(Opcode, PC, Info, DelaySlot, DelaySlotHandler);
            }
            else
            {
                Compiler.InterpretedBranch(Opcode, PC, Interpreter.HandlerAddress(Opcode.Value), HasDelaySlot ? &DelaySlot : nullptr, DelaySlotHandler);
            }
            break;
        }
        Compiler.Instruction(Opcode, PC, Interpreter.HandlerAddress(Opcode.Value), false);
        if (i + 1 == Count)
        {
            Compiler.EndOfPage(PC);
        }
        else
        {
            Compiler.InstructionDone();
        }
    }
    Compiler.Epilogue();

    BlockCode Function = nullptr;
    if (Errors.Failed || m_Runtime->add(&Function, &Holder) != kErrorOk || Function == nullptr)
    {
        return nullptr;
    }
    Block * NewBlock = new Block;
    NewBlock->Code = Function;
    NewBlock->Inner = (const uint8_t *)Function + Holder.labelOffsetFromBase(Compiler.Inner());
    NewBlock->StartPC = StartPC;
    NewBlock->NumWords = Count;
    NewBlock->UnwindRegistered = false;
#ifdef _WIN32
    static_assert(sizeof(RUNTIME_FUNCTION) == sizeof(NewBlock->Unwind), "RUNTIME_FUNCTION layout");
    const uint32_t UnwindOffset = (uint32_t)Holder.labelOffsetFromBase(Compiler.UnwindInfo());
    if (memcmp((const void *)Function, PrologueBytes, sizeof(PrologueBytes)) == 0)
    {
        PRUNTIME_FUNCTION Entry = (PRUNTIME_FUNCTION)NewBlock->Unwind;
        Entry->BeginAddress = 0;
        Entry->EndAddress = UnwindOffset;
        Entry->UnwindData = UnwindOffset;
        NewBlock->UnwindRegistered = RtlAddFunctionTable(Entry, 1, (DWORD64)(uintptr_t)Function) != FALSE;
    }
#endif
    return NewBlock;
}

#endif
