// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// Copyright(C) 2000-2015 Azimer
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

#include "SoundBase.h"
#include <Common/Util.h>
#include <Project64-audio/AudioSettings.h>
#include <Project64-audio/AudioMain.h>
#include <Project64-audio/trace.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

// The register pointers inside g_AudioInfo belong to the emulator core and can
// transiently dangle while a save state loads or the ROM/plugin is torn down.
// The audio thread runs asynchronously and used to dereference them unguarded,
// which crashed the whole emulator (an access violation reading AI_CONTROL_REG).
// These helpers reject a null pointer and, on Windows, swallow an access
// violation so a stale pointer yields silence instead of taking the process
// down. Each is POD-only so __try/__except is valid under /EHsc.
namespace
{
bool AudioRegRead(const uint32_t * reg, uint32_t & value)
{
    if (!g_AudioActive || reg == nullptr)
    {
        return false;
    }
#ifdef _WIN32
    __try
    {
        value = *reg;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Transient stale pointer: skip this pass and recover next one.
        return false;
    }
    return true;
#else
    value = *reg;
    return true;
#endif
}

bool AudioRegWrite(uint32_t * reg, uint32_t value)
{
    if (!g_AudioActive || reg == nullptr)
    {
        return false;
    }
#ifdef _WIN32
    __try
    {
        *reg = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Transient stale pointer: skip this pass and recover next one.
        return false;
    }
    return true;
#else
    *reg = value;
    return true;
#endif
}

void AudioCheckInterrupts(void)
{
    if (!g_AudioActive || g_AudioInfo.CheckInterrupts == nullptr)
    {
        return;
    }
#ifdef _WIN32
    __try
    {
        g_AudioInfo.CheckInterrupts();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_AudioActive = false;
    }
#else
    g_AudioInfo.CheckInterrupts();
#endif
}
}

SoundDriverBase::SoundDriverBase() :
    m_MaxBufferSize(MAX_SIZE),
    m_AI_DMAPrimaryBuffer(nullptr),
    m_AI_DMASecondaryBuffer(nullptr),
    m_AI_DMAPrimaryBytes(0),
    m_AI_DMASecondaryBytes(0),
    m_CurrentReadLoc(0),
    m_CurrentWriteLoc(0),
    m_BufferRemaining(0),
    m_LastSampleL(0),
    m_LastSampleR(0)
{
    memset(&m_Buffer, 0, sizeof(m_Buffer));
}

bool SoundDriverBase::Initialize()
{
    return true;
}

void SoundDriverBase::AI_SetFrequency(uint32_t Frequency, uint32_t BufferSize)
{
    SetFrequency(Frequency, BufferSize);
    m_MaxBufferSize = (BufferSize * 8);
    if (m_MaxBufferSize > MAX_SIZE)
    {
        m_MaxBufferSize = MAX_SIZE;
    }
    m_CurrentReadLoc = m_CurrentWriteLoc = m_BufferRemaining = 0;
}

