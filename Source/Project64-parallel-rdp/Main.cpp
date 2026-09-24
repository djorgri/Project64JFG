#include <windows.h>
#include <dwmapi.h>

#include <Video.h>
#include "Settings.h"
#include "JfgReticleOverlay.h"
#include "JfgHudRaster.h"
#include "JfgHudLayer.h"

#include <context.hpp>
#include <device.hpp>
#include <wsi.hpp>
#include <rdp_device.hpp>
#include <global_managers_init.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t RdpCommandLengths[64] = {
	8, 8, 8, 8, 8, 8, 8, 8,
	32, 48, 96, 112, 96, 112, 160, 176,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 16, 16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

class Project64WSIPlatform final : public Vulkan::WSIPlatform
{
public:
	explicit Project64WSIPlatform(HWND window) : m_window(window)
	{
		update_dimensions();
	}

	VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice) override
	{
		VkWin32SurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		info.hinstance = GetModuleHandleW(nullptr);
		info.hwnd = m_window;

		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		update_dimensions();
		return surface;
	}

	std::vector<const char *> get_instance_extensions() override
	{
		return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
	}

	uint32_t get_surface_width() override
	{
		update_dimensions();
		return m_width;
	}

	uint32_t get_surface_height() override
	{
		update_dimensions();
		return m_height;
	}

	const VkApplicationInfo *get_application_info() override
	{
		static const VkApplicationInfo info = {
			VK_STRUCTURE_TYPE_APPLICATION_INFO,
			nullptr,
			"Project64 Parallel RDP", 0,
			"Project64", 0,
			VK_API_VERSION_1_3,
		};
		return &info;
	}

	bool alive(Vulkan::WSI &) override
	{
		update_dimensions();
		return m_window != nullptr && IsWindow(m_window) && m_width != 0 && m_height != 0;
	}

	void poll_input() override
	{
		update_dimensions();
	}

	void poll_input_async(Granite::InputTrackerHandler *) override
	{
	}

	uintptr_t get_native_window() override
	{
		return reinterpret_cast<uintptr_t>(m_window);
	}

private:
	void update_dimensions()
	{
		RECT rect = {};
		if (!m_window || !GetClientRect(m_window, &rect))
			return;

		const auto width = static_cast<unsigned>(std::max<LONG>(0, rect.right - rect.left));
		const auto height = static_cast<unsigned>(std::max<LONG>(0, rect.bottom - rect.top));
		if (width != m_width || height != m_height)
		{
			m_width = width;
			m_height = height;
			resize = true;
		}
	}

	HWND m_window = nullptr;
	unsigned m_width = 0;
	unsigned m_height = 0;
};

std::mutex g_mutex;
GFX_INFO g_gfx = {};
std::unique_ptr<Vulkan::Context> g_context;
std::unique_ptr<Vulkan::Device> g_device;
std::unique_ptr<RDP::CommandProcessor> g_processor;
std::vector<uint32_t> g_pending_rdp_words;
std::vector<uint32_t> g_pending_rdp_command_batch;
std::vector<RDP::RGBA> g_scanout_colors;
std::vector<uint32_t> g_display_pixels;
unsigned g_scanout_width = 0;
unsigned g_scanout_height = 0;
std::vector<RDP::RGBA> g_last_visible_scanout;
unsigned g_last_visible_width = 0;
unsigned g_last_visible_height = 0;
unsigned g_consecutive_black_scanouts = 0;
JfgReticleOverlay::Queue g_reticle_queue;
JfgHudRaster::State g_hud_raster;
std::array<std::unique_ptr<RDP::CommandProcessor>, 2> g_hud_processors;
JfgHudLayer::Capture g_hud_capture;
JfgHudLayer::Bindings g_hud_bindings;
JfgReticleOverlay::View g_reticle_view, g_last_reticle_view;
// Reading a VI image back to the CPU is needed by the current GDI presenter,
// but it must not make the emulation thread wait for the GPU. Keep a small
// ring of host-visible copies and present the newest completed image instead.
struct AsyncScanout
{
    RDP::VIScanoutBuffer buffer;
    uint64_t sequence = 0;
    JfgReticleOverlay::View reticle;
};
constexpr size_t AsyncScanoutCount = 3;
constexpr size_t RdpCommandBatchFlushWords = 1024;
std::array<AsyncScanout, AsyncScanoutCount> g_async_scanouts;
uint64_t g_next_scanout_sequence = 1;

// Coarse integration-layer counters. They distinguish GPU completion stalls
// from presentation back-pressure without enabling Parallel RDP's verbose
// developer tracing.
struct PerformanceCounters
{
	LARGE_INTEGER frequency = {};
	LARGE_INTEGER window_start = {};
	uint64_t sync_wait_ticks = 0;
	uint64_t presentation_ticks = 0;
	uint64_t rdp_callback_ticks = 0;
	uint64_t rdp_lock_ticks = 0;
	uint64_t rdp_batch_enqueue_ticks = 0;
	uint32_t sync_wait_count = 0;
	uint32_t present_count = 0;
	uint32_t rdp_callback_count = 0;
	uint32_t rdp_batch_count = 0;
	uint32_t rdp_command_count = 0;
	uint32_t rdp_word_count = 0;
	uint32_t scanouts_queued = 0;
	uint32_t scanouts_completed = 0;
	uint32_t scanouts_skipped = 0;
	uint32_t hud_health_scopes = 0, hud_weapon_scopes = 0, hud_text_scopes = 0, hud_primitives = 0;
	uint64_t hud_render_ticks = 0;
};
PerformanceCounters g_performance;
bool g_global_managers_initialized = false;
std::atomic_uint32_t g_trace_count = 0;
HINSTANCE g_module = nullptr;
GraphicsSettings g_settings;
bool g_settings_loaded = false;

struct WindowPresentationState
{
	HWND window = nullptr;
	RECT windowed_rect = {};
	LONG_PTR windowed_style = 0;
	LONG_PTR windowed_exstyle = 0;
	bool fullscreen = false;
};

WindowPresentationState g_window_presentation;

