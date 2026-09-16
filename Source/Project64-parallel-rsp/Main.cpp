#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <Rsp.h>
#include <rsp_jit.hpp>

// parallel-rsp's optional integration path declares this structure privately
// in rsp_1.1.h. Defining the data symbol with this equivalent layout avoids
// importing that incompatible legacy header into Project64's plugin ABI.
struct ParallelRSPInfo
{
    HINSTANCE hInst;
    int MemoryBswaped;
    unsigned char *RDRAM;
    unsigned char *DMEM;
    unsigned char *IMEM;
    uint32_t *MI_INTR_REG;
    uint32_t *SP_MEM_ADDR_REG;
    uint32_t *SP_DRAM_ADDR_REG;
    uint32_t *SP_RD_LEN_REG;
    uint32_t *SP_WR_LEN_REG;
    uint32_t *SP_STATUS_REG;
    uint32_t *SP_DMA_FULL_REG;
    uint32_t *SP_DMA_BUSY_REG;
    uint32_t *SP_PC_REG;
    uint32_t *SP_SEMAPHORE_REG;
    uint32_t *DPC_START_REG;
    uint32_t *DPC_END_REG;
    uint32_t *DPC_CURRENT_REG;
    uint32_t *DPC_STATUS_REG;
    uint32_t *DPC_CLOCK_REG;
    uint32_t *DPC_BUFBUSY_REG;
    uint32_t *DPC_PIPEBUSY_REG;
    uint32_t *DPC_TMEM_REG;
    void (*CheckInterrupts)(void);
    void (*ProcessDList)(void);
    void (*ProcessAList)(void);
    void (*ProcessRdpList)(void);
    void (*ShowCFB)(void);
};

namespace RSP
{
ParallelRSPInfo rsp = {};
short MFC0_count[32] = {};
int SP_STATUS_TIMEOUT = 0x7fff;
}

