#pragma once
#include "SoundBase.h"

class DirectSoundDriver :
    public SoundDriverBase
{
    enum { DS_SEGMENTS = 4 };

public:
    DirectSoundDriver();
    bool Initialize();
    void StopAudio();							// Stops the audio playback (as if paused)
    void StartAudio();							// Starts the audio playback (as if unpaused)
    void SetFrequency(uint32_t Frequency, uint32_t BufferSize);
    void SetVolume(uint32_t Volume);

private:
    static uint32_t __stdcall stAudioThreadProc(DirectSoundDriver * _this) { _this->AudioThreadProc(); return 0; }

    void SetSegmentSize(uint32_t length, uint32_t SampleRate);
    void AudioThreadProc();
    void ApplyDynamicRateControl();

    volatile bool m_AudioIsDone;
    uint32_t m_LOCK_SIZE;
    uint32_t m_BaseFrequency;    // N64 playback rate that DRC nudges around
    double m_RateFactor;         // current DRC multiplier, ~1.0
    void * m_lpds;
    void * m_lpdsb;
    void * m_lpdsbuf;
    void * m_handleAudioThread;
    uint32_t m_dwAudioThreadId;
};
