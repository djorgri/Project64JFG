// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64.
// Copyright(C) 2000-2015 Azimer
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

#include <windows.h>
#include <mmreg.h>
#include <dsound.h>
#include "DirectSound.h"
#include "AudioMain.h"
#include "trace.h"
#include "AudioSettings.h"

DirectSoundDriver::DirectSoundDriver() :
    m_AudioIsDone(true),
    m_LOCK_SIZE(0),
    m_BaseFrequency(0),
    m_RateFactor(1.0),
    m_lpds(nullptr),
    m_lpdsb(nullptr),
    m_lpdsbuf(nullptr),
    m_handleAudioThread(nullptr),
    m_dwAudioThreadId(0)
{
}

bool DirectSoundDriver::Initialize()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    if (!SoundDriverBase::Initialize())
    {
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return false;
    }

    CGuard guard(m_CS);
    LPDIRECTSOUND8  & lpds = (LPDIRECTSOUND8 &)m_lpds;
    HRESULT hr = DirectSoundCreate8(nullptr, &lpds, nullptr);
    if (FAILED(hr))
    {
        WriteTrace(TraceAudioDriver, TraceWarning, "Unable to DirectSoundCreate (hr: 0x%08X)", hr);
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return false;
    }

    hr = lpds->SetCooperativeLevel((HWND)g_AudioInfo.hWnd, DSSCL_PRIORITY);
    if (FAILED(hr))
    {
        WriteTrace(TraceAudioDriver, TraceWarning, "Failed to SetCooperativeLevel (hr: 0x%08X)", hr);
        WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: false)");
        return false;
    }

    LPDIRECTSOUNDBUFFER & lpdsbuf = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
    if (lpdsbuf)
    {
        IDirectSoundBuffer_Release(lpdsbuf);
        lpdsbuf = nullptr;
    }
    DSBUFFERDESC dsPrimaryBuff = { 0 };
    dsPrimaryBuff.dwSize = sizeof(DSBUFFERDESC);
    dsPrimaryBuff.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;
    dsPrimaryBuff.dwBufferBytes = 0;
    dsPrimaryBuff.lpwfxFormat = nullptr;

    WAVEFORMATEX wfm = { 0 };
    wfm.wFormatTag = WAVE_FORMAT_PCM;
    wfm.nChannels = 2;
    wfm.nSamplesPerSec = 48000;
    wfm.wBitsPerSample = 16;
    wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

    LPDIRECTSOUNDBUFFER & lpdsb = (LPDIRECTSOUNDBUFFER &)m_lpdsb;
    hr = lpds->CreateSoundBuffer(&dsPrimaryBuff, &lpdsb, nullptr);
    if (SUCCEEDED(hr))
    {
        lpdsb->SetFormat(&wfm);
        lpdsb->Play(0, 0, DSBPLAY_LOOPING);
    }
    WriteTrace(TraceAudioDriver, TraceDebug, "Done (res: true)");
    return true;
}

void DirectSoundDriver::StopAudio()
{
    if (m_handleAudioThread != nullptr)
    {
        m_AudioIsDone = true;
        if (WaitForSingleObject((HANDLE)m_handleAudioThread, 5000) == WAIT_TIMEOUT)
        {
            WriteTrace(TraceAudioDriver, TraceError, "Timeout on close");

            TerminateThread((HANDLE)m_handleAudioThread, 1);
        }
        CloseHandle((HANDLE)m_handleAudioThread);
        m_handleAudioThread = nullptr;
    }
}

void DirectSoundDriver::StartAudio()
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start");
    if (m_handleAudioThread == nullptr)
    {
        m_AudioIsDone = false;
        m_handleAudioThread = CreateThread(nullptr, NULL, (LPTHREAD_START_ROUTINE)stAudioThreadProc, this, 0, (LPDWORD)&m_dwAudioThreadId);

        LPDIRECTSOUNDBUFFER & lpdsbuf = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
        if (lpdsbuf != nullptr)
        {
            lpdsbuf->Play(0, 0, DSBPLAY_LOOPING);
        }
    }
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void DirectSoundDriver::SetFrequency(uint32_t Frequency, uint32_t BufferSize)
{
    WriteTrace(TraceAudioDriver, TraceDebug, "Start (Frequency: 0x%08X)", Frequency);
    StopAudio();
    m_LOCK_SIZE = (BufferSize * 2);
    SetSegmentSize(m_LOCK_SIZE, Frequency);

    StartAudio();
    WriteTrace(TraceAudioDriver, TraceDebug, "Done");
}