namespace
{
RSP_INFO g_RspInfo = {};
std::unique_ptr<RSP::JIT::CPU> g_Cpu;
bool g_Initialized = false;
void(CALL *g_HostProcessRdpList)(void) = nullptr;
bool g_RspPerformanceSampleActive = false;
uint64_t g_ActiveRdpCallbackTicks = 0;

// Sample one RSP task out of 32. This keeps the diagnostic overhead below the
// noise floor while still exposing whether RSP execution is the 60 fps limit.
constexpr uint32_t RspPerformanceSampleStride = 32;
constexpr size_t RspPerformanceTaskBucketCount = 8;
struct RspTaskPerformanceBucket
{
    uint64_t imem_hash = 0;
    uint32_t dmem_task_type = 0;
    uint64_t estimated_ticks = 0;
    uint64_t estimated_rdp_callback_ticks = 0;
    uint32_t samples = 0;
};
struct RspPerformanceCounters
{
    LARGE_INTEGER frequency = {};
    LARGE_INTEGER window_start = {};
    uint64_t estimated_ticks = 0;
    uint64_t estimated_rdp_callback_ticks = 0;
    uint32_t calls = 0;
    uint32_t samples = 0;
    std::array<RspTaskPerformanceBucket, RspPerformanceTaskBucketCount> task_buckets = {};
};
RspPerformanceCounters g_Performance;

uint64_t hash_imem(const unsigned char *imem)
{
    // Only sampled tasks are hashed. This identifies audio, graphics and
    // game-specific microcodes without adding measurable work to every call.
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t index = 0; index < 0x1000; index++)
    {
        hash ^= imem[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

void add_task_sample(uint64_t imem_hash, uint32_t dmem_task_type, uint64_t estimated_ticks,
                     uint64_t estimated_rdp_callback_ticks)
{
    RspTaskPerformanceBucket *bucket = nullptr;
    for (auto &candidate : g_Performance.task_buckets)
    {
        if (candidate.samples != 0 && candidate.imem_hash == imem_hash)
        {
            bucket = &candidate;
            break;
        }
        if (bucket == nullptr && candidate.samples == 0)
            bucket = &candidate;
    }

    if (bucket == nullptr)
    {
        bucket = &g_Performance.task_buckets[0];
        for (auto &candidate : g_Performance.task_buckets)
        {
            if (candidate.estimated_ticks < bucket->estimated_ticks)
                bucket = &candidate;
        }
        *bucket = {};
    }

    bucket->imem_hash = imem_hash;
    bucket->dmem_task_type = dmem_task_type;
    bucket->estimated_ticks += estimated_ticks;
    bucket->estimated_rdp_callback_ticks += estimated_rdp_callback_ticks;
    bucket->samples++;
}

void CALL ProfiledProcessRdpList()
{
    if (g_HostProcessRdpList == nullptr)
        return;

    if (!g_RspPerformanceSampleActive)
    {
        g_HostProcessRdpList();
        return;
    }

    LARGE_INTEGER start = {};
    LARGE_INTEGER end = {};
    QueryPerformanceCounter(&start);
    g_HostProcessRdpList();
    QueryPerformanceCounter(&end);
    g_ActiveRdpCallbackTicks += uint64_t(end.QuadPart - start.QuadPart);
}

void reset_performance_counters()
{
    g_Performance = {};
    QueryPerformanceFrequency(&g_Performance.frequency);
    QueryPerformanceCounter(&g_Performance.window_start);
}

void report_performance_if_due()
{
    if (g_Performance.frequency.QuadPart == 0)
        reset_performance_counters();

    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    const auto elapsed_ticks = now.QuadPart - g_Performance.window_start.QuadPart;
    if (elapsed_ticks < g_Performance.frequency.QuadPart)
        return;

    const auto elapsed_seconds = double(elapsed_ticks) / double(g_Performance.frequency.QuadPart);
    const auto estimated_ms = double(g_Performance.estimated_ticks) * 1000.0 /
        double(g_Performance.frequency.QuadPart);
    const auto rdp_callback_ms = double(g_Performance.estimated_rdp_callback_ticks) * 1000.0 /
        double(g_Performance.frequency.QuadPart);
    char executable[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executable, sizeof(executable));
    std::string path(executable);
    const auto separator = path.find_last_of("\\/");
    if (separator != std::string::npos)
    {
        path.resize(separator + 1);
        path += "Logs\\Project64-ParallelRSP.log";
        char message[768] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "perf: calls %u, samples %u, estimated RSP CPU %.2f ms (%.1f%% of %.2f s), RDP callback %.2f ms",
            g_Performance.calls, g_Performance.samples, estimated_ms,
            estimated_ms / (elapsed_seconds * 10.0), elapsed_seconds, rdp_callback_ms);
        for (const auto &bucket : g_Performance.task_buckets)
        {
            if (bucket.samples == 0)
                continue;

            const auto bucket_ms = double(bucket.estimated_ticks) * 1000.0 /
                double(g_Performance.frequency.QuadPart);
            const auto bucket_rdp_callback_ms = double(bucket.estimated_rdp_callback_ticks) * 1000.0 /
                double(g_Performance.frequency.QuadPart);
            const auto length = std::strlen(message);
            _snprintf_s(message + length, sizeof(message) - length, _TRUNCATE,
                " | u%08llX dmem0=%u %.1fms rdp=%.1f/%u",
                static_cast<unsigned long long>(bucket.imem_hash), bucket.dmem_task_type,
                bucket_ms, bucket_rdp_callback_ms, bucket.samples);
        }
        FILE *file = nullptr;
        if (fopen_s(&file, path.c_str(), "a") == 0 && file != nullptr)
        {
            std::fprintf(file, "%s\n", message);
            std::fclose(file);
        }
    }
    reset_performance_counters();
}

class ScopedRspPerformanceSample
{
public:
    ScopedRspPerformanceSample()
    {
        sample = (g_Performance.calls++ % RspPerformanceSampleStride) == 0;
        if (sample)
        {
            imem_hash = hash_imem(g_RspInfo.IMEM);
            dmem_task_type = *reinterpret_cast<const uint32_t *>(g_RspInfo.DMEM);
            g_ActiveRdpCallbackTicks = 0;
            g_RspPerformanceSampleActive = true;
            QueryPerformanceCounter(&start);
        }
    }

    ~ScopedRspPerformanceSample()
    {
        if (!sample)
            return;

        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&end);
        const auto estimated_ticks = uint64_t(end.QuadPart - start.QuadPart) * RspPerformanceSampleStride;
        const auto estimated_rdp_callback_ticks = g_ActiveRdpCallbackTicks * RspPerformanceSampleStride;
        g_RspPerformanceSampleActive = false;
        g_Performance.estimated_ticks += estimated_ticks;
        g_Performance.estimated_rdp_callback_ticks += estimated_rdp_callback_ticks;
        add_task_sample(imem_hash, dmem_task_type, estimated_ticks, estimated_rdp_callback_ticks);
        g_Performance.samples++;
        report_performance_if_due();
    }

private:
    bool sample = false;
    LARGE_INTEGER start = {};
    uint64_t imem_hash = 0;
    uint32_t dmem_task_type = 0;
};

void CALL EmptyProcessRdpList()
{
}

bool HasRegisters()
{
    return g_Initialized && g_Cpu != nullptr && g_RspInfo.SP_STATUS_REG != nullptr &&
           g_RspInfo.SP_PC_REG != nullptr && g_RspInfo.SP_SEMAPHORE_REG != nullptr;
}

void EnsureCpu()
{
    if (!g_Cpu)
    {
        g_Cpu = std::make_unique<RSP::JIT::CPU>();
    }
}

void ClearHostState()
{
    // CloseDLL and RomClosed can both run during Project64's final application
    // cleanup, after the N64 register storage has already been released: when
    // the emulation thread does not stop within CloseCpu's timeout it is
    // terminated before it notifies the plugins, and CPlugins' destructor then
    // delivers RomClosed with dangling RSP_INFO pointers. Never dereference a
    // host-owned pointer from either callback. The core clears the SP
    // registers itself on every ROM start, so nothing is lost by not touching
    // SP_PC_REG here.
    g_Initialized = false;
    g_RspInfo = {};
    g_HostProcessRdpList = nullptr;
    g_RspPerformanceSampleActive = false;
    g_ActiveRdpCallbackTicks = 0;
    RSP::rsp = {};
    g_Cpu.reset();
}

void ConfigureIntegrationState(const RSP_INFO &info)
{
	RSP::rsp = {};
	RSP::rsp.hInst = static_cast<HINSTANCE>(info.hInst);
	RSP::rsp.MemoryBswaped = info.MemoryBswaped;
	RSP::rsp.RDRAM = info.RDRAM;
	RSP::rsp.DMEM = info.DMEM;
	RSP::rsp.IMEM = info.IMEM;
	RSP::rsp.MI_INTR_REG = info.MI_INTR_REG;
	RSP::rsp.SP_MEM_ADDR_REG = info.SP_MEM_ADDR_REG;
	RSP::rsp.SP_DRAM_ADDR_REG = info.SP_DRAM_ADDR_REG;
	RSP::rsp.SP_RD_LEN_REG = info.SP_RD_LEN_REG;
	RSP::rsp.SP_WR_LEN_REG = info.SP_WR_LEN_REG;
	RSP::rsp.SP_STATUS_REG = info.SP_STATUS_REG;
	RSP::rsp.SP_DMA_FULL_REG = info.SP_DMA_FULL_REG;
	RSP::rsp.SP_DMA_BUSY_REG = info.SP_DMA_BUSY_REG;
	RSP::rsp.SP_PC_REG = info.SP_PC_REG;
	RSP::rsp.SP_SEMAPHORE_REG = info.SP_SEMAPHORE_REG;
	RSP::rsp.DPC_START_REG = info.DPC_START_REG;
	RSP::rsp.DPC_END_REG = info.DPC_END_REG;
	RSP::rsp.DPC_CURRENT_REG = info.DPC_CURRENT_REG;
	RSP::rsp.DPC_STATUS_REG = info.DPC_STATUS_REG;
	RSP::rsp.DPC_CLOCK_REG = info.DPC_CLOCK_REG;
	RSP::rsp.DPC_BUFBUSY_REG = info.DPC_BUFBUSY_REG;
	RSP::rsp.DPC_PIPEBUSY_REG = info.DPC_PIPEBUSY_REG;
	RSP::rsp.DPC_TMEM_REG = info.DPC_TMEM_REG;
	RSP::rsp.CheckInterrupts = info.CheckInterrupts;
	RSP::rsp.ProcessDList = info.ProcessDList;
	RSP::rsp.ProcessAList = info.ProcessAList;
	g_HostProcessRdpList = info.ProcessRdpList ? info.ProcessRdpList : EmptyProcessRdpList;
	RSP::rsp.ProcessRdpList = ProfiledProcessRdpList;
	RSP::rsp.ShowCFB = info.ShowCFB;
}
} // namespace