// Match the Project64 Parallel RDP hardware preset: render at 2x, then
// downsample once. This preserves crisp 2D TEX_RECT elements while retaining
// the VI's filtering.
void clamp_settings()
{
	// 320x240 used to be offered briefly, but cannot display every Project64
	// control reliably. Preserve a sensible 4:3 window size for existing INIs.
	if (g_settings.window_width == 320)
		g_settings.window_width = 640;
	if (g_settings.window_width < 640 || g_settings.window_width > 1920 || (g_settings.window_width * 3) % 4 != 0)
		g_settings.window_width = 1280;
	g_settings.window_height = g_settings.window_width * 3 / 4;
	if (g_settings.upscaling != 1 && g_settings.upscaling != 2 &&
		g_settings.upscaling != 4 && g_settings.upscaling != 8)
		g_settings.upscaling = 2;
	g_settings.downscale_steps = std::max(0, std::min(3, g_settings.downscale_steps));
	g_settings.overscan_crop = std::max(0, std::min(12, g_settings.overscan_crop));
}

void load_settings_once()
{
	if (!g_settings_loaded)
	{
		LoadGraphicsSettings(g_module, g_settings);
		clamp_settings();
		g_settings_loaded = true;
	}
}

RDP::CommandProcessorFlags render_flags()
{
	RDP::CommandProcessorFlags flags = 0;
	switch (g_settings.upscaling)
	{
	case 2: flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT; break;
	case 4: flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT; break;
	case 8: flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT; break;
	default: break;
	}
	// The upstream renderer rejects supersampled readback at native resolution.
	// Keep the preference visible in the UI, but treat it as a no-op until an
	// internal upscale is selected.
	if (g_settings.upscaling > 1 && g_settings.super_sampled_readback)
		flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
	if (g_settings.upscaling > 1 && g_settings.super_sampled_dither)
		flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;
	return flags;
}

void set_fullscreen(HWND window, bool fullscreen)
{
	if (!window || !IsWindow(window))
		return;

	if (fullscreen && !g_window_presentation.fullscreen)
	{
		g_window_presentation.window = window;
		GetWindowRect(window, &g_window_presentation.windowed_rect);
		g_window_presentation.windowed_style = GetWindowLongPtr(window, GWL_STYLE);
		g_window_presentation.windowed_exstyle = GetWindowLongPtr(window, GWL_EXSTYLE);

		MONITORINFO monitor = { sizeof(monitor) };
		GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
		SetWindowLongPtr(window, GWL_STYLE, g_window_presentation.windowed_style & ~(WS_CAPTION | WS_THICKFRAME));
		SetWindowLongPtr(window, GWL_EXSTYLE, g_window_presentation.windowed_exstyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
			monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
		g_window_presentation.fullscreen = true;
	}
	else if (!fullscreen && g_window_presentation.fullscreen && g_window_presentation.window == window)
	{
		SetWindowLongPtr(window, GWL_STYLE, g_window_presentation.windowed_style);
		SetWindowLongPtr(window, GWL_EXSTYLE, g_window_presentation.windowed_exstyle);
		SetWindowPos(window, HWND_NOTOPMOST, g_window_presentation.windowed_rect.left, g_window_presentation.windowed_rect.top,
			g_window_presentation.windowed_rect.right - g_window_presentation.windowed_rect.left,
			g_window_presentation.windowed_rect.bottom - g_window_presentation.windowed_rect.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
		g_window_presentation.fullscreen = false;
	}
}

void apply_display_settings()
{
	const auto window = static_cast<HWND>(g_gfx.hWnd);
	if (!window || !IsWindow(window))
		return;

	set_fullscreen(window, g_settings.fullscreen);
	if (g_settings.fullscreen)
		return;

	RECT rect = { 0, 0, g_settings.window_width, g_settings.window_height };
	AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtr(window, GWL_STYLE)),
		GetMenu(window) != nullptr, static_cast<DWORD>(GetWindowLongPtr(window, GWL_EXSTYLE)));
	SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// Keep the first events in a small on-disk trace. This makes Vulkan startup
// failures diagnosable from a normal Project64 launch without a debugger.
void trace_event(const char *event)
{
	if (g_trace_count.fetch_add(1) >= 512)
		return;

	char executable[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, executable, sizeof(executable));
	std::string path(executable);
	const auto separator = path.find_last_of("\\/");
	if (separator == std::string::npos)
		return;
	path.resize(separator + 1);
	path += "Logs\\Project64-ParallelRDP.log";

	FILE *file = nullptr;
	if (fopen_s(&file, path.c_str(), "a") == 0 && file != nullptr)
	{
		std::fprintf(file, "%s\n", event);
		std::fclose(file);
	}
}

void trace_eventf(const char *format, ...)
{
	char message[192] = {};
	va_list args;
	va_start(args, format);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);
	trace_event(message);
}

void reset_performance_counters()
{
	g_performance = {};
	QueryPerformanceFrequency(&g_performance.frequency);
	QueryPerformanceCounter(&g_performance.window_start);
}

