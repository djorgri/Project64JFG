#pragma once
#include <Project64-plugin-spec/Audio.h>

extern AUDIO_INFO g_AudioInfo;

// True only while the sound driver is running against a valid g_AudioInfo. The
// audio thread must not touch g_AudioInfo's register pointers or callbacks
// while this is false (ROM close, re-init, shutdown), when those pointers can
// be stale or torn.
extern volatile bool g_AudioActive;
