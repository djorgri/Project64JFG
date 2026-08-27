#include "stdafx.h"

#include <stdio.h>

#include <Common/Log.h>
#include <Project64-core/N64System/Profiling.h>

enum
{
    MAX_FRAMES = 13
};

CProfiling::CProfiling() :
    m_CurrentDisplayCount(MAX_FRAMES),
    m_CurrentTimerType(Timer_None)
{
    memset(m_Timers, 0, sizeof(m_Timers));
}

void CProfiling::RecordTime(PROFILE_TIMERS timer, uint32_t TimeTaken)
{
    m_Timers[timer] += TimeTaken;
}

uint64_t CProfiling::NonCPUTime(void)
{
    uint64_t TotalTime = 0;
    for (int i = 0; i < Timer_Max; i++)
    {
        if (i == Timer_R4300)
        {
            continue;
        }
        TotalTime += m_Timers[i];
    }
    return TotalTime;
}

PROFILE_TIMERS CProfiling::StartTimer(PROFILE_TIMERS TimerType)
{
    PROFILE_TIMERS PreviousType = StopTimer();
    m_CurrentTimerType = TimerType;
    m_StartTime.SetToNow();
    return PreviousType;
}

PROFILE_TIMERS CProfiling::StopTimer()
{
    if (m_CurrentTimerType == Timer_None)
    {
        return m_CurrentTimerType;
    }
    HighResTimeStamp EndTime;
    EndTime.SetToNow();
    uint64_t TimeTaken = EndTime.GetMicroSeconds() - m_StartTime.GetMicroSeconds();
    m_Timers[m_CurrentTimerType] += TimeTaken;

    PROFILE_TIMERS CurrentTimerType = m_CurrentTimerType;
    m_CurrentTimerType = Timer_None;
    return CurrentTimerType;
}

void CProfiling::ShowCPU_Usage()
{
    PROFILE_TIMERS PreviousType = StopTimer();
    // Total over every timer, so the reported percentages add up to 100%. Timer_UpdateScreen
    // in particular covers the video plugin's UpdateScreen (VI) and used to be left out.
    uint64_t TotalTime = 0;
    for (int i = Timer_R4300; i < Timer_Max; i++)
    {
        TotalTime += m_Timers[i];
    }
    if (TotalTime == 0)
    {
        return;
    }
    if (m_CurrentDisplayCount > 0)
    {
        m_CurrentDisplayCount -= 1;
        return;
    }

    uint64_t OtherTime = m_Timers[Timer_RSP_Unknown] + m_Timers[Timer_RefreshScreen] + m_Timers[Timer_UpdateFPS];

    uint32_t R4300 = (uint32_t)(m_Timers[Timer_R4300] * 10000 / TotalTime);
    uint32_t RSP_Dlist = (uint32_t)(m_Timers[Timer_RSP_Dlist] * 10000 / TotalTime);
    uint32_t RSP_Alist = (uint32_t)(m_Timers[Timer_RSP_Alist] * 10000 / TotalTime);
    uint32_t UpdateScreen = (uint32_t)(m_Timers[Timer_UpdateScreen] * 10000 / TotalTime);
    uint32_t Idel = (uint32_t)(m_Timers[Timer_Idel] * 10000 / TotalTime);
    uint32_t Other = (uint32_t)(OtherTime * 10000 / TotalTime);

    m_CurrentDisplayCount = MAX_FRAMES;
    g_Notify->DisplayMessage(0, stdstr_f("r4300i: %d.%02d%%   GFX: %d.%02d%%   Alist: %d.%02d%%   VI: %d.%02d%%   Idle: %d.%02d%%   Other: %d.%02d%%",
                                         R4300 / 100, R4300 % 100, RSP_Dlist / 100, RSP_Dlist % 100, RSP_Alist / 100, RSP_Alist % 100,
                                         UpdateScreen / 100, UpdateScreen % 100, Idel / 100, Idel % 100, Other / 100, Other % 100)
                                    .c_str());

    ResetTimers();
    if (PreviousType != Timer_None)
    {
        StartTimer(PreviousType);
    }
}

void CProfiling::ResetTimers()
{
    memset(m_Timers, 0, sizeof(m_Timers));
}