EXPORT void CALL CloseDLL(void)
{
    ClearHostState();
}

EXPORT void CALL DllAbout(void * hParent)
{
    MessageBoxA(static_cast<HWND>(hParent),
                "Parallel RSP plugin for Project64.\n\nBuilt from Themaister's parallel-rsp.",
                "Project64 Parallel RSP", MB_OK | MB_ICONINFORMATION);
}

EXPORT void CALL DllConfig(void * hParent)
{
    MessageBoxA(static_cast<HWND>(hParent),
                "This initial Parallel RSP integration has no configurable options.",
                "Project64 Parallel RSP", MB_OK | MB_ICONINFORMATION);
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    if (PluginInfo == nullptr)
    {
        return;
    }

    std::memset(PluginInfo, 0, sizeof(*PluginInfo));
    PluginInfo->Version = RSP_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_RSP;
    std::snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "Project64 Parallel RSP");
    // Project64 supplies RDRAM, DMEM and IMEM in dword byte-swapped form.
    PluginInfo->Reserved1 = true;
    PluginInfo->Reserved2 = false;
}

EXPORT void CALL GetRspDebugInfo(RSPDEBUG_INFO * DebugInfo)
{
    if (DebugInfo != nullptr)
    {
        std::memset(DebugInfo, 0, sizeof(*DebugInfo));
    }
}