void DirectSoundDriver::SetVolume(uint32_t Volume)
{
    LPDIRECTSOUNDBUFFER & lpdsb = (LPDIRECTSOUNDBUFFER &)m_lpdsb;
    int32_t dsVolume = -((100 - (int32_t)Volume) * 25);
    if (Volume == 0)
    {
        dsVolume = DSBVOLUME_MIN;
    }
    if (lpdsb != nullptr)
    {
        lpdsb->SetVolume(dsVolume);
    }
}

void DirectSoundDriver::SetSegmentSize(uint32_t length, uint32_t SampleRate)
{
    if (SampleRate == 0) { return; }
    CGuard guard(m_CS);

    WAVEFORMATEX wfm = { 0 };
    wfm.wFormatTag = WAVE_FORMAT_PCM;
    wfm.nChannels = 2;
    wfm.nSamplesPerSec = SampleRate;
    wfm.wBitsPerSample = 16;
    wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

    DSBUFFERDESC dsbdesc = { 0 };
    dsbdesc.dwSize = sizeof(DSBUFFERDESC);
    // DSBCAPS_CTRLFREQUENCY lets dynamic rate control nudge the playback rate.
    dsbdesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRLFREQUENCY;
    dsbdesc.dwBufferBytes = length * DS_SEGMENTS;
    dsbdesc.lpwfxFormat = &wfm;

    // Base rate that dynamic rate control nudges around; reset the running
    // adjustment for the freshly created buffer.
    m_BaseFrequency = SampleRate;
    m_RateFactor = 1.0;

    LPDIRECTSOUND8 & lpds = (LPDIRECTSOUND8 &)m_lpds;
    LPDIRECTSOUNDBUFFER & lpdsbuf = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
    if (lpds != nullptr)
    {
        HRESULT hr = lpds->CreateSoundBuffer(&dsbdesc, &lpdsbuf, nullptr);
        if (FAILED(hr))
        {
            WriteTrace(TraceAudioDriver, TraceWarning, "CreateSoundBuffer failed (hr: 0x%08X)", hr);
        }
    }

    if (lpdsbuf != nullptr)
    {
        lpdsbuf->Play(0, 0, DSBPLAY_LOOPING);
    }
}

