#include "../JfgHudLayer.h"
#include <context.hpp>
#include <device.hpp>
#include <rdp_device.hpp>
#include <global_managers_init.hpp>
#include <iostream>
#include <stdexcept>
#include <windows.h>

static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
#include "JfgHudViTest.h"
#include "JfgHudFadeTest.h"
#include "JfgMapTextTest.h"

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        using namespace JfgHudLayer;
        require(Vulkan::Context::init_loader(nullptr), "Vulkan loader");
        Granite::Global::init(Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
        Vulkan::Context context;
        Vulkan::Context::SystemHandles handles = {}; handles.filesystem = GRANITE_FILESYSTEM();
        context.set_system_handles(handles);
        const VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
            "HUD layer regression", 0, "Project64", 0, VK_API_VERSION_1_3 };
        context.set_application_info(&application);
        require(context.init_instance_and_device(nullptr, 0, nullptr, 0), "Vulkan device");
        Vulkan::Device device; device.set_context(context);
        std::array<std::unique_ptr<RDP::CommandProcessor>, 2> processors;
        auto createHud = [&]() {
            for (auto &p : processors)
            {
                p.reset();
                p = std::make_unique<RDP::CommandProcessor>(device, nullptr, 0, RamSize, RamSize / 2,
                    RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
                    RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT);
                require(p->device_is_supported(), "RDP support");
                RDP::Quirks quirks; quirks.set_native_hud_coordinates(true); p->set_quirks(quirks);
            }
        };
        createHud();
        Capture capture;
        auto command = [&](std::initializer_list<uint32_t> words, bool marker = false) {
            return capture.command(words.begin(), unsigned(words.size()), marker);
        };
        command({0xFF10013F, 0x00100000});
        command({0xED000000, (320 * 4 << 12) | (240 * 4)});
        // World draws must neither be suppressed nor enter the isolated batch.
        require(!command({0xF6010000, 0}), "world draw was isolated");
        command({0xE7000000, JfgHudRaster::Marker | 1}, true);
        command({0xEF000CF0, 0x00404240}); // RGBA point sample, source alpha over memory, no dithering
        command({0xFCFFFFFF, 0xFFFDF6FB}); // primitive RGBA
        command({0xFA000000, 0xFF000080});
        // A 9x12 corrected rectangle becomes 12x12 before native rasterization.
        require(command({0xF6000000 | (24 * 4 << 12) | (22 * 4), (15 * 4 << 12) | (10 * 4)}), "HUD not isolated");
        // The health ring uses triangles. Keep a separate opaque triangle to
        // exercise edge slopes as well as the rectangle path.
        command({0xFA000000, 0x00FF00FF});
        command({0xC8800000 | (62 * 4), (50 * 4 << 16) | (50 * 4),
                 24u << 16, uint32_t(-49152), 15u << 16, 0, 24u << 16, 0});
        // Ammo uses source textures compressed by dsdx=1365. A native row must
        // recover every source column, including the first and last ones.
        command({0xFD10000B, 0x1000});
        command({0xF5100600, 0x00080200});
        command({0xF4000000, 0x0002C004});
        command({0xF2000000, 0x0002C004});
        command({0xFCFFFFFF, 0xFFFCF279});
        command({0xE4000000 | (24 * 4 << 12) | (42 * 4), (15 * 4 << 12) | (40 * 4), 0, 0x05550400});
        command({0xE7000000, JfgHudRaster::Marker | 2}, true);
        command({0xE9000000, 0});
        auto prepare = [&](unsigned background, uint16_t clear) {
            auto &p = processors[background]; p->idle();
            auto ram = static_cast<uint8_t *>(p->begin_read_rdram());
            std::memset(ram, 0, ColorBase);
            auto texture = reinterpret_cast<uint16_t *>(ram + 0x1000);
            for (unsigned y=0;y<2;++y)for(unsigned x=0;x<12;++x)
                texture[(y*12+x)^1] = uint16_t(((5+x*2+y)<<6)|1);
            auto colors = reinterpret_cast<uint16_t *>(ram + ColorBase);
            std::fill(colors, colors + Groups * ImageBytes / 2, clear);
            std::memset(ram + DepthBase, 0xFF, ImageBytes * Groups);
            p->end_write_rdram();
            std::memset(p->begin_read_hidden_rdram(), 3, p->get_hidden_rdram_size());
            p->end_write_hidden_rdram();
        };
        auto render = [&](unsigned background, uint16_t clear) {
            prepare(background, clear);
            auto &p = processors[background];
            p->enqueue_command_batch(unsigned(capture.commands.size()), capture.commands.data());
            p->idle();
            return static_cast<const uint8_t *>(p->begin_read_rdram());
        };
        const auto black = render(0, 0x0001), white = render(1, 0xFFFF);
        auto layer = extract(black, white, 0, capture.layers[0]);
        std::cout << "native bounds " << layer.x << ',' << layer.y << ' ' << layer.width << 'x' << layer.height << '\n';
        require(layer.x == 20 && layer.y == 10 && layer.width == 12 && layer.height >= 51, "native rectangle/triangle bounds");
        auto sample = layer.pixels[5 * layer.width + 5];
        require(sample.color[0] > 100 && sample.color[0] < 150 && sample.color[1] == 0, "foreground blend");
        require(sample.transmit[1] > 100 && sample.transmit[1] < 150, "transparent transmission");
        const auto native = reinterpret_cast<const uint16_t *>(black + ColorBase);
        for(unsigned x=0;x<12;++x)
        {
            auto color=rgb(native[(40*Width+20+x)^1]);
            require(color[1] == rgb(uint16_t(((5+x*2)<<6)|1))[1], "native texture column lost");
        }
        require(rgb(native[(52*Width+22)^1])[1]==255 && rgb(native[(61*Width+31)^1])[1]==0, "triangle edge slope");
        // Check reconstructed transparency against an actual RDP render over gray.
        auto grayRam = render(0, 0x8421);
        auto actual = rgb(reinterpret_cast<const uint16_t *>(grayRam + ColorBase)[((15 * Width + 25) ^ 1)]);
        auto gray = rgb(0x8421);
        for (unsigned c = 0; c < 3; ++c)
        {
            int reconstructed = sample.color[c] + (gray[c] * sample.transmit[c] + 127) / 255;
            require(std::abs(reconstructed - int(actual[c])) <= 8, "RDP transparency mismatch");
        }
        // Restrict composition to the 12x12 rectangle for the square-pixel check.
        layer.height = 12; layer.pixels.resize(layer.width * layer.height);
        auto frame = std::make_shared<Frame>(); frame->target = capture.layers[0]; frame->layers.push_back(layer);
        View view; view.frame = frame;
        view.vi.origin = frame->target.address; view.vi.xscale = 512; view.vi.yscale = 1365;
        view.vi.hstart = 108u << 16; view.vi.vstart = 34u << 16;
        for (int outputWidth : {960, 1280})
        {
            std::vector<uint32_t> pixels(outputWidth * 720, 0x848484);
            composite(view, pixels.data(), outputWidth, 720);
            int x0=outputWidth,y0=720,x1=-1,y1=-1;
            for(int y=0;y<720;++y)for(int x=0;x<outputWidth;++x)if(pixels[y*outputWidth+x]!=0x848484)
            {x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
            require(x1-x0==y1-y0 && x1>=x0, "final pixels are not square");
        }
        Bindings bindings; bindings.publish(frame); auto saved=bindings.find(0x100280);
        auto empty=std::make_shared<Frame>();empty->target=frame->target;bindings.publish(empty);
        require(saved && !saved->layers.empty() && bindings.find(0x100000)->layers.empty(), "frame ownership");
        // Markers can nest; leaving the inner group must restore the outer
        // binding. Neither a foreign render target nor the next frame may
        // inherit suppression from a previous scope.
        capture.next();
        command({0xE7000000, JfgHudRaster::Marker | 1}, true);
        command({0xE7000000, JfgHudRaster::Marker | 3}, true);
        require(capture.scope == 2, "nested weapon scope");
        command({0xE7000000, JfgHudRaster::Marker | 4}, true);
        require(capture.scope == 1 && capture.commands[capture.commands.size()-4] == ColorBase, "restore outer binding");
        command({0xFF10013F, 0x00200000});
        require(!command({0xF6010000, 0}), "foreign target was isolated");
        capture.next();
        require(!command({0xF6010000, 0}) && capture.scopes.empty(), "scope leaked into next frame");
        command({0xFF18013F, 0x00100000}); // RGBA32 is deliberately unsupported.
        command({0xE7000000, JfgHudRaster::Marker | 1}, true);
        require(!command({0xF6010000, 0}), "unsupported target was isolated");
        // Text is packed into a private atlas, with the old Y expansion
        // undone before rasterization. Distant glyphs retain independent
        // positions, and each source row/column is recovered exactly.
        capture.next();
        command({0xFF10013F, 0x00100000});
        command({0xED000000, (320 * 4 << 12) | (240 * 4)});
        command({0xEF000CF0, 0x00404240});
        command({0xFD10000B, 0x1000});
        command({0xF5100600, 0x00080200});
        command({0xF4000000, 0x0002C004});
        command({0xF2000000, 0x0002C004});
        command({0xFCFFFFFF, 0xFFFCF279});
        require(!command({0xE4000000 | (192*4<<12) | (122*4), (180*4<<12) | (120*4), 0, 0x04000400}), "unscoped pause art was isolated");
        command({0xE7000000, JfgHudRaster::Marker | 5}, true);
        require(!command({0xF6010000, 0}), "font background was isolated");
        require(command({0xE4000000 | (42*4<<12) | 169, (30*4<<12) | 159, 0, 0x04000300}), "font not isolated");
        // Pause uses stock 1:1 glyphs without the gameplay Y expansion.
        require(command({0xE4000000 | (192*4<<12) | (122*4), (180*4<<12) | (120*4), 0, 0x04000400}), "pause font not isolated");
        require(!command({0xE4000000 | (42*4<<12) | 169, (30*4<<12) | 159, 0, 0x08000300}), "unknown font step suppressed");
        command({0xE7000000, JfgHudRaster::Marker | 6}, true);
        command({0xE9000000, 0});
        require(capture.glyphs.size()==2 && capture.glyphs[0].y==40 && capture.glyphs[1].y==120, "native font Y or atlas placement");
        const auto textBlack=render(0,0x0001),textWhite=render(1,0xFFFF);
        auto textFrame=std::make_shared<Frame>();textFrame->target=capture.target;
        for(const auto &g:capture.glyphs)
        {
            auto glyph=extract_glyph(textBlack,textWhite,g);
            for(int y=0;y<2;++y)for(int x=0;x<12;++x)
            {
                auto &p=glyph.pixels[y*12+x];
                require(p.color[1]==rgb(uint16_t(((5+x*2+y)<<6)|1))[1] && p.transmit[1]==0, "font source pixels lost");
            }
            textFrame->layers.push_back(std::move(glyph));
        }
        view.frame=textFrame;
        for(int outputWidth:{960,1280})
        {
            std::vector<uint32_t> pixels(outputWidth*720,0x848484);
            composite(view,pixels.data(),outputWidth,720);
            // Two separate 12x2 glyphs must remain 36x6 final pixels,
            // wherever their original text block is positioned.
            for(int index=0;index<2;++index)
            {
                int x0=outputWidth,y0=720,x1=-1,y1=-1;
                const int beginY=index?200:0,endY=index?350:150;
                for(int y=beginY;y<endY;++y)for(int x=0;x<outputWidth;++x)
                    if(pixels[y*outputWidth+x]!=0x848484)
                    {x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
                require(x1-x0+1==36 && y1-y0+1==6, "text pixels stretched");
                require(x0==(index?180:30)*outputWidth/320, "text anchor moved");
            }
        }
        textFrame->layers.resize(1);
        textFrame->layers[0].clip={31*4,0,41*4,240*4};
        std::vector<uint32_t> clipped(960*720,0x848484);composite(view,clipped.data(),960,720);
        for(int y=0;y<720;++y)for(int x=0;x<960;++x)
            if(x<93 || x>=123)require(clipped[y*960+x]==0x848484,"font escaped clipping window");
        std::cout << "Font atlas/native texels/independent anchors/square pixels/clipping: OK\n";
        test_hud_before_vi(device);
        test_hud_fade_order(device);
        test_map_text_order(device);
        if (argc > 1 && std::string(argv[1]) == "--stress")
        {
            command({0xE7000000, JfgHudRaster::Marker | 5}, true);
            for (unsigned i=0;i<300;++i)
                require(command({0xE4000000 | (42*4<<12) | 169, (30*4<<12) | 159, 0, 0x04000300}), "stress atlas capacity");
            command({0xE7000000, JfgHudRaster::Marker | 6}, true);
            command({0xE9000000, 0});
            // Match the plugin: one asynchronous scene processor plus two HUD
            // processors, all sharing the same Device and thread-index pools.
            RDP::CommandProcessor scene(device,nullptr,0,ColorBase,ColorBase/2,0);
            require(scene.device_is_supported(),"scene processor support");
            const uint32_t sceneBatch[]={2,0xFF10013F,0x00100000,
                2,0xED000000,(320*4<<12)|(240*4),2,0xEF3000F0,0,
                2,0xF7000000,0x84218421,2,0xF6000000|(319*4<<12)|(239*4),0,
                2,0xE9000000,0};
            for (unsigned iteration=0;iteration<600;++iteration)
            {
                scene.enqueue_command_batch(unsigned(sizeof(sceneBatch)/4),sceneBatch);
                scene.idle();
                if(iteration && iteration%200==0)createHud();
                prepare(0,0x0001); prepare(1,0xFFFF);
                for(auto &p:processors)p->enqueue_command_batch(unsigned(capture.commands.size()),capture.commands.data());
                for(auto &p:processors)p->idle();
                const auto ram=static_cast<const uint8_t *>(processors[0]->begin_read_rdram());
                const auto texels=reinterpret_cast<const uint16_t *>(ram+ColorBase+2*ImageBytes);
                require(rgb(texels[1])[1]==41,"stress glyph readback corrupted");
                scene.begin_frame_context();
                if(iteration%100==0)std::cout<<"Stress frame "<<iteration<<std::endl;
            }
            std::cout << "600 frames with scene/HUD replay, frame-context recycling and HUD recreation: OK\n";
        }
        std::cout << "HUD GPU/native dimensions/transparency/composition/frame ownership: OK\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