void report_performance_if_due()
{
	if (g_performance.frequency.QuadPart == 0)
		reset_performance_counters();

	LARGE_INTEGER now = {};
	QueryPerformanceCounter(&now);
	const auto elapsed_ticks = now.QuadPart - g_performance.window_start.QuadPart;
	if (elapsed_ticks < g_performance.frequency.QuadPart)
		return;

	const auto elapsed_seconds = double(elapsed_ticks) / double(g_performance.frequency.QuadPart);
	const auto wait_ms = double(g_performance.sync_wait_ticks) * 1000.0 /
		double(g_performance.frequency.QuadPart);
	const auto wait_per_sync_ms = g_performance.sync_wait_count != 0 ?
		wait_ms / double(g_performance.sync_wait_count) : 0.0;
	const auto presentation_ms = double(g_performance.presentation_ticks) * 1000.0 /
		double(g_performance.frequency.QuadPart);
	const auto callback_ms = double(g_performance.rdp_callback_ticks) * 1000.0 /
		double(g_performance.frequency.QuadPart);
	const auto callback_lock_ms = double(g_performance.rdp_lock_ticks) * 1000.0 /
		double(g_performance.frequency.QuadPart);
	const auto batch_enqueue_ms = double(g_performance.rdp_batch_enqueue_ticks) * 1000.0 /
		double(g_performance.frequency.QuadPart);
	char executable[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, executable, sizeof(executable));
	std::string path(executable);
	const auto separator = path.find_last_of("\\/");
	if (separator == std::string::npos)
		return;
	path.resize(separator + 1);
	path += "Logs\\Project64-ParallelRDP.log";

	char message[512] = {};
	_snprintf_s(message, sizeof(message), _TRUNCATE,
		"perf: present %.1f Hz, present CPU %.2f ms, SyncFull %u waits %.2f ms (%.2f ms/wait), RDP callback %u %.2f ms lock %.2f ms batch %u %.2f ms cmds/words %u/%u, scanout queued/ready/skipped %u/%u/%u, HUD overlay health/weapon/text/primitives %u/%u/%u/%u GPU+readback %.2f ms",
		double(g_performance.present_count) / elapsed_seconds,
		presentation_ms,
		g_performance.sync_wait_count, wait_ms, wait_per_sync_ms,
		g_performance.rdp_callback_count, callback_ms, callback_lock_ms,
		g_performance.rdp_batch_count, batch_enqueue_ms,
		g_performance.rdp_command_count, g_performance.rdp_word_count,
		g_performance.scanouts_queued, g_performance.scanouts_completed, g_performance.scanouts_skipped,
		g_performance.hud_health_scopes, g_performance.hud_weapon_scopes, g_performance.hud_text_scopes, g_performance.hud_primitives,
		double(g_performance.hud_render_ticks) * 1000.0 / double(g_performance.frequency.QuadPart));
	FILE *file = nullptr;
	if (fopen_s(&file, path.c_str(), "a") == 0 && file != nullptr)
	{
		std::fprintf(file, "%s\n", message);
		std::fclose(file);
	}
	reset_performance_counters();
}

void clear_hud_overlay()
{
    // All three processors share Granite's thread-index-zero pools. Finish
    // scene recording before private processor destruction touches the Device.
    if (g_processor && g_hud_processors[0]) g_processor->idle();
    for (auto &processor : g_hud_processors)
    {
        if (processor) processor->idle();
        processor.reset();
    }
    g_hud_raster = {};
    g_hud_capture = {};
    g_hud_bindings = {};
    // The HUD is baked into scanouts now. A reset/state load must discard
    // those images too, instead of only clearing a separate overlay handle.
    for (auto &scanout : g_async_scanouts)
    {
        if (scanout.buffer.fence) scanout.buffer.fence->wait();
        scanout.buffer.fence.reset();
        scanout.sequence = 0;
    }
    g_scanout_colors.clear(); g_scanout_width = g_scanout_height = 0;
    g_last_visible_scanout.clear(); g_consecutive_black_scanouts = 0;
}

void destroy_renderer()
{
    clear_hud_overlay();
    g_reticle_queue.clear();
    g_reticle_view = {};
    g_last_reticle_view = {};
	g_pending_rdp_words.clear();
	g_pending_rdp_command_batch.clear();
	for (auto &scanout : g_async_scanouts)
	{
		scanout.buffer.fence.reset();
		scanout.buffer.buffer.reset();
		scanout.buffer.width = 0;
		scanout.buffer.height = 0;
        scanout.sequence = 0;
        scanout.reticle = {};
	}
	g_next_scanout_sequence = 1;
	g_scanout_colors.clear();
	g_display_pixels.clear();
	g_scanout_width = 0;
	g_scanout_height = 0;
	g_last_visible_scanout.clear();
	g_last_visible_width = 0;
	g_last_visible_height = 0;
	g_consecutive_black_scanouts = 0;
	g_processor.reset();
	if (g_device)
	{
		g_device->wait_idle();
		g_device.reset();
	}
	g_context.reset();
}

bool create_renderer()
{
	if (g_processor)
		return true;
	if (!g_gfx.hWnd || !g_gfx.RDRAM || g_gfx.RDRAM_SIZE == 0)
	{
		trace_event("renderer: missing Project64 window or RDRAM");
		return false;
	}

	if (!Vulkan::Context::init_loader(nullptr))
	{
		trace_event("renderer: Vulkan loader initialization failed");
		return false;
	}

	if (!g_global_managers_initialized)
	{
		Granite::Global::init(Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
		g_global_managers_initialized = true;
	}

	Vulkan::Context::SystemHandles handles = {};
	handles.filesystem = GRANITE_FILESYSTEM();

	g_context = std::make_unique<Vulkan::Context>();
	g_context->set_system_handles(handles);
	static const VkApplicationInfo application_info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO,
		nullptr,
		"Project64 Parallel RDP", 0,
		"Project64", 0,
		VK_API_VERSION_1_3,
	};
	g_context->set_application_info(&application_info);
	if (!g_context->init_instance_and_device(nullptr, 0, nullptr, 0))
	{
		trace_event("renderer: Vulkan initialization failed");
		destroy_renderer();
		return false;
	}

	g_device = std::make_unique<Vulkan::Device>();
	g_device->set_context(*g_context);
	auto &device = *g_device;
	// Prefer imported host RDRAM. Besides avoiding copies, this is essential
	// for titles such as Jet Force Gemini which render a surface, then read and
	// filter it immediately on the CPU (e.g. character blob shadows).
	// Parallel RDP safely falls back to its copy path if the driver rejects the
	// Project64 allocation.
	_putenv_s("PARALLEL_RDP_ALLOW_EXTERNAL_HOST", "1");
	// The command ring copies every RDP command before returning, so the RSP
	// callback can keep emulating while Parallel RDP prepares Vulkan work on its
	// worker thread. SyncFull drains that ring before the CPU is signalled.
	_putenv_s("PARALLEL_RDP_SINGLE_THREADED_COMMAND", "0");
	// Normal asynchronous pipeline compilation avoids avoidable stalls whenever
	// a scene first uses a new shader variant. The renderer's ubershader fallback
	// keeps rendering correct while that compilation completes.
	_putenv_s("PARALLEL_RDP_FORCE_SYNC_SHADER", "0");
	// Shaders address one hidden byte per 16-bit RDRAM word (the low two
	// coverage bits), not a packed two-bit array. VI fetch uses that same index.
	g_processor = std::make_unique<RDP::CommandProcessor>(
		device, g_gfx.RDRAM, 0, g_gfx.RDRAM_SIZE, g_gfx.RDRAM_SIZE / 2,
		render_flags());
	if (!g_processor->device_is_supported())
	{
		trace_event("renderer: Parallel RDP device is unsupported");
		destroy_renderer();
		return false;
	}

	trace_event("renderer: ready");
	return true;
}

