#include "Settings.h"
#include "resource.h"

#include <commctrl.h>
#include <cstdio>
#include <string>

namespace
{
const char *ConfigSection = "Settings";

std::string config_path(HINSTANCE module)
{
    char path[MAX_PATH] = {};
    if (!GetModuleFileNameA(module, path, sizeof(path)))
        return "Project64-ParallelRDP.ini";

    std::string result(path);
    const auto separator = result.find_last_of("\\/");
    if (separator == std::string::npos)
        return "Project64-ParallelRDP.ini";

    result.resize(separator + 1);
    result += "Project64-ParallelRDP.ini";
    return result;
}

int read_int(const std::string &path, const char *key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntA(ConfigSection, key, fallback, path.c_str()));
}

void write_int(const std::string &path, const char *key, int value)
{
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d", value);
    WritePrivateProfileStringA(ConfigSection, key, text, path.c_str());
}

void set_checked(HWND dialog, int id, bool value)
{
    CheckDlgButton(dialog, id, value ? BST_CHECKED : BST_UNCHECKED);
}

bool checked(HWND dialog, int id)
{
    return IsDlgButtonChecked(dialog, id) == BST_CHECKED;
}

void add_tooltip(HWND tooltip, HWND dialog, int id, const char *text)
{
    const auto control = GetDlgItem(dialog, id);
    if (!control)
        return;

    TTTOOLINFOA tool = {};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = dialog;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<char *>(text);
    SendMessageA(tooltip, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&tool));
}

void initialize_tooltips(HWND dialog)
{
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);

    const auto tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, dialog, nullptr, nullptr, nullptr);
    if (!tooltip)
        return;

    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageA(tooltip, TTM_SETMAXTIPWIDTH, 0, 340);
    SendMessageA(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 350);

    add_tooltip(tooltip, dialog, IDC_WINDOW_SIZE,
        "Sets the 4:3 client size for windowed presentation. It does not change internal N64 rendering resolution.");
    add_tooltip(tooltip, dialog, IDC_FULLSCREEN,
        "Presents Project64 fullscreen while keeping the selected display aspect ratio.");
    add_tooltip(tooltip, dialog, IDC_VSYNC,
        "Synchronizes presentation with the desktop compositor. Reduces tearing but can add a little latency.");
    add_tooltip(tooltip, dialog, IDC_FORCE_WIDESCREEN,
        "Stretches the N64 image to a 16:9 frame. Disable it for the original 4:3 aspect ratio.");
    add_tooltip(tooltip, dialog, IDC_INTEGER_SCALING,
        "Uses an integer horizontal scale when possible, then applies the N64's vertical pixel-aspect correction.");

    add_tooltip(tooltip, dialog, IDC_UPSCALING,
        "Renders the RDP at native, 2x, 4x, or 8x resolution. 2x or 4x is recommended for Jet Force Gemini.");
    add_tooltip(tooltip, dialog, IDC_DOWNSCALING,
        "Reduces the upscaled VI image before display. For example, 4x with 1/2 downscaling produces a filtered 2x output.");
    add_tooltip(tooltip, dialog, IDC_DEINTERLACER,
        "Bob doubles each interlaced field; Weave combines fields. Bob is safer for moving N64 content.");
    add_tooltip(tooltip, dialog, IDC_SYNCHRONOUS_RDP,
        "Waits for the RDP before the CPU continues. Required for Jet Force Gemini framebuffer effects; may lower performance.");
    add_tooltip(tooltip, dialog, IDC_SUPER_DITHER,
        "Uses high-resolution data to reconstruct N64 dithering while upscaling. Has no effect at native resolution.");
    add_tooltip(tooltip, dialog, IDC_SUPER_READBACK,
        "Uses high-resolution framebuffer data for RDRAM reads while upscaling. Has no effect at native resolution.");
    add_tooltip(tooltip, dialog, IDC_NATIVE_LOD,
        "Computes texture level of detail as at native resolution. Helps games that rely on original N64 mipmap selection.");
    add_tooltip(tooltip, dialog, IDC_NATIVE_TEXRECTS,
        "Draws TEX_RECT commands at native scale to avoid 2D and bilinear-filtering glitches. Recommended for most games.");

    add_tooltip(tooltip, dialog, IDC_VI_AA,
        "Applies the N64 Video Interface anti-aliasing filter based on coverage values.");
    add_tooltip(tooltip, dialog, IDC_VI_DIVOT,
        "Applies the N64 divot filter, which removes single-pixel artifacts from the VI output.");
    add_tooltip(tooltip, dialog, IDC_VI_DITHER,
        "Applies the N64 VI dither reconstruction filter.");
    add_tooltip(tooltip, dialog, IDC_VI_BILINEAR,
        "Applies the N64 VI bilinear scaling filter to the final image.");
    add_tooltip(tooltip, dialog, IDC_VI_GAMMA,
        "Applies the N64 gamma and dither stage to the final image.");
    add_tooltip(tooltip, dialog, IDC_VI_SERRATE,
        "Enables the N64 interlace serration behavior for video modes that use it.");
    add_tooltip(tooltip, dialog, IDC_OVERSCAN,
        "Crops pixels from each edge of the final VI image to hide overscan borders.");
    add_tooltip(tooltip, dialog, IDC_PERSIST_INVALID,
        "Keeps the last valid image through brief invalid VI states. This is enabled automatically while upscaling.");
    add_tooltip(tooltip, dialog, IDC_BLEND_PREVIOUS,
        "Blends the previous image into the next one. Useful with Weave interlacing, but can introduce ghosting.");
    add_tooltip(tooltip, dialog, IDC_HARDWARE_PRESET,
        "Restores native internal RDP rendering and the original VI filters. Use 2x or 4x in Jet Force Gemini if interface text is incomplete.");
}

