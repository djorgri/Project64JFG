#pragma once

// The plugin keeps both private RAMs between replays: it restores only what the
// previous replay drew and mirrors only the RDRAM the replay loads. Compare that
// with a full rewrite of both RAMs over frames that move, add and remove draws.
static void test_hud_private_ram(Vulkan::Device &device)
{
    using namespace JfgHudLayer;

    // Spans follow Renderer::load_tile's address arithmetic, 64-byte aligned,
    // wrapping at the private RAM size.
    {
        TextureImage image;std::vector<Span> spans;
        const std::vector<uint32_t> loads={
            2,0xFD10000B,0x1000,     // RGBA16, 12 texels per row
            2,0xF4000000,0x0002C004, // LoadTile 12x2: 48 bytes
            2,0xF4000010,0x0002C000, // LoadTile with TH < TL: no-op
            2,0xFD100000,0x2000,     // width 1
            2,0xF3000000,0x0707F800, // LoadBlock 128 texels: 256 bytes
            2,0xF3010000,0x0700F800, // LoadBlock with SH < SL: no-op
            2,0xFD10001F,0x7FFFC0,   // width 32
            2,0xF0000000,0x0703C000, // LoadTLUT 16 entries: 32 bytes
            2,0xFD100000,0xFFFFC0,
            2,0xF3000000,0x0707F800, // 256 bytes, wrapping past the end of the private RAM
        };
        load_spans(loads,image,spans);
        require(image.address==0xFFFFC0 && image.width==1 && image.size==2,"texture image state not carried");
        merge_spans(spans);
        const std::vector<std::pair<uint32_t,uint32_t>> expected={
            {0,0xC0},{0x1000,0x1040},{0x2000,0x2100},{0x7FFFC0,0x800000},{0xFFFFC0,RamSize}};
        std::vector<std::pair<uint32_t,uint32_t>> actual;
        for(const auto &s:spans)actual.push_back({s.begin,s.end});
        require(actual==expected,"load spans");
    }

    const unsigned flags=RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
        RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT;
    auto create=[&]() {
        auto p=std::make_unique<RDP::CommandProcessor>(device,nullptr,0,RamSize,RamSize/2,flags);
        require(p->device_is_supported(),"private RAM test device support");
        RDP::Quirks quirks;quirks.set_native_hud_coordinates(true);p->set_quirks(quirks);
        const uint32_t bind[]={0xFF100000|(Width-1),ColorBase};
        p->enqueue_command(2,bind);p->idle();
        return p;
    };
    std::array<std::unique_ptr<RDP::CommandProcessor>,2> full={create(),create()},kept={create(),create()};
    std::vector<uint8_t> guest(ColorBase,0);

    Capture capture;
    auto command=[&](std::initializer_list<uint32_t> words,bool marker=false) {
        capture.command(words.begin(),unsigned(words.size()),marker);
    };
    auto build=[&](unsigned frame) {
        const unsigned k=frame%3;
        command({0xFF10013F,0x00100000});
        command({0xED000000,(320*4<<12)|(240*4)});
        command({0xE7000000,JfgHudRaster::Marker|1},true);
        command({0xEF000CF0,0x00404240});
        command({0xFCFFFFFF,0xFFFDF6FB});
        command({0xFA000000,0xFF000080});
        if(k!=2) {
            // A 9x12 guest rectangle, then a triangle, both moving between frames.
            const unsigned x=15+40*k,y=10+30*k;
            command({0xF6000000|((x+9)*4<<12)|((y+12)*4),(x*4<<12)|(y*4)});
            command({0xFA000000,0x00FF00FF});
            // Z-buffered, so the depth image and its hidden dz bits change too.
            command({0xEF000CF0,0x00404270});
            command({0xC9800000|((62+20*k)*4),((50+20*k)*4<<16)|((50+20*k)*4),
                     (24+30*k)<<16,uint32_t(-49152),(15+30*k)<<16,0,(24+30*k)<<16,0,
                     0x10000000,0,0,0});
            command({0xEF000CF0,0x00404240});
        }
        command({0xE7000000,JfgHudRaster::Marker|2},true);
        command({0xE7000000,JfgHudRaster::Marker|3},true);
        // The third frame reuses the previous SetTextureImage.
        if(k!=2) command({0xFD10000B,k?0x3000u:0x1000u});
        command({0xF5100600,0x00080200});
        command({0xF4000000,0x0002C004});
        command({0xF2000000,0x0002C004});
        command({0xFCFFFFFF,0xFFFCF279});
        const unsigned tx=15+60*k,ty=40+20*k;
        command({0xE4000000|((tx+9)*4<<12)|((ty+2)*4),(tx*4<<12)|(ty*4),0,0x05550400});
        command({0xE7000000,JfgHudRaster::Marker|4},true);
        command({0xE9000000,0});
    };

    bool initialized=false;TextureImage image;DrawBounds drawn;std::vector<Span> spans;
    for(unsigned frame=0;frame<7;++frame)
    {
        // Guest textures change every frame, so a stale mirror cannot pass.
        for(uint32_t base:{0x1000u,0x3000u}) {
            auto texture=reinterpret_cast<uint16_t *>(guest.data()+base);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<12;++x)
                texture[(y*12+x)^1]=uint16_t(((5+x*2+y+frame+base/0x1000)&31)<<6|(frame&1?0x800:0)|1);
        }
        build(frame);
        const auto previous=drawn;
        const bool fresh=!initialized || previous.unknown;
        spans.clear();load_spans(capture.commands,image,spans);merge_spans(spans);
        drawn.scan(capture.commands);initialized=true;
        require(!drawn.unknown,"private draws classified as unknown");
        for(unsigned background=0;background<2;++background)
        {
            const uint16_t color=background?0xFFFF:0x0001;
            auto &reference=full[background];reference->idle();
            auto ram=static_cast<uint8_t *>(reference->begin_read_rdram());
            std::memcpy(ram,guest.data(),ColorBase);
            clear_all(ram,static_cast<uint8_t *>(reference->begin_read_hidden_rdram()),reference->get_hidden_rdram_size(),color);
            reference->end_write_rdram();reference->end_write_hidden_rdram();
            reference->enqueue_command_batch(unsigned(capture.commands.size()),capture.commands.data());

            auto &p=kept[background];p->idle();
            ram=static_cast<uint8_t *>(p->begin_read_rdram());
            auto hidden=static_cast<uint8_t *>(p->begin_read_hidden_rdram());
            if(fresh)clear_all(ram,hidden,p->get_hidden_rdram_size(),color);
            else restore(ram,hidden,color,previous);
            mirror(ram,guest.data(),uint32_t(guest.size()),spans);
            p->end_write_rdram();p->end_write_hidden_rdram();
            p->enqueue_command_batch(unsigned(capture.commands.size()),capture.commands.data());
        }
        std::array<const uint8_t *,2> fullRam,keptRam;
        for(unsigned background=0;background<2;++background)
        {
            full[background]->idle();kept[background]->idle();
            fullRam[background]=static_cast<const uint8_t *>(full[background]->begin_read_rdram());
            keptRam[background]=static_cast<const uint8_t *>(kept[background]->begin_read_rdram());
            require(std::memcmp(fullRam[background]+ColorBase,keptRam[background]+ColorBase,2*Groups*ImageBytes)==0,
                "kept private images differ from a full rewrite");
            auto fullHidden=static_cast<const uint8_t *>(full[background]->begin_read_hidden_rdram());
            auto keptHidden=static_cast<const uint8_t *>(kept[background]->begin_read_hidden_rdram());
            require(std::memcmp(fullHidden+ColorBase/2,keptHidden+ColorBase/2,Groups*ImageBytes)==0,
                "kept hidden coverage differs from a full rewrite");
        }
        for(unsigned group=0;group<2;++group)
        {
            const auto expected=extract(fullRam[0],fullRam[1],group,capture.layers[group]);
            const auto actual=extract(keptRam[0],keptRam[1],group,capture.layers[group],drawn.colors[group]);
            bool same=expected.x==actual.x && expected.y==actual.y && expected.width==actual.width &&
                expected.height==actual.height && expected.pixels.size()==actual.pixels.size();
            for(size_t i=0;same && i<expected.pixels.size();++i)
                same=expected.pixels[i].color==actual.pixels[i].color && expected.pixels[i].transmit==actual.pixels[i].transmit;
            require(same,"extraction within the drawn rectangle differs");
            require((frame%3==2 && group==0)==expected.pixels.empty(),"test frame did not draw as intended");
        }
        capture.next();
    }
    std::cout<<"HUD private RAMs: load spans, kept images/coverage, texture mirror, narrowed extraction: OK\n";
}