bool create_hud_renderers()
{
    if (g_hud_processors[0] && g_hud_processors[1]) return true;
    if (!create_renderer()) return false;
    g_processor->idle();
    // Two native backgrounds let the original RDP blender determine both
    // foreground color and transmission. No scene color is present in either.
    // Separate processors retain identical TMEM/state between completion points.
    for (auto &p : g_hud_processors)
    {
        p = std::make_unique<RDP::CommandProcessor>(*g_device, nullptr, 0,
            JfgHudLayer::RamSize, JfgHudLayer::RamSize / 2, // One coverage byte per halfword.
            RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
            RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT);
        if (!p->device_is_supported()) { clear_hud_overlay(); return false; }
        RDP::Quirks quirks;
        quirks.set_native_hud_coordinates(true);
        p->set_quirks(quirks);
        const uint32_t bind[] = { 0xFF100000 | (JfgHudLayer::Width - 1), JfgHudLayer::ColorBase };
        p->enqueue_command(2, bind);
        p->idle();
    }
    trace_event("JFG HUD: native isolated overlay renderers ready (serialized command recording)");
    return true;
}

void render_hud_layers()
{
    if (!g_hud_processors[0] || g_hud_capture.commands.empty()) return;
    // SyncFull has idled the scene processor. Private command recording stays
    // on this calling thread: separate worker threads would all register as
    // Granite index zero and race in descriptor/command-pool allocation. GPU
    // execution can still overlap; both replays complete before readback.
    LARGE_INTEGER start = {}, end = {};
    QueryPerformanceCounter(&start);
    for (unsigned background = 0; background < 2; ++background)
    {
        auto &p = g_hud_processors[background];
        auto ram = static_cast<uint8_t *>(p->begin_read_rdram());
        std::memcpy(ram, g_gfx.RDRAM, std::min(g_gfx.RDRAM_SIZE, JfgHudLayer::ColorBase));
        auto color = reinterpret_cast<uint16_t *>(ram + JfgHudLayer::ColorBase);
        std::fill(color, color + JfgHudLayer::Groups * JfgHudLayer::ImageBytes / 2, background ? 0xFFFF : 0x0001);
        std::memset(ram + JfgHudLayer::DepthBase, 0xFF, JfgHudLayer::Groups * JfgHudLayer::ImageBytes);
        p->end_write_rdram();
        // Coverage belongs to this fresh pair of backgrounds, not to the
        // preceding frame's antialiased edges.
        std::memset(p->begin_read_hidden_rdram(), 3, p->get_hidden_rdram_size());
        p->end_write_hidden_rdram();
        p->enqueue_command_batch(unsigned(g_hud_capture.commands.size()), g_hud_capture.commands.data());
    }
    for (auto &p : g_hud_processors) p->idle();
    auto black = static_cast<const uint8_t *>(g_hud_processors[0]->begin_read_rdram());
    auto white = static_cast<const uint8_t *>(g_hud_processors[1]->begin_read_rdram());
    for (const auto &target : g_hud_capture.touched)
    {
        auto frame = std::make_shared<JfgHudLayer::Frame>(); frame->target = target;
        for (const auto &fade : g_hud_capture.fades)
            if (fade.target.address == target.address) frame->fades.push_back(fade);
        for (unsigned group = 0; group < 2; ++group)
            if (g_hud_capture.layers[group].address == target.address)
            {
                auto layer = JfgHudLayer::extract(black, white, group, target);
                layer.order = g_hud_capture.layerOrders[group];
                if (!layer.pixels.empty()) frame->layers.push_back(std::move(layer));
            }
        for (const auto &glyph : g_hud_capture.glyphs)
            if (glyph.target.address == target.address)
                frame->layers.push_back(JfgHudLayer::extract_glyph(black, white, glyph));
        // Empty frames remove the old HUD when a menu/scene replaces it.
        g_hud_bindings.publish(std::move(frame));
    }
    g_hud_capture.next();
    QueryPerformanceCounter(&end);
    g_performance.hud_render_ticks += end.QuadPart - start.QuadPart;
}

void update_vi_registers()
{
	if (!g_processor)
		return;

	const auto set = [](RDP::VIRegister reg, const uint32_t *value) {
		if (value)
			g_processor->set_vi_register(reg, *value);
	};

	set(RDP::VIRegister::Control, g_gfx.VI_STATUS_REG);
	set(RDP::VIRegister::Origin, g_gfx.VI_ORIGIN_REG);
	set(RDP::VIRegister::Width, g_gfx.VI_WIDTH_REG);
	set(RDP::VIRegister::Intr, g_gfx.VI_INTR_REG);
	set(RDP::VIRegister::VCurrentLine, g_gfx.VI_V_CURRENT_LINE_REG);
	set(RDP::VIRegister::Timing, g_gfx.VI_TIMING_REG);
	set(RDP::VIRegister::VSync, g_gfx.VI_V_SYNC_REG);
	set(RDP::VIRegister::HSync, g_gfx.VI_H_SYNC_REG);
	set(RDP::VIRegister::Leap, g_gfx.VI_LEAP_REG);
	set(RDP::VIRegister::HStart, g_gfx.VI_H_START_REG);
	set(RDP::VIRegister::VStart, g_gfx.VI_V_START_REG);
	set(RDP::VIRegister::VBurst, g_gfx.VI_V_BURST_REG);
	set(RDP::VIRegister::XScale, g_gfx.VI_X_SCALE_REG);
	set(RDP::VIRegister::YScale, g_gfx.VI_Y_SCALE_REG);
}

void signal_dp_interrupt()
{
	// SyncFull tells the N64 CPU that the display processor completed a list.
	// Project64's original video plugin and the 64DD Parallel port both raise
	// this interrupt from the graphics plugin itself.
	if (g_gfx.MI_INTR_REG)
		*g_gfx.MI_INTR_REG |= 0x20;
	if (g_gfx.CheckInterrupts)
		g_gfx.CheckInterrupts();
}