EXPORT void CALL InitiateRSP(RSP_INFO RspInfo, uint32_t * CycleCount)
{
    if (CycleCount != nullptr)
    {
        *CycleCount = 0;
    }

    g_Initialized = false;
    g_RspInfo = RspInfo;
    ConfigureIntegrationState(RspInfo);

    if (RspInfo.DMEM == nullptr || RspInfo.IMEM == nullptr || RspInfo.RDRAM == nullptr ||
        RspInfo.DMEM == RspInfo.IMEM || RspInfo.SP_STATUS_REG == nullptr ||
        RspInfo.SP_PC_REG == nullptr || RspInfo.MI_INTR_REG == nullptr)
    {
        return;
    }

    EnsureCpu();

    auto ** registers = g_Cpu->get_state().cp0.cr;
    registers[RSP::CP0_REGISTER_DMA_CACHE] = RspInfo.SP_MEM_ADDR_REG;
    registers[RSP::CP0_REGISTER_DMA_DRAM] = RspInfo.SP_DRAM_ADDR_REG;
    registers[RSP::CP0_REGISTER_DMA_READ_LENGTH] = RspInfo.SP_RD_LEN_REG;
    registers[RSP::CP0_REGISTER_DMA_WRITE_LENGTH] = RspInfo.SP_WR_LEN_REG;
    registers[RSP::CP0_REGISTER_SP_STATUS] = RspInfo.SP_STATUS_REG;
    registers[RSP::CP0_REGISTER_DMA_FULL] = RspInfo.SP_DMA_FULL_REG;
    registers[RSP::CP0_REGISTER_DMA_BUSY] = RspInfo.SP_DMA_BUSY_REG;
    registers[RSP::CP0_REGISTER_SP_SEMAPHORE] = RspInfo.SP_SEMAPHORE_REG;
    registers[RSP::CP0_REGISTER_CMD_START] = RspInfo.DPC_START_REG;
    registers[RSP::CP0_REGISTER_CMD_END] = RspInfo.DPC_END_REG;
    registers[RSP::CP0_REGISTER_CMD_CURRENT] = RspInfo.DPC_CURRENT_REG;
    registers[RSP::CP0_REGISTER_CMD_STATUS] = RspInfo.DPC_STATUS_REG;
    registers[RSP::CP0_REGISTER_CMD_CLOCK] = RspInfo.DPC_CLOCK_REG;
    registers[RSP::CP0_REGISTER_CMD_BUSY] = RspInfo.DPC_BUFBUSY_REG;
    registers[RSP::CP0_REGISTER_CMD_PIPE_BUSY] = RspInfo.DPC_PIPEBUSY_REG;
    registers[RSP::CP0_REGISTER_CMD_TMEM_BUSY] = RspInfo.DPC_TMEM_REG;

    *RspInfo.SP_STATUS_REG = SP_STATUS_HALT;
    g_Cpu->get_state().cp0.irq = RspInfo.MI_INTR_REG;
    g_Cpu->set_dmem(reinterpret_cast<uint32_t *>(RspInfo.DMEM));
    g_Cpu->set_imem(reinterpret_cast<uint32_t *>(RspInfo.IMEM));
    g_Cpu->set_rdram(reinterpret_cast<uint32_t *>(RspInfo.RDRAM));
	RSP::SP_STATUS_TIMEOUT = 0x7fff;
    g_Initialized = true;
}

