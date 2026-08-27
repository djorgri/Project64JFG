#pragma once

#include <windows.h>

struct GraphicsSettings
{
    int window_width = 1280;
    int window_height = 960;
    int upscaling = 2;
    int downscale_steps = 1;
    int overscan_crop = 0;

    bool fullscreen = false;
    bool vsync = false;
    bool force_widescreen = false;
    bool integer_scaling = true;
    bool bob_deinterlacing = true;
    bool synchronous_rdp = true;
    bool super_sampled_dither = true;
    bool super_sampled_readback = true;
    bool native_texture_lod = true;
    bool native_texrects = true;
    bool persist_frame_on_invalid_input = false;
    bool blend_previous_frame = false;

    bool vi_aa = true;
    bool vi_divot = true;
    bool vi_dither_filter = true;
    bool vi_bilinear = true;
    bool vi_gamma_dither = true;
    bool vi_serrate = true;
};

void LoadGraphicsSettings(HINSTANCE module, GraphicsSettings &settings);
void SaveGraphicsSettings(HINSTANCE module, const GraphicsSettings &settings);
bool ShowGraphicsSettingsDialog(HINSTANCE module, HWND parent, GraphicsSettings &settings);