uint32_t read_rdp_word(uint32_t address)
{
	if (g_gfx.DPC_STATUS_REG && (*g_gfx.DPC_STATUS_REG & 1) != 0)
		return reinterpret_cast<const uint32_t *>(g_gfx.DMEM)[(address & 0xfff) >> 2];
	return reinterpret_cast<const uint32_t *>(g_gfx.RDRAM)[address >> 2];
}

void flush_pending_rdp_command_batch()
{
	if (!g_processor || g_pending_rdp_command_batch.empty())
		return;

	// The vector already contains the length-prefixed format expected by the
	// command ring. Submit it under one producer lock instead of unpacking it
	// into individually locked commands. Callers keep SyncFull at the boundary.
	g_processor->enqueue_command_batch(static_cast<unsigned>(g_pending_rdp_command_batch.size()),
	                                 g_pending_rdp_command_batch.data());
	g_pending_rdp_command_batch.clear();
}

void blit_scanout()
{
	if (!g_gfx.hWnd || g_scanout_width == 0 || g_scanout_height == 0 || g_scanout_colors.empty())
		return;

	g_display_pixels.resize(g_scanout_colors.size());
	for (size_t index = 0; index < g_scanout_colors.size(); index++)
	{
		const auto &pixel = g_scanout_colors[index];
		g_display_pixels[index] = (uint32_t(pixel.r) << 16) |
			(uint32_t(pixel.g) << 8) | uint32_t(pixel.b);
	}

	RECT client = {};
	const auto window = static_cast<HWND>(g_gfx.hWnd);
	if (!GetClientRect(window, &client))
		return;
	const int client_width = client.right - client.left;
	const int client_height = client.bottom - client.top;
	if (client_width <= 0 || client_height <= 0)
		return;
	// The VI scanout dimensions describe a framebuffer, not square display
	// pixels. In particular, many N64 modes use a 320x224 or 640x448 buffer
	// which the VI presents as 4:3. Keeping the raw buffer ratio here therefore
	// squashes the native-resolution preset. The N64 display ratio is 4:3 unless
	// the user explicitly requests the widescreen presentation override.
	const double target_aspect = g_settings.force_widescreen ? 16.0 / 9.0 : 4.0 / 3.0;
	int target_width = client_width;
	int target_height = static_cast<int>(target_width / target_aspect);
	if (target_height > client_height)
	{
		target_height = client_height;
		target_width = static_cast<int>(target_height * target_aspect);
	}
	if (g_settings.integer_scaling)
	{
		// Preserve an integer horizontal scale (the meaningful dimension for the
		// framebuffer) while still applying the VI's non-square-pixel conversion
		// vertically to the selected display aspect ratio. If the scanout is
		// already larger than the window (e.g. 4x internal upscale), an integer
		// scale of one would crop the image. Keep the aspect-fitted target instead.
		const int scale = target_width / static_cast<int>(g_scanout_width);
		if (scale > 0)
		{
			target_width = static_cast<int>(g_scanout_width) * scale;
			target_height = static_cast<int>(target_width / target_aspect);
		}
	}
	const int target_x = (client_width - target_width) / 2;
	const int target_y = (client_height - target_height) / 2;

	BITMAPINFO bitmap = {};
	bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
	bitmap.bmiHeader.biWidth = static_cast<LONG>(g_scanout_width);
	bitmap.bmiHeader.biHeight = -static_cast<LONG>(g_scanout_height);
	bitmap.bmiHeader.biPlanes = 1;
	bitmap.bmiHeader.biBitCount = 32;
	bitmap.bmiHeader.biCompression = BI_RGB;

	if (const auto dc = GetDC(window))
	{
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, target_x, target_y, target_width, target_height, 0, 0,
            g_scanout_width, g_scanout_height, g_display_pixels.data(), &bitmap,
            DIB_RGB_COLORS, SRCCOPY);
		// Do not clear the whole client area before StretchDIBits. With the
		// larger scanout produced by internal upscaling, DWM can occasionally
		// composite the window between those two GDI operations and reveal a
		// one-frame black flash. Only paint the actual letterbox/pillarbox bars,
		// and do it after the new game image has been copied.
		const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		if (target_x > 0)
		{
			RECT left = { 0, 0, target_x, client_height };
			RECT right = { target_x + target_width, 0, client_width, client_height };
			FillRect(dc, &left, black);
			FillRect(dc, &right, black);
		}
		if (target_y > 0)
		{
			RECT top = { 0, 0, client_width, target_y };
			RECT bottom = { 0, target_y + target_height, client_width, client_height };
			FillRect(dc, &top, black);
			FillRect(dc, &bottom, black);
		}
		if (g_settings.vsync)
			DwmFlush();
		ReleaseDC(window, dc);
	}
}

bool is_completely_black(const std::vector<RDP::RGBA> &colors)
{
	return !colors.empty() && std::all_of(colors.begin(), colors.end(), [](const RDP::RGBA &color) {
		return color.r == 0 && color.g == 0 && color.b == 0;
	});
}

void select_scanout_for_presentation(std::vector<RDP::RGBA> &&colors, unsigned width, unsigned height,
                                    const JfgReticleOverlay::View &reticle)
{
	if (colors.empty() || width == 0 || height == 0)
		return;

	const bool black = is_completely_black(colors);
	const bool can_hold_previous = black && !g_last_visible_scanout.empty() &&
		width == g_last_visible_width && height == g_last_visible_height &&
		g_consecutive_black_scanouts < 3;
	if (can_hold_previous)
	{
		g_consecutive_black_scanouts++;
		g_scanout_colors = g_last_visible_scanout;
		g_scanout_width = g_last_visible_width;
		g_scanout_height = g_last_visible_height;
		g_reticle_view = g_last_reticle_view;
		return;
	}

	g_scanout_colors = std::move(colors);
	g_scanout_width = width;
	g_scanout_height = height;
	g_reticle_view = black ? JfgReticleOverlay::View{} : reticle;
	if (black)
	{
		g_consecutive_black_scanouts++;
	}
	else
	{
		g_last_visible_scanout = g_scanout_colors;
		g_last_visible_width = width;
		g_last_visible_height = height;
		g_last_reticle_view = g_reticle_view;
		g_consecutive_black_scanouts = 0;
	}
}