void SoundDriverBase::AI_LenChanged(uint8_t *start, uint32_t length)
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");

    // Bleed off some of this buffer to smooth out audio. Bound the wait: if the
    // audio thread stalls (lost DirectSound focus, teardown), an unbounded loop
    // here would freeze the whole emulator. ~250 ms is far longer than the few
    // ms normal pacing needs, so it never trims real playback, only breaks a
    // hang.
    if (g_settings->SyncAudio() || !g_settings->FullSpeed())
    {
        uint32_t waited = 0;
        while ((m_BufferRemaining) == m_MaxBufferSize && g_AudioActive && waited < 250)
        {
            pjutil::Sleep(1);
            waited++;
        }
    }

    CGuard guard(m_CS);
    BufferAudio();

    if (m_AI_DMASecondaryBuffer != nullptr)
    {
        WriteTrace(TraceAudioDriver, TraceDebug, "Discarding previous secondary buffer");
    }
    m_AI_DMASecondaryBuffer = start;
    m_AI_DMASecondaryBytes = length;
    if (m_AI_DMAPrimaryBytes == 0)
    {
        m_AI_DMAPrimaryBuffer = m_AI_DMASecondaryBuffer;
        m_AI_DMASecondaryBuffer = nullptr;
        m_AI_DMAPrimaryBytes = m_AI_DMASecondaryBytes;
        m_AI_DMASecondaryBytes = 0;
    }

    AudioRegWrite(g_AudioInfo.AI_STATUS_REG, AI_STATUS_DMA_BUSY);
    if (m_AI_DMAPrimaryBytes > 0 && m_AI_DMASecondaryBytes > 0)
    {
        AudioRegWrite(g_AudioInfo.AI_STATUS_REG, (uint32_t)(AI_STATUS_DMA_BUSY | AI_STATUS_FIFO_FULL));
    }
    BufferAudio();
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void SoundDriverBase::AI_Startup()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    m_AI_DMAPrimaryBytes = m_AI_DMASecondaryBytes = 0;
    m_AI_DMAPrimaryBuffer = m_AI_DMASecondaryBuffer = nullptr;
    m_MaxBufferSize = MAX_SIZE;
    m_CurrentReadLoc = m_CurrentWriteLoc = m_BufferRemaining = 0;
    if (Initialize())
    {
        StartAudio();
        // The driver is up against the current g_AudioInfo; let the audio
        // thread read it.
        g_AudioActive = true;
    }
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
}

void SoundDriverBase::AI_Shutdown()
{
    // Stop the audio thread from touching g_AudioInfo before it is joined.
    g_AudioActive = false;
    StopAudio();
}

void SoundDriverBase::AI_Update(bool Wait)
{
    m_AiUpdateEvent.IsTriggered(Wait ? SyncEvent::INFINITE_TIMEOUT : 0);
}

uint32_t SoundDriverBase::AI_ReadLength()
{
    CGuard guard(m_CS);
    return m_AI_DMAPrimaryBytes & ~0x7;
}

void SoundDriverBase::LoadAiBuffer(uint8_t *start, uint32_t length)
{
    static uint8_t nullBuff[MAX_SIZE];
    uint8_t *ptrStart = start != nullptr ? start : nullBuff;
    uint32_t writePtr = 0, bytesToMove = length;

    if (bytesToMove > m_MaxBufferSize)
    {
        memset(ptrStart, 0, bytesToMove);
        return;
    }
    uint32_t control = 0;
    if (!AudioRegRead(g_AudioInfo.AI_CONTROL_REG, control))
    {
        // g_AudioInfo is not safe to read (tearing down / stale) - emit silence
        // and leave the guest state untouched.
        memset(ptrStart, 0, bytesToMove);
        return;
    }
    bool DMAEnabled = (control & AI_CONTROL_DMA_ON) == AI_CONTROL_DMA_ON;
    if (!DMAEnabled)
    {
        WriteTrace(TraceAudioDriver, TraceVerbose, "Return silence - DMA is disabled");
        memset(ptrStart, 0, bytesToMove);
        return;
    }

    CGuard guard(m_CS);
    WriteTrace(TraceAudioDriver, TraceVerbose, "Step 0: Replace depleted stored buffer for next run");
    BufferAudio();

    WriteTrace(TraceAudioDriver, TraceVerbose, "Step 1: Deplete stored buffer (bytesToMove: 0x%08X m_BufferRemaining: 0x%08X)", bytesToMove, m_BufferRemaining);
    while (bytesToMove > 0 && m_BufferRemaining > 0)
    {
        *(uint32_t *)(ptrStart + writePtr) = *(uint32_t *)(m_Buffer + m_CurrentReadLoc);
        m_CurrentReadLoc += 4;
        writePtr += 4;
        m_CurrentReadLoc %= m_MaxBufferSize;
        m_BufferRemaining -= 4;
        bytesToMove -= 4;
    }
    // Remember the last frame that actually played, for underrun declicking.
    if (writePtr >= 4)
    {
        uint32_t lastFrame = *(uint32_t *)(ptrStart + writePtr - 4);
        m_LastSampleL = (int16_t)(lastFrame & 0xFFFF);
        m_LastSampleR = (int16_t)(lastFrame >> 16);
    }

    // Step 2: underrun. Cutting straight to digital silence clicks; fade the
    // last frame down to zero over ~1-2 ms instead, then stay silent. This only
    // shapes a gap we already had to emit, so it adds no latency.
    WriteTrace(TraceAudioDriver, TraceVerbose, "Step 2: Fill bytesToMove (0x%08X) by fading out", bytesToMove);
    int16_t fadeL = m_LastSampleL, fadeR = m_LastSampleR;
    while (bytesToMove >= 4)
    {
        fadeL = (int16_t)((fadeL * 15) / 16);
        fadeR = (int16_t)((fadeR * 15) / 16);
        *(int16_t *)(ptrStart + writePtr) = fadeL;
        *(int16_t *)(ptrStart + writePtr + 2) = fadeR;
        writePtr += 4;
        bytesToMove -= 4;
    }
    m_LastSampleL = fadeL;
    m_LastSampleR = fadeR;
    while (bytesToMove > 0)
    {
        *(uint8_t *)(ptrStart + writePtr) = 0;
        writePtr += 1;
        bytesToMove -= 1;
    }

    WriteTrace(TraceAudioDriver, TraceVerbose, "Step 3: Replace depleted stored buffer for next run");
    BufferAudio();
}