void add_combo_item(HWND dialog, int id, const char *text, int value)
{
    const auto combo = GetDlgItem(dialog, id);
    const auto index = SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageA(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(value));
}

void select_combo_value(HWND dialog, int id, int value)
{
    const auto combo = GetDlgItem(dialog, id);
    const auto count = SendMessageA(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; index++)
    {
        if (static_cast<int>(SendMessageA(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)) == value)
        {
            SendMessageA(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
            return;
        }
    }
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
}

int selected_combo_value(HWND dialog, int id, int fallback)
{
    const auto combo = GetDlgItem(dialog, id);
    const auto index = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR)
        return fallback;
    const auto value = SendMessageA(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    return value == CB_ERR ? fallback : static_cast<int>(value);
}

void apply_hardware_preset(GraphicsSettings &settings)
{
    settings = GraphicsSettings{};
}

void initialize_dialog(HWND dialog, GraphicsSettings &settings)
{
	SendMessageA(GetDlgItem(dialog, IDC_WINDOW_SIZE), CB_RESETCONTENT, 0, 0);
	SendMessageA(GetDlgItem(dialog, IDC_UPSCALING), CB_RESETCONTENT, 0, 0);
	SendMessageA(GetDlgItem(dialog, IDC_DOWNSCALING), CB_RESETCONTENT, 0, 0);
	SendMessageA(GetDlgItem(dialog, IDC_DEINTERLACER), CB_RESETCONTENT, 0, 0);
	SendMessageA(GetDlgItem(dialog, IDC_OVERSCAN), CB_RESETCONTENT, 0, 0);

	add_combo_item(dialog, IDC_WINDOW_SIZE, "640 x 480", 640);
    add_combo_item(dialog, IDC_WINDOW_SIZE, "800 x 600", 800);
    add_combo_item(dialog, IDC_WINDOW_SIZE, "1024 x 768", 1024);
    add_combo_item(dialog, IDC_WINDOW_SIZE, "1280 x 960", 1280);
    add_combo_item(dialog, IDC_WINDOW_SIZE, "1600 x 1200", 1600);
    add_combo_item(dialog, IDC_WINDOW_SIZE, "1920 x 1440", 1920);

    add_combo_item(dialog, IDC_UPSCALING, "None", 1);
    add_combo_item(dialog, IDC_UPSCALING, "2x", 2);
    add_combo_item(dialog, IDC_UPSCALING, "4x", 4);
    add_combo_item(dialog, IDC_UPSCALING, "8x", 8);

    add_combo_item(dialog, IDC_DOWNSCALING, "None", 0);
    add_combo_item(dialog, IDC_DOWNSCALING, "1/2", 1);
    add_combo_item(dialog, IDC_DOWNSCALING, "1/4", 2);
    add_combo_item(dialog, IDC_DOWNSCALING, "1/8", 3);

    add_combo_item(dialog, IDC_DEINTERLACER, "Bob", 1);
    add_combo_item(dialog, IDC_DEINTERLACER, "Weave", 0);

    add_combo_item(dialog, IDC_OVERSCAN, "None", 0);
    add_combo_item(dialog, IDC_OVERSCAN, "4 pixels", 4);
    add_combo_item(dialog, IDC_OVERSCAN, "8 pixels", 8);
    add_combo_item(dialog, IDC_OVERSCAN, "12 pixels", 12);

    select_combo_value(dialog, IDC_WINDOW_SIZE, settings.window_width);
    select_combo_value(dialog, IDC_UPSCALING, settings.upscaling);
    select_combo_value(dialog, IDC_DOWNSCALING, settings.downscale_steps);
    select_combo_value(dialog, IDC_DEINTERLACER, settings.bob_deinterlacing ? 1 : 0);
    select_combo_value(dialog, IDC_OVERSCAN, settings.overscan_crop);

    set_checked(dialog, IDC_FULLSCREEN, settings.fullscreen);
    set_checked(dialog, IDC_VSYNC, settings.vsync);
    set_checked(dialog, IDC_FORCE_WIDESCREEN, settings.force_widescreen);
    set_checked(dialog, IDC_INTEGER_SCALING, settings.integer_scaling);
    set_checked(dialog, IDC_SYNCHRONOUS_RDP, settings.synchronous_rdp);
    set_checked(dialog, IDC_SUPER_DITHER, settings.super_sampled_dither);
    set_checked(dialog, IDC_SUPER_READBACK, settings.super_sampled_readback);
    set_checked(dialog, IDC_NATIVE_LOD, settings.native_texture_lod);
    set_checked(dialog, IDC_NATIVE_TEXRECTS, settings.native_texrects);
    set_checked(dialog, IDC_VI_AA, settings.vi_aa);
    set_checked(dialog, IDC_VI_DIVOT, settings.vi_divot);
    set_checked(dialog, IDC_VI_DITHER, settings.vi_dither_filter);
    set_checked(dialog, IDC_VI_BILINEAR, settings.vi_bilinear);
    set_checked(dialog, IDC_VI_GAMMA, settings.vi_gamma_dither);
    set_checked(dialog, IDC_VI_SERRATE, settings.vi_serrate);
    set_checked(dialog, IDC_PERSIST_INVALID, settings.persist_frame_on_invalid_input);
    set_checked(dialog, IDC_BLEND_PREVIOUS, settings.blend_previous_frame);
}

void read_dialog(HWND dialog, GraphicsSettings &settings)
{
    settings.window_width = selected_combo_value(dialog, IDC_WINDOW_SIZE, settings.window_width);
    settings.window_height = settings.window_width * 3 / 4;
    settings.upscaling = selected_combo_value(dialog, IDC_UPSCALING, settings.upscaling);
    settings.downscale_steps = selected_combo_value(dialog, IDC_DOWNSCALING, settings.downscale_steps);
    settings.bob_deinterlacing = selected_combo_value(dialog, IDC_DEINTERLACER, 1) != 0;
    settings.overscan_crop = selected_combo_value(dialog, IDC_OVERSCAN, settings.overscan_crop);

    settings.fullscreen = checked(dialog, IDC_FULLSCREEN);
    settings.vsync = checked(dialog, IDC_VSYNC);
    settings.force_widescreen = checked(dialog, IDC_FORCE_WIDESCREEN);
    settings.integer_scaling = checked(dialog, IDC_INTEGER_SCALING);
    settings.synchronous_rdp = checked(dialog, IDC_SYNCHRONOUS_RDP);
    settings.super_sampled_dither = checked(dialog, IDC_SUPER_DITHER);
    settings.super_sampled_readback = checked(dialog, IDC_SUPER_READBACK);
    settings.native_texture_lod = checked(dialog, IDC_NATIVE_LOD);
    settings.native_texrects = checked(dialog, IDC_NATIVE_TEXRECTS);
    settings.vi_aa = checked(dialog, IDC_VI_AA);
    settings.vi_divot = checked(dialog, IDC_VI_DIVOT);
    settings.vi_dither_filter = checked(dialog, IDC_VI_DITHER);
    settings.vi_bilinear = checked(dialog, IDC_VI_BILINEAR);
    settings.vi_gamma_dither = checked(dialog, IDC_VI_GAMMA);
    settings.vi_serrate = checked(dialog, IDC_VI_SERRATE);
    settings.persist_frame_on_invalid_input = checked(dialog, IDC_PERSIST_INVALID);
    settings.blend_previous_frame = checked(dialog, IDC_BLEND_PREVIOUS);
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *settings = reinterpret_cast<GraphicsSettings *>(GetWindowLongPtr(dialog, DWLP_USER));
    switch (message)
    {
    case WM_INITDIALOG:
        settings = reinterpret_cast<GraphicsSettings *>(lparam);
        SetWindowLongPtr(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(settings));
        initialize_dialog(dialog, *settings);
        initialize_tooltips(dialog);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_HARDWARE_PRESET:
            apply_hardware_preset(*settings);
            initialize_dialog(dialog, *settings);
            return TRUE;

        case IDOK:
            read_dialog(dialog, *settings);
            EndDialog(dialog, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
} // namespace

void LoadGraphicsSettings(HINSTANCE module, GraphicsSettings &settings)
{
    const auto path = config_path(module);
    settings.window_width = read_int(path, "WindowWidth", settings.window_width);
    settings.window_height = settings.window_width * 3 / 4;
    settings.upscaling = read_int(path, "Upscaling", settings.upscaling);
    settings.downscale_steps = read_int(path, "DownscaleSteps", settings.downscale_steps);
    settings.overscan_crop = read_int(path, "OverscanCrop", settings.overscan_crop);

    settings.fullscreen = read_int(path, "Fullscreen", settings.fullscreen) != 0;
    settings.vsync = read_int(path, "VSync", settings.vsync) != 0;
    settings.force_widescreen = read_int(path, "ForceWidescreen", settings.force_widescreen) != 0;
    settings.integer_scaling = read_int(path, "IntegerScaling", settings.integer_scaling) != 0;
    settings.bob_deinterlacing = read_int(path, "BobDeinterlacing", settings.bob_deinterlacing) != 0;
    settings.synchronous_rdp = read_int(path, "SynchronousRDP", settings.synchronous_rdp) != 0;
    settings.super_sampled_dither = read_int(path, "SuperSampledDither", settings.super_sampled_dither) != 0;
    settings.super_sampled_readback = read_int(path, "SuperSampledReadback", settings.super_sampled_readback) != 0;
    settings.native_texture_lod = read_int(path, "NativeTextureLod", settings.native_texture_lod) != 0;
    settings.native_texrects = read_int(path, "NativeTexRects", settings.native_texrects) != 0;
    settings.persist_frame_on_invalid_input = read_int(path, "PersistInvalidFrame", settings.persist_frame_on_invalid_input) != 0;
    settings.blend_previous_frame = read_int(path, "BlendPreviousFrame", settings.blend_previous_frame) != 0;

    settings.vi_aa = read_int(path, "ViAA", settings.vi_aa) != 0;
    settings.vi_divot = read_int(path, "ViDivot", settings.vi_divot) != 0;
    settings.vi_dither_filter = read_int(path, "ViDitherFilter", settings.vi_dither_filter) != 0;
    settings.vi_bilinear = read_int(path, "ViBilinear", settings.vi_bilinear) != 0;
    settings.vi_gamma_dither = read_int(path, "ViGammaDither", settings.vi_gamma_dither) != 0;
    settings.vi_serrate = read_int(path, "ViSerrate", settings.vi_serrate) != 0;
}

void SaveGraphicsSettings(HINSTANCE module, const GraphicsSettings &settings)
{
    const auto path = config_path(module);
    write_int(path, "WindowWidth", settings.window_width);
    write_int(path, "Upscaling", settings.upscaling);
    write_int(path, "DownscaleSteps", settings.downscale_steps);
    write_int(path, "OverscanCrop", settings.overscan_crop);

    write_int(path, "Fullscreen", settings.fullscreen);
    write_int(path, "VSync", settings.vsync);
    write_int(path, "ForceWidescreen", settings.force_widescreen);
    write_int(path, "IntegerScaling", settings.integer_scaling);
    write_int(path, "BobDeinterlacing", settings.bob_deinterlacing);
    write_int(path, "SynchronousRDP", settings.synchronous_rdp);
    write_int(path, "SuperSampledDither", settings.super_sampled_dither);
    write_int(path, "SuperSampledReadback", settings.super_sampled_readback);
    write_int(path, "NativeTextureLod", settings.native_texture_lod);
    write_int(path, "NativeTexRects", settings.native_texrects);
    write_int(path, "PersistInvalidFrame", settings.persist_frame_on_invalid_input);
    write_int(path, "BlendPreviousFrame", settings.blend_previous_frame);

    write_int(path, "ViAA", settings.vi_aa);
    write_int(path, "ViDivot", settings.vi_divot);
    write_int(path, "ViDitherFilter", settings.vi_dither_filter);
    write_int(path, "ViBilinear", settings.vi_bilinear);
    write_int(path, "ViGammaDither", settings.vi_gamma_dither);
    write_int(path, "ViSerrate", settings.vi_serrate);
}

bool ShowGraphicsSettingsDialog(HINSTANCE module, HWND parent, GraphicsSettings &settings)
{
    return DialogBoxParamA(module, MAKEINTRESOURCEA(IDD_PARALLEL_RDP_SETTINGS), parent,
                           dialog_proc, reinterpret_cast<LPARAM>(&settings)) == IDOK;
}