bool consume_completed_scanout()
{
	if (!g_device)
		return false;

	AsyncScanout *newest = nullptr;
	for (auto &scanout : g_async_scanouts)
	{
		if (!scanout.buffer.fence || !scanout.buffer.fence->wait_timeout(0))
			continue;
		if (newest == nullptr || scanout.sequence > newest->sequence)
			newest = &scanout;
	}
	if (newest == nullptr)
		return false;
	g_performance.scanouts_completed++;

	if (newest->buffer.buffer && newest->buffer.width != 0 && newest->buffer.height != 0)
	{
		std::vector<RDP::RGBA> colors(newest->buffer.width * newest->buffer.height);
		const void *mapped = g_device->map_host_buffer(*newest->buffer.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
		std::memcpy(colors.data(), mapped, colors.size() * sizeof(RDP::RGBA));
		g_device->unmap_host_buffer(*newest->buffer.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
		select_scanout_for_presentation(std::move(colors), newest->buffer.width, newest->buffer.height, newest->reticle);
	}

	// All older completed readbacks are obsolete once the newest image has been
	// selected. Reclaim their fences but keep their buffers for reuse.
	for (auto &scanout : g_async_scanouts)
	{
		if (scanout.sequence <= newest->sequence && scanout.buffer.fence &&
			scanout.buffer.fence->wait_timeout(0))
		{
			scanout.buffer.fence.reset();
			scanout.sequence = 0;
		}
	}
	return true;
}

void queue_async_scanout(const RDP::ScanoutOptions &options)
{
	if (!g_processor)
		return;

	AsyncScanout *free_scanout = nullptr;
	for (auto &scanout : g_async_scanouts)
	{
		if (!scanout.buffer.fence)
		{
			free_scanout = &scanout;
			break;
		}
	}
	if (free_scanout == nullptr)
	{
		g_performance.scanouts_skipped++;
		return;
	}

	g_processor->begin_frame_context();
	free_scanout->reticle = {};
	JfgHudLayer::View hud;
	if (g_gfx.VI_ORIGIN_REG && g_gfx.VI_X_SCALE_REG &&
		g_gfx.VI_Y_SCALE_REG && g_gfx.VI_H_START_REG && g_gfx.VI_V_START_REG)
	{
		auto &r = free_scanout->reticle;
		r.origin = *g_gfx.VI_ORIGIN_REG;
		r.frame = g_reticle_queue.find(r.origin);
		r.xscale = *g_gfx.VI_X_SCALE_REG;
		r.yscale = *g_gfx.VI_Y_SCALE_REG;
		r.hstart = *g_gfx.VI_H_START_REG;
		r.vstart = *g_gfx.VI_V_START_REG;
		r.crop = options.crop_overscan_pixels;
		r.lines = JfgBuild::vi_lines(g_gfx.VI_V_SYNC_REG ? *g_gfx.VI_V_SYNC_REG : 0);
		hud.vi = r;
		hud.vi.frame = {};
		hud.frame = g_hud_bindings.find(r.origin);
	}
	// The overlay is uploaded synchronously into a buffer owned by this GPU
	// submission. Later frames cannot change an in-flight scanout's HUD.
	auto overlay = JfgHudLayer::before_vi(hud, unsigned(g_settings.upscaling),
		g_settings.force_widescreen ? 16.0 / 9.0 : 4.0 / 3.0);
    JfgReticleOverlay::before_vi(free_scanout->reticle, unsigned(g_settings.upscaling),
        g_settings.force_widescreen ? 16.0 / 9.0 : 4.0 / 3.0, overlay);
	auto filtered_options = options;
	if (!overlay.pixels.empty()) filtered_options.overlay = &overlay;
	g_processor->scanout_async_buffer(free_scanout->buffer, filtered_options);
	if (free_scanout->buffer.fence)
	{
		free_scanout->sequence = g_next_scanout_sequence++;
		g_performance.scanouts_queued++;
	}
}

void apply_raster_quirks()
{
    if (!g_processor) return;
    flush_pending_rdp_command_batch();
    RDP::Quirks quirks;
    quirks.set_native_resolution_tex_rect(g_settings.native_texrects);
    quirks.set_native_texture_lod(g_settings.native_texture_lod);
    g_processor->set_quirks(quirks);
}

void present()
{
	if (!g_processor)
		return;
	LARGE_INTEGER presentation_start = {};
	QueryPerformanceCounter(&presentation_start);
	g_performance.present_count++;

	update_vi_registers();
	apply_raster_quirks();

	RDP::ScanoutOptions scanout_options;
	scanout_options.downscale_steps = static_cast<unsigned>(g_settings.downscale_steps);
	scanout_options.crop_overscan_pixels = static_cast<unsigned>(g_settings.overscan_crop);
	// Some games briefly strobe the VI with no valid horizontal range while
	// switching a framebuffer. At native resolution that is difficult to spot,
	// but the upscaled frame is visibly black. Parallel RDP's upstream
	// compatibility path keeps the most recent VI image for up to three such
	// transient frames and still permits intentional, sustained black screens.
	scanout_options.persist_frame_on_invalid_input =
		g_settings.persist_frame_on_invalid_input || g_settings.upscaling > 1;
	scanout_options.blend_previous_frame = g_settings.blend_previous_frame;
	scanout_options.upscale_deinterlacing = g_settings.bob_deinterlacing;
	scanout_options.vi.aa = g_settings.vi_aa;
	scanout_options.vi.scale = g_settings.vi_bilinear;
	scanout_options.vi.serrate = g_settings.vi_serrate;
	scanout_options.vi.dither_filter = g_settings.vi_dither_filter;
	scanout_options.vi.divot_filter = g_settings.vi_divot;
	scanout_options.vi.gamma_dither = g_settings.vi_gamma_dither;
	trace_event("present: scanout_async");
	consume_completed_scanout();
	queue_async_scanout(scanout_options);
	blit_scanout();
	LARGE_INTEGER presentation_end = {};
	QueryPerformanceCounter(&presentation_end);
	g_performance.presentation_ticks += presentation_end.QuadPart - presentation_start.QuadPart;
	report_performance_if_due();
	trace_event("present: complete");
}
}

EXPORT void CALL CloseDLL()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	set_fullscreen(static_cast<HWND>(g_gfx.hWnd), false);
	destroy_renderer();
}

