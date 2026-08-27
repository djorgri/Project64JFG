#pragma once

#include <stddef.h>
#include <stdint.h>

class CMipsMemoryVM;
class CRecompiler;

class CGameHackMemory
{
public:
    CGameHackMemory(CMipsMemoryVM & MMU);

    bool IsRdramAddress(uint32_t Address, uint32_t Size = 1) const;
    bool ReadU8(uint32_t Address, uint8_t & Value) const;
    bool ReadS16(uint32_t Address, int16_t & Value) const;
    bool ReadU32(uint32_t Address, uint32_t & Value) const;
    bool ReadF32(uint32_t Address, float & Value) const;
    bool WriteU8(uint32_t Address, uint8_t Value);
    bool WriteS16(uint32_t Address, int16_t Value);
    bool WriteU32(uint32_t Address, uint32_t Value);
    bool WriteF32(uint32_t Address, float Value);

private:
    CMipsMemoryVM & m_MMU;
};

struct GAME_HACK_CODE_PATCH
{
    uint32_t Address;
    uint32_t Original;
    uint32_t Replacement;
};

struct GAME_HACK_CODE_WRITE
{
    enum
    {
        MaxAllowedValues = 6,
    };

    uint32_t Address;
    uint32_t Desired;
    uint32_t Allowed[MaxAllowedValues];
    size_t AllowedCount;
};

class CGameHackCodePatcher
{
public:
    enum Result
    {
        Result_NoChanges,
        Result_Changed,
        Result_SignatureMismatch,
        Result_MemoryUnavailable,
    };

    CGameHackCodePatcher(CGameHackMemory & Memory, CRecompiler *& Recompiler);
    Result SetEnabled(const GAME_HACK_CODE_PATCH * Patches, size_t Count, bool Enabled);
    Result Apply(const GAME_HACK_CODE_WRITE * Writes, size_t Count);

private:
    void InvalidatePage(uint32_t Address);

    CGameHackMemory & m_Memory;
    CRecompiler *& m_Recompiler;
};