void SoundDriverBase::BufferAudio()
{
    WriteTrace(TraceAudioDriver, TraceVerbose, "Start (m_BufferRemaining: 0x%08X m_MaxBufferSize: 0x%08X m_AI_DMAPrimaryBytes: 0x%08X m_AI_DMASecondaryBytes: 0x%08X)", m_BufferRemaining, m_MaxBufferSize, m_AI_DMAPrimaryBytes, m_AI_DMASecondaryBytes);
    while ((m_BufferRemaining < m_MaxBufferSize) && (m_AI_DMAPrimaryBytes > 0 || m_AI_DMASecondaryBytes > 0))
    {
        *(uint16_t *)(m_Buffer + m_CurrentWriteLoc) = *(uint16_t *)(m_AI_DMAPrimaryBuffer + 2);
        *(uint16_t *)(m_Buffer + m_CurrentWriteLoc + 2) = *(uint16_t *)m_AI_DMAPrimaryBuffer;
        m_CurrentWriteLoc += 4;
        m_AI_DMAPrimaryBuffer += 4;
        m_CurrentWriteLoc %= m_MaxBufferSize;
        m_BufferRemaining += 4;
        m_AI_DMAPrimaryBytes -= 4;
        if (m_AI_DMAPrimaryBytes == 0)
        {
            WriteTrace(TraceAudioDriver, TraceVerbose, "Emptied primary buffer");
            m_AI_DMAPrimaryBytes = m_AI_DMASecondaryBytes; m_AI_DMAPrimaryBuffer = m_AI_DMASecondaryBuffer; // Switch
            m_AI_DMASecondaryBytes = 0; m_AI_DMASecondaryBuffer = nullptr;
            uint32_t status = 0;
            AudioRegWrite(g_AudioInfo.AI_STATUS_REG, AI_STATUS_DMA_BUSY);
            if (AudioRegRead(g_AudioInfo.AI_STATUS_REG, status))
            {
                AudioRegWrite(g_AudioInfo.AI_STATUS_REG, status & ~AI_STATUS_FIFO_FULL);
            }
            uint32_t intr = 0;
            if (AudioRegRead(g_AudioInfo.MI_INTR_REG, intr))
            {
                AudioRegWrite(g_AudioInfo.MI_INTR_REG, intr | MI_INTR_AI);
            }
            AudioCheckInterrupts();
            if (m_AI_DMAPrimaryBytes == 0)
            {
                AudioRegWrite(g_AudioInfo.AI_STATUS_REG, 0);
            }
        }
    }
    WriteTrace(TraceAudioDriver, TraceVerbose, "Done (m_BufferRemaining: 0x%08X)", m_BufferRemaining);
}

void SoundDriverBase::SetFrequency(uint32_t /*Frequency*/, uint32_t /*Divider*/)
{
}

void SoundDriverBase::StartAudio()
{
}

void SoundDriverBase::StopAudio()
{
}