EXPORT void CALL CaptureScreen(const char *)
{
	// Screenshot capture is handled by Project64's front-end for now.
}

EXPORT void CALL ChangeWindow()
{
	// WSI observes the updated client size during the next frame.
}

EXPORT void CALL DllAbout(void *parent)
{
	MessageBoxW(static_cast<HWND>(parent),
		L"Parallel RDP graphics plugin for Project64.\n\nRequires Vulkan 1.3.",
		L"Project64 Parallel RDP", MB_OK | MB_ICONINFORMATION);
}

EXPORT void CALL DllConfig(void *parent)
{
	GraphicsSettings updated;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		load_settings_once();
		updated = g_settings;
	}

	const auto owner = parent ? static_cast<HWND>(parent) : GetForegroundWindow();
	if (!ShowGraphicsSettingsDialog(g_module, owner, updated))
		return;

	std::lock_guard<std::mutex> lock(g_mutex);
	g_settings = updated;
	clamp_settings();
	SaveGraphicsSettings(g_module, g_settings);
	destroy_renderer();
	apply_display_settings();
}

EXPORT void CALL DrawScreen()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	blit_scanout();
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO *info)
{
	if (!info)
		return;
	std::memset(info, 0, sizeof(*info));
	info->Version = VIDEO_SPECS_VERSION;
	info->Type = PLUGIN_TYPE_VIDEO;
	std::strncpy(info->Name, "Project64 Parallel RDP", sizeof(info->Name) - 1);
	info->Reserved1 = false;
	info->Reserved2 = true;
}

EXPORT int CALL InitiateGFX(GFX_INFO info)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	destroy_renderer();
	g_gfx = info;
	JfgBuild::detect(g_gfx.HEADER);
	load_settings_once();
	apply_display_settings();
	g_trace_count = 0;
	trace_event("plugin: InitiateGFX");
	return TRUE;
}

EXPORT void CALL MoveScreen(int, int)
{
}

EXPORT void CALL PluginLoaded()
{
}

EXPORT void CALL ProcessDList()
{
	// Parallel RDP consumes low-level RDP command lists. Pair this plugin with Parallel RSP.
}

