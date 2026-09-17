#pragma once

static void test_hud_fade_order(Vulkan::Device &device)
{
    using namespace JfgHudLayer;
    Capture cap;
    auto cmd = [&](std::initializer_list<uint32_t> w, bool marker = false) {
        return cap.command(w.begin(), unsigned(w.size()), marker);
    };
    cmd({0xFF10013F,0x100000});
    cmd({0xED000000,(320*4<<12)|(240*4)});
    cmd({0xEF000CF0,0x00504340});
    cmd({0xFCFFFFFF,0xFFFDF6FB});
    cmd({0xFA000000,0x00000080});
    const uint32_t rectangle = 0xF6000000|(320*4<<12)|(240*4);
    require(!cmd({rectangle,0}),"fade removed from scene");
    cmd({0xE7000000,JfgHudRaster::Marker|5},true);
    require(cmd({0xE4000000|(48*4<<12)|(48*4),(40*4<<12)|(40*4),0,0x04000400}),"fade test glyph capture");
    cmd({0xE7000000,JfgHudRaster::Marker|6},true);
    cmd({0xED000000|(42*4<<12)|(41*4),(46*4<<12)|(47*4)});
    require(!cmd({rectangle,0}),"later fade removed from scene");
    require(cap.fades.size()==2 && cap.fades[0].order<cap.glyphs[0].order &&
        cap.fades[1].order>cap.glyphs[0].order,"fade submission order");
    require(cap.fades[1].clip==std::array<int,4>{168,164,184,188},"fade scissor lost");
    // Texture-dependent combiners, depth-tested/unknown blends and fill-cycle
    // clears must never be mistaken for a uniform source-alpha fade.
    cmd({0xFCFFFFFF,0xFFFCF279});cmd({rectangle,0});
    cmd({0xFCFFFFFF,0xFFFDF6FB});cmd({0xEF000CF0,0x00504350});cmd({rectangle,0});
    cmd({0xEF300CF0,0x00504340});cmd({rectangle,0});
    require(cap.fades.size()==2,"foreign primitive classified as fade");
    cap.next();require(cap.fades.empty() && cap.order==0,"fade leaked into next frame");
    cmd({0xEF000CF0,0x00504340});cmd({rectangle,0});
    require(cap.fades.size()==1,"RDP state lost across SyncFull");

    auto f=std::make_shared<Frame>();f->target={0x100000,320,240};
    Layer l;l.target=f->target;l.text=true;l.x=40;l.y=40;l.width=8;l.height=8;
    l.order=2;l.clip={0,0,1280,960};l.pixels.resize(64,Pixel{{200,100,40},{0,0,0}});
    f->layers.push_back(l);
    View v;v.frame=f;v.vi.xscale=512;v.vi.yscale=1024;
    auto colorAt=[&](unsigned x,unsigned y) {
        auto o=before_vi(v,1,16.0/9.0);return o.pixels[(y*320+x)*2];
    };
    f->fades={{f->target,1,{0,0,1280,960},0x000000FF}};
    require(colorAt(43,43)==0xC86428,"earlier fade darkened later text");
    f->fades[0].order=3;
    require(colorAt(43,43)==0,"opaque fade did not hide text");
    f->fades[0].rgba=0x00000080;
    require(colorAt(43,43)==0x643214,"half fade wrong");
    f->fades[0].target.address+=0x1000;
    require(colorAt(43,43)==0xC86428,"fade affected another framebuffer");
    f->fades[0].target=f->target;f->fades[0].clip={42*4,41*4,46*4,47*4};
    require(colorAt(41,43)==0xC86428 && colorAt(43,43)==0x643214,"partial fade bounds");
    f->fades.push_back({f->target,4,{0,0,1280,960},0xFFFFFF80});
    require(colorAt(43,43)==0xB2998A,"successive fades reordered");

    // Compare against the real RDP drawing HUD first, then the fade. Background
    // has already been faded on the scene path; transparent glyph edges must
    // not fade it twice. RGB555 quantization may differ by one 5-bit step.
    const unsigned ramSize=4*1024*1024;
    RDP::CommandProcessor p(device,nullptr,0,ramSize,ramSize/2,
        RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
        RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT);
    require(p.device_is_supported(),"fade RDP device");
    auto render=[&](uint16_t background,bool hud,const std::vector<uint32_t> &fades) {
        p.idle();auto ram=static_cast<uint8_t *>(p.begin_read_rdram());
        std::memset(ram,0,ramSize);
        std::fill(reinterpret_cast<uint16_t *>(ram+0x100000),
            reinterpret_cast<uint16_t *>(ram+0x100000)+320*240,background);
        p.end_write_rdram();std::memset(p.begin_read_hidden_rdram(),3,p.get_hidden_rdram_size());p.end_write_hidden_rdram();
        std::vector<uint32_t> batch={2,0xFF10013F,0x100000,2,0xED000000,(320*4<<12)|(240*4),
            2,0xEF000CF0,0x00504340,2,0xFCFFFFFF,0xFFFDF6FB};
        auto draw=[&](uint32_t rgba) {
            const uint32_t w[]={2,0xE7000000,0,2,0xFA000000,rgba,2,rectangle,0};
            batch.insert(batch.end(),std::begin(w),std::end(w));
        };
        if(hud)draw(0xC8602080);
        for(auto rgba:fades)draw(rgba);
        batch.insert(batch.end(),{2,0xE9000000,0});
        p.enqueue_command_batch(unsigned(batch.size()),batch.data());p.idle();
        return rgb(reinterpret_cast<const uint16_t *>(static_cast<const uint8_t *>(p.begin_read_rdram())+0x100000)[(43*320+43)^1]);
    };
    auto black=render(1,true,{}),white=render(0xFFFF,true,{});
    Pixel glyph;glyph.color=black;
    for(unsigned c=0;c<3;++c)glyph.transmit[c]=uint8_t(std::max(0,int(white[c])-int(black[c])));
    f->layers[0].pixels.assign(64,glyph);
    for(auto fades:std::vector<std::vector<uint32_t>>{{0},{0x00000080},{0x000000FF},
        {0xFFFFFF80},{0xFFFFFFff},{0x2060C0A0},{0x00000080,0xFFFFFF80}}) {
        auto scene=render(0x8421,false,fades),reference=render(0x8421,true,fades);
        f->fades.clear();uint64_t order=3;
        for(auto rgba:fades)f->fades.push_back({f->target,order++,{0,0,1280,960},rgba});
        auto plane=before_vi(v,1,16.0/9.0);auto i=(43*320+43)*2;
        for(unsigned c=0;c<3;++c) {
            unsigned shift=(2-c)*8;
            auto actual=std::min(255u,((plane.pixels[i]>>shift)&255)+
                (((plane.pixels[i+1]>>shift)&255)*scene[c]+127)/255);
            require(std::abs(int(actual)-int(reference[c]))<=9,"HUD/fade RDP order or double blend");
        }
    }
    std::cout<<"HUD fades: ordered capture, scissor, framebuffer isolation, black/white/color, RDP blend reference: OK\n";
}