EXPORT void CALL InitiateRSPDebugger(DEBUG_INFO /* DebugInfo */)
{
}

EXPORT void CALL EnableDebugging(int /* Enabled */)
{
}

EXPORT uint32_t CALL DoRspCycles(uint32_t Cycles)
{
    if (!HasRegisters())
    {
        return 0;
    }

    if ((*g_RspInfo.SP_STATUS_REG & (SP_STATUS_HALT | SP_STATUS_BROKE)) != 0)
    {
        return 0;
    }

    ScopedRspPerformanceSample performance_sample;

    // Project64 may modify IMEM between RSP tasks.
    g_Cpu->invalidate_imem();
    g_Cpu->get_state().pc = *g_RspInfo.SP_PC_REG & 0x0fff;
	std::memset(RSP::MFC0_count, 0, sizeof(RSP::MFC0_count));

    while ((*g_RspInfo.SP_STATUS_REG & SP_STATUS_HALT) == 0)
    {
        const auto mode = g_Cpu->run();
        if (mode == RSP::MODE_CHECK_FLAGS && (*g_Cpu->get_state().cp0.irq & 1) != 0)
        {
            break;
        }
    }

    *g_RspInfo.SP_PC_REG = 0x04001000 | (g_Cpu->get_state().pc & 0x0ffc);

    // Match upstream parallel-rsp's task completion path. In particular, a
    // task which neither halts nor raises an interrupt must enter the short
    // timeout path used by cxd4; otherwise its following task can observe an
    // incomplete RDP command list.
    if ((*g_RspInfo.SP_STATUS_REG & SP_STATUS_BROKE) != 0)
    {
        return Cycles;
    }
    else if ((*g_Cpu->get_state().cp0.irq & 1) != 0)
    {
        if (g_RspInfo.CheckInterrupts != nullptr)
        {
            g_RspInfo.CheckInterrupts();
        }
    }
    else if ((*g_RspInfo.SP_STATUS_REG & SP_STATUS_HALT) != 0)
    {
        return Cycles;
    }
    else if (*g_RspInfo.SP_SEMAPHORE_REG != 0)
    {
        // Semaphore lock: preserve the initial timeout budget.
    }
    else
    {
        RSP::SP_STATUS_TIMEOUT = 16;
    }

    *g_RspInfo.SP_STATUS_REG &= ~SP_STATUS_HALT;
    return Cycles;
}

EXPORT void CALL RomOpen(void)
{
}

EXPORT void CALL RomClosed(void)
{
    // Intentionally host-free: see ClearHostState.
}

EXPORT void CALL PluginLoaded(void)
{
}