EXPORT void CALL ProcessRDPList()
{
	if (!g_gfx.DPC_CURRENT_REG || !g_gfx.DPC_END_REG || !g_gfx.DPC_START_REG)
		return;

	LARGE_INTEGER callback_start = {};
	LARGE_INTEGER callback_locked = {};
	QueryPerformanceCounter(&callback_start);
	std::unique_lock<std::mutex> lock(g_mutex);
	QueryPerformanceCounter(&callback_locked);

	const uint32_t raw_current = *g_gfx.DPC_CURRENT_REG;
	const uint32_t raw_end = *g_gfx.DPC_END_REG;
	const uint32_t current = raw_current & 0x00ffffffu;
	const uint32_t end = raw_end & 0x00ffffffu;
	// Reject genuinely empty ranges before renderer setup and tracing.
	if (end <= current || end - current > g_gfx.RDRAM_SIZE)
		return;
	if (!create_renderer())
		return;

	const auto received_word_count = (end - current) / sizeof(uint32_t);
	g_pending_rdp_words.reserve(g_pending_rdp_words.size() + received_word_count);
	g_pending_rdp_command_batch.reserve(g_pending_rdp_command_batch.size() + received_word_count * 2);
	const bool rdp_in_dmem = g_gfx.DPC_STATUS_REG && (*g_gfx.DPC_STATUS_REG & 1) != 0;
	const auto *source_words = reinterpret_cast<const uint32_t *>(rdp_in_dmem ? g_gfx.DMEM : g_gfx.RDRAM);
	const auto source_offset = rdp_in_dmem ? ((current & 0x0fffu) >> 2) : (current >> 2);
	if (!rdp_in_dmem || source_offset + received_word_count <= 0x400)
	{
		g_pending_rdp_words.insert(g_pending_rdp_words.end(), source_words + source_offset,
			source_words + source_offset + received_word_count);
	}
	else
	{
		// XBUS command lists normally stay within DMEM. Retain the wrapped
		// fallback for the unusual case where a list crosses its 4 KiB boundary.
		for (uint32_t address = current; address < end; address += sizeof(uint32_t))
			g_pending_rdp_words.push_back(read_rdp_word(address));
	}

	size_t consumed_words = 0;
	while (consumed_words < g_pending_rdp_words.size())
	{
		const auto opcode = (g_pending_rdp_words[consumed_words] >> 24) & 0x3f;
		const auto word_count = RdpCommandLengths[opcode] / sizeof(uint32_t);
		if (word_count == 0 || consumed_words + word_count > g_pending_rdp_words.size())
		{
			break;
		}
		const auto words = g_pending_rdp_words.data() + consumed_words;
		const bool marker = g_hud_raster.consume(words[0], words[1]);
		if (marker)
		{
			const auto id = words[1] & 255;
			if (id == 1) ++g_performance.hud_health_scopes;
			if (id == 3) ++g_performance.hud_weapon_scopes;
            if (id == 5 || id == 7) ++g_performance.hud_text_scopes;
		}
		const bool isolated = g_hud_processors[0] && g_hud_capture.command(words, unsigned(word_count), marker);
		if (isolated)
			++g_performance.hud_primitives;
		else
		{
			// Only HUD draw primitives are suppressed. Preserve every state
			// transition, texture upload and hardware completion in the scene.
            RDP::Quirks sceneQuirks;
            sceneQuirks.set_native_resolution_tex_rect(g_settings.native_texrects);
            sceneQuirks.set_native_texture_lod(g_settings.native_texture_lod);
            if (!(g_gfx.VI_X_SCALE_REG && g_gfx.VI_Y_SCALE_REG &&
                g_hud_capture.ordered_font_commands(words, unsigned(word_count),
                    *g_gfx.VI_X_SCALE_REG, *g_gfx.VI_Y_SCALE_REG, g_settings.overscan_crop,
                    g_settings.force_widescreen ? 16.0 / 9.0 : 4.0 / 3.0, g_pending_rdp_command_batch,
                    sceneQuirks, unsigned(g_settings.upscaling),
                    JfgBuild::vi_lines(g_gfx.VI_V_SYNC_REG ? *g_gfx.VI_V_SYNC_REG : 0))))
            {
                g_pending_rdp_command_batch.push_back(word_count);
                std::array<uint32_t, 4> ordered_number;
                if (g_hud_capture.ordered_number_rectangle(words, unsigned(word_count), ordered_number))
                    g_pending_rdp_command_batch.insert(g_pending_rdp_command_batch.end(), ordered_number.begin(), ordered_number.end());
                else
                    g_pending_rdp_command_batch.insert(g_pending_rdp_command_batch.end(), words, words + word_count);
            }
		}
		// Flush periodically so the worker keeps progressing while the RSP is
		// still producing commands. This avoids the per-command lock overhead
		// without delaying an entire frame until SyncFull.
		if (opcode != 0x29 && g_pending_rdp_command_batch.size() >= RdpCommandBatchFlushWords)
		{
			LARGE_INTEGER batch_start = {};
			LARGE_INTEGER batch_end = {};
			QueryPerformanceCounter(&batch_start);
			flush_pending_rdp_command_batch();
			QueryPerformanceCounter(&batch_end);
			g_performance.rdp_batch_enqueue_ticks += batch_end.QuadPart - batch_start.QuadPart;
			g_performance.rdp_batch_count++;
		}
		if (opcode == 0x29) // RDP SyncFull
		{
			// A truncated/abandoned HUD scope must never reach the next frame.
			g_hud_raster.drawing = 0;
			LARGE_INTEGER batch_start = {};
			LARGE_INTEGER batch_end = {};
			QueryPerformanceCounter(&batch_start);
			flush_pending_rdp_command_batch();
			QueryPerformanceCounter(&batch_end);
			g_performance.rdp_batch_enqueue_ticks += batch_end.QuadPart - batch_start.QuadPart;
			g_performance.rdp_batch_count++;
			// SyncFull is a hardware completion point. The CPU can consume the
			// framebuffer as soon as we raise its interrupt, so wait for the GPU
			// before returning to Project64. This matches ParaLLEl's synchronous
			// RDP mode and is required for Jet Force Gemini's CPU-filtered shadows.
			if (g_settings.synchronous_rdp || g_hud_processors[0])
			{
				LARGE_INTEGER sync_wait_start = {};
				LARGE_INTEGER sync_wait_end = {};
				QueryPerformanceCounter(&sync_wait_start);
				g_processor->idle();
				QueryPerformanceCounter(&sync_wait_end);
				g_performance.sync_wait_ticks += sync_wait_end.QuadPart - sync_wait_start.QuadPart;
				g_performance.sync_wait_count++;
			}
			render_hud_layers();
			signal_dp_interrupt();
		}
		g_performance.rdp_command_count++;
		g_performance.rdp_word_count += word_count;
		consumed_words += word_count;
	}
	if (consumed_words != 0)
		g_pending_rdp_words.erase(g_pending_rdp_words.begin(),
			g_pending_rdp_words.begin() + consumed_words);

	*g_gfx.DPC_START_REG = raw_end;
	*g_gfx.DPC_CURRENT_REG = raw_end;
	LARGE_INTEGER callback_end = {};
	QueryPerformanceCounter(&callback_end);
	g_performance.rdp_callback_ticks += callback_end.QuadPart - callback_start.QuadPart;
	g_performance.rdp_lock_ticks += callback_locked.QuadPart - callback_start.QuadPart;
	g_performance.rdp_callback_count++;
}

EXPORT void CALL RomClosed()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	destroy_renderer();
}

EXPORT void CALL RomOpen()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	JfgBuild::detect(g_gfx.HEADER);
}

EXPORT void CALL ShowCFB()
{
}

EXPORT void CALL SoftReset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    clear_hud_overlay();
    apply_raster_quirks();
    g_reticle_queue.clear();
    g_reticle_view = {};
    g_last_reticle_view = {};
    for (auto &scanout : g_async_scanouts) scanout.reticle = {};
}

EXPORT void CALL JfgReticleCommand(uint32_t command, uint32_t packet)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_reticle_queue.command(command, packet, g_gfx.RDRAM, g_gfx.RDRAM_SIZE);
    if (command == 0)
    {
        g_reticle_view = {};
        g_last_reticle_view = {};
        for (auto &scanout : g_async_scanouts) scanout.reticle = {};
    }
}

EXPORT void CALL JfgHudCommand(uint32_t command)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (command == 0)
    {
        clear_hud_overlay();
        apply_raster_quirks();
    }
    else if (g_hud_raster.notify(command, g_gfx.RDRAM, g_gfx.RDRAM_SIZE))
    {
        create_hud_renderers();
    }
}

EXPORT void CALL JfgHudTextCommand(uint32_t command, uint32_t display_list_pointer)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (command == 12) {
        JfgHudRaster::multiplayer_radar_point(g_gfx.RDRAM, g_gfx.RDRAM_SIZE, display_list_pointer);
        return;
    }
    if (command == 11) {
        JfgHudRaster::multiplayer_matrix(g_gfx.RDRAM, g_gfx.RDRAM_SIZE, display_list_pointer);
        return;
    }
    if ((command == 9 || command == 10) &&
        g_hud_raster.notify_number(command, g_gfx.RDRAM, g_gfx.RDRAM_SIZE, int32_t(display_list_pointer)))
        create_hud_renderers();
    if ((command == 5 || command == 6) &&
        g_hud_raster.notify(command, g_gfx.RDRAM, g_gfx.RDRAM_SIZE, display_list_pointer))
        create_hud_renderers();
}

EXPORT void CALL UpdateScreen()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (create_renderer())
		present();
}

EXPORT void CALL ViStatusChanged()
{
}

EXPORT void CALL ViWidthChanged()
{
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}
