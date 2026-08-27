#include "stdafx.h"

#include "GameHackMemory.h"
#include <Project64-core/N64System/Mips/MemoryVirtualMem.h>
#include <Project64-core/N64System/Recompiler/Recompiler.h>

CGameHackMemory::CGameHackMemory(CMipsMemoryVM & MMU) :
    m_MMU(MMU)
{
}

bool CGameHackMemory::IsRdramAddress(uint32_t Address, uint32_t Size) const
{
    uint32_t Segment = Address & 0xE0000000;
    if ((Segment != 0x80000000 && Segment != 0xA0000000) || Size == 0 || Size > m_MMU.RdramSize())
    {
        return false;
    }
    uint32_t Offset = Address & 0x1FFFFFFF;
    return Offset <= m_MMU.RdramSize() - Size;
}

bool CGameHackMemory::ReadU8(uint32_t Address, uint8_t & Value) const
{
    return IsRdramAddress(Address, sizeof(Value)) && m_MMU.MemoryValue8(Address, Value);
}

bool CGameHackMemory::ReadS16(uint32_t Address, int16_t & Value) const
{
    uint16_t RawValue;
    if (!IsRdramAddress(Address, sizeof(RawValue)) || !m_MMU.MemoryValue16(Address, RawValue))
    {
        return false;
    }
    Value = (int16_t)RawValue;
    return true;
}

bool CGameHackMemory::ReadU32(uint32_t Address, uint32_t & Value) const
{
    return IsRdramAddress(Address, sizeof(Value)) && m_MMU.MemoryValue32(Address, Value);
}

bool CGameHackMemory::ReadF32(uint32_t Address, float & Value) const
{
    uint32_t RawValue;
    if (!ReadU32(Address, RawValue))
    {
        return false;
    }
    memcpy(&Value, &RawValue, sizeof(Value));
    return true;
}

bool CGameHackMemory::WriteU8(uint32_t Address, uint8_t Value)
{
    return IsRdramAddress(Address, sizeof(Value)) && m_MMU.UpdateMemoryValue8(Address, Value);
}

bool CGameHackMemory::WriteS16(uint32_t Address, int16_t Value)
{
    return IsRdramAddress(Address, sizeof(Value)) && m_MMU.UpdateMemoryValue16(Address, (uint16_t)Value);
}

bool CGameHackMemory::WriteU32(uint32_t Address, uint32_t Value)
{
    return IsRdramAddress(Address, sizeof(Value)) && m_MMU.UpdateMemoryValue32(Address, Value);
}

bool CGameHackMemory::WriteF32(uint32_t Address, float Value)
{
    uint32_t RawValue;
    memcpy(&RawValue, &Value, sizeof(RawValue));
    return WriteU32(Address, RawValue);
}

CGameHackCodePatcher::CGameHackCodePatcher(CGameHackMemory & Memory, CRecompiler *& Recompiler) :
    m_Memory(Memory),
    m_Recompiler(Recompiler)
{
}

CGameHackCodePatcher::Result CGameHackCodePatcher::SetEnabled(
    const GAME_HACK_CODE_PATCH * Patches, size_t Count, bool Enabled)
{
    if (Patches == nullptr || Count == 0)
    {
        return Result_NoChanges;
    }

    for (size_t i = 0; i < Count; i++)
    {
        uint32_t Current;
        if (!m_Memory.ReadU32(Patches[i].Address, Current))
        {
            return Result_MemoryUnavailable;
        }
        if (Current != Patches[i].Original && Current != Patches[i].Replacement)
        {
            return Result_SignatureMismatch;
        }
    }

    bool Changed = false;
    for (size_t i = 0; i < Count; i++)
    {
        uint32_t Current;
        uint32_t Desired = Enabled ? Patches[i].Replacement : Patches[i].Original;
        if (!m_Memory.ReadU32(Patches[i].Address, Current))
        {
            return Result_MemoryUnavailable;
        }
        if (Current == Desired)
        {
            continue;
        }
        if (!m_Memory.WriteU32(Patches[i].Address, Desired))
        {
            return Result_MemoryUnavailable;
        }
        InvalidatePage(Patches[i].Address);
        Changed = true;
    }
    return Changed ? Result_Changed : Result_NoChanges;
}

CGameHackCodePatcher::Result CGameHackCodePatcher::Apply(
    const GAME_HACK_CODE_WRITE * Writes, size_t Count)
{
    if (Writes == nullptr || Count == 0)
    {
        return Result_NoChanges;
    }

    for (size_t i = 0; i < Count; i++)
    {
        if (Writes[i].AllowedCount == 0 || Writes[i].AllowedCount > GAME_HACK_CODE_WRITE::MaxAllowedValues)
        {
            return Result_SignatureMismatch;
        }

        uint32_t Current;
        if (!m_Memory.ReadU32(Writes[i].Address, Current))
        {
            return Result_MemoryUnavailable;
        }

        bool Allowed = false;
        for (size_t ValueIndex = 0; ValueIndex < Writes[i].AllowedCount; ValueIndex++)
        {
            if (Current == Writes[i].Allowed[ValueIndex])
            {
                Allowed = true;
                break;
            }
        }
        if (!Allowed)
        {
            return Result_SignatureMismatch;
        }
    }

    bool Changed = false;
    for (size_t i = 0; i < Count; i++)
    {
        uint32_t Current;
        if (!m_Memory.ReadU32(Writes[i].Address, Current))
        {
            return Result_MemoryUnavailable;
        }
        if (Current == Writes[i].Desired)
        {
            continue;
        }
        if (!m_Memory.WriteU32(Writes[i].Address, Writes[i].Desired))
        {
            return Result_MemoryUnavailable;
        }
        InvalidatePage(Writes[i].Address);
        Changed = true;
    }
    return Changed ? Result_Changed : Result_NoChanges;
}

void CGameHackCodePatcher::InvalidatePage(uint32_t Address)
{
    if (m_Recompiler != nullptr)
    {
        uint32_t PhysicalAddress = (Address & 0x1FFFFFFF) & ~0xFFF;
        m_Recompiler->ClearRecompCode_Phys(PhysicalAddress, 0x1000, CRecompiler::Remove_GameHack);
    }
}