void DirectSoundDriver::AudioThreadProc()
{
    LPDIRECTSOUNDBUFFER & lpdsbuff = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
    while (lpdsbuff == nullptr && !m_AudioIsDone)
    {
        Sleep(10);
    }

    if (!m_AudioIsDone)
    {
        WriteTrace(TraceAudioDriver, TraceDebug, "Audio thread started...");
        DWORD dwStatus;
        lpdsbuff->GetStatus(&dwStatus);
        if ((dwStatus & DSBSTATUS_PLAYING) == 0)
        {
            lpdsbuff->Play(0, 0, 0);
        }

        SetThreadPriority(m_handleAudioThread, THREAD_PRIORITY_HIGHEST);
    }

    uint32_t next_pos = 0, write_pos = 0, last_pos = 0;
    while (lpdsbuff != nullptr && !m_AudioIsDone)
    {
        WriteTrace(TraceAudioDriver, TraceVerbose, "last_pos: 0x%08X write_pos: 0x%08X next_pos: 0x%08X", last_pos, write_pos, next_pos);
        while (last_pos == write_pos)
        { // Cycle around until a new buffer position is available
            if (lpdsbuff == nullptr)
            {
                break;
            }
            uint32_t play_pos = 0;
            if (lpdsbuff == nullptr || FAILED(lpdsbuff->GetCurrentPosition((unsigned long*)&play_pos, nullptr)))
            {
                WriteTrace(TraceAudioDriver, TraceDebug, "Error getting audio position...");
                m_AudioIsDone = true;
                break;
            }
            write_pos = play_pos < m_LOCK_SIZE ? (m_LOCK_SIZE * DS_SEGMENTS) - m_LOCK_SIZE : ((play_pos / m_LOCK_SIZE) * m_LOCK_SIZE) - m_LOCK_SIZE;
            WriteTrace(TraceAudioDriver, TraceVerbose, "play_pos: 0x%08X m_write_pos: 0x%08X next_pos: 0x%08X m_LOCK_SIZE: 0x%08X", play_pos, write_pos, next_pos, m_LOCK_SIZE);
            if (last_pos == write_pos)
            {
                WriteTrace(TraceAudioDriver, TraceVerbose, "Sleep");
                Sleep(1);
            }
        }
        // This means we had a buffer segment skipped
        if (next_pos != write_pos)
        {
            WriteTrace(TraceAudioDriver, TraceDebug, "Buffer segment skipped");
        }

        // Store our last position
        last_pos = write_pos;

        // Set out next anticipated segment
        next_pos = write_pos + m_LOCK_SIZE;
        if (next_pos >= (m_LOCK_SIZE*DS_SEGMENTS))
        {
            next_pos -= (m_LOCK_SIZE*DS_SEGMENTS);
        }

        if (m_AudioIsDone)
        {
            break;
        }

        // Time to write out to the buffer
        LPVOID lpvPtr1, lpvPtr2;
        DWORD dwBytes1, dwBytes2;
        WriteTrace(TraceAudioDriver, TraceVerbose, "Lock buffer");
        if (lpdsbuff->Lock(write_pos, m_LOCK_SIZE, &lpvPtr1, &dwBytes1, &lpvPtr2, &dwBytes2, 0) != DS_OK)
        {
            WriteTrace(TraceAudioDriver, TraceError, "Error locking sound buffer");
            break;
        }
        WriteTrace(TraceAudioDriver, TraceVerbose, "dwBytes1: 0x%08X dwBytes2: 0x%08X", dwBytes1, dwBytes2);

        {
            CGuard guard(m_CS);
            LoadAiBuffer((uint8_t *)lpvPtr1, dwBytes1);
            if (dwBytes2)
            {
                WriteTrace(TraceAudioDriver, TraceDebug, "Loading second buffer");
                LoadAiBuffer((BYTE *)lpvPtr2, dwBytes2);
            }
        }

        // Fills dwBytes to the sound buffer
        WriteTrace(TraceAudioDriver, TraceVerbose, "Unlock buffer");
        if (FAILED(lpdsbuff->Unlock(lpvPtr1, dwBytes1, lpvPtr2, dwBytes2)))
        {
            WriteTrace(TraceAudioDriver, TraceError, "Error unlocking sound buffer");
            break;
        }

        // Steer the playback rate to keep the ring near half full.
        ApplyDynamicRateControl();
    }

    LPDIRECTSOUNDBUFFER & lpdsbuf = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
    if (lpdsbuf)
    {
        lpdsbuf->Stop();
    }
    WriteTrace(TraceAudioDriver, TraceDebug, "Audio thread terminated...");
}

// Dynamic rate control: nudge the DirectSound playback rate a fraction of a
// percent so the ring buffer stays near half full instead of drifting to empty
// (underrun -> gaps) or full (overflow -> dropped audio). This absorbs mild
// emulation-speed jitter using the buffer we already have, so it adds no
// latency, and the deviation is capped well below what the ear can hear. A gross
// speed change (a real FPS collapse) exceeds the cap and still falls back to the
// declick/silence path - by design, since tracking it would be audibly wrong.
void DirectSoundDriver::ApplyDynamicRateControl()
{
    if (m_BaseFrequency == 0)
    {
        return;
    }
    LPDIRECTSOUNDBUFFER & lpdsbuff = (LPDIRECTSOUNDBUFFER &)m_lpdsbuf;
    if (lpdsbuff == nullptr)
    {
        return;
    }

    const double targetFill = 0.5;
    const double maxDeviation = 0.005; // +/-0.5% playback rate, inaudible
    const double slew = 0.08;          // ease toward the target rate

    double error = BufferFillRatio() - targetFill; // -0.5 (empty) .. +0.5 (full)
    double desired = 1.0 + (maxDeviation / targetFill) * error;
    if (desired > 1.0 + maxDeviation) { desired = 1.0 + maxDeviation; }
    if (desired < 1.0 - maxDeviation) { desired = 1.0 - maxDeviation; }

    m_RateFactor += (desired - m_RateFactor) * slew;

    uint32_t newFrequency = (uint32_t)((double)m_BaseFrequency * m_RateFactor + 0.5);
    lpdsbuff->SetFrequency(newFrequency);
}
