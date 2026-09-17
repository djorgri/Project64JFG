#pragma once

static void test_map_text_order(Vulkan::Device &device)
{
    using namespace JfgHudLayer;
    Capture cap;
    auto capture=[&](std::initializer_list<uint32_t> w,bool marker=false) {
        return cap.command(w.begin(),unsigned(w.size()),marker);
    };
    const std::array<uint32_t,4> glyph={0xE4000000|(48*4<<12)|(48*4),(40*4<<12)|(40*4),0,0x04000400};
    std::array<uint32_t,4> draw;
    capture({0xFF10013F,0x100000});
    require(!cap.ordered_font_rectangle(glyph.data(),4,512,1024,0,16.0/9.0,draw),"unscoped map rewrite");
    capture({0xE7000000,JfgHudRaster::Marker|7},true);
    require(!cap.command(glyph.data(),4,false) && cap.glyphs.empty(),"map glyph entered final overlay");
    for(unsigned width:{320u,448u})for(double aspect:{4.0/3.0,16.0/9.0}) {
        capture({0xFF100000|(width-1),0x100000});
        const unsigned xa=width*512/320,ya=width==320?1365:1911;
        require(cap.ordered_font_rectangle(glyph.data(),4,xa,ya,0,aspect,draw),"map rewrite rejected");
        const double dx=(640.0/240.0)/aspect*xa/ya*(4.0/3.0);
        require(((draw[1]>>12)&4095)==160,"map anchor moved");
        require(std::abs(int((draw[0]>>12)&4095)-(40+8*dx)*4)<=2,"map aspect mismatch");
        const int destWidth=int((draw[0]>>12)&4095)-160;
        require((draw[3]&65535)==819 && std::abs(int(draw[3]>>16)-32768.0/destWidth)<=.5,"map texture step mismatch");
        require((draw[1]&4095)==156 && (draw[0]&4095)==196,"map vertical centre changed");
    }
    capture({0xEF002CF0,0x00404240});
    std::vector<uint32_t> fontBatch;
    require(cap.ordered_font_commands(glyph.data(),4,512,1024,0,16.0/9.0,fontBatch), "pixel-aligned font batch");
    require(fontBatch.size()==17 && fontBatch[3]==2 && fontBatch[4]==0xEF000CF0 &&
        fontBatch[11]==2 && fontBatch[12]==0xEF002CF0 && fontBatch[13]==0x00404240,
        "font sampler override leaked or changed the blender");
    RDP::Quirks defaultQuirks, preciseQuirks;preciseQuirks.set_native_resolution_tex_rect(false);
    require(fontBatch[1]==uint32_t(RDP::Op::MetaSetQuirks)<<24 && fontBatch[2]==preciseQuirks.u.words[0] &&
        fontBatch[15]==fontBatch[1] && fontBatch[16]==defaultQuirks.u.words[0], "font resolution override leaked");
    require(cap.otherModes[0]==0xEF002CF0, "font rewrite changed tracked guest sampler");
    // Check every vertical phase, small glyph height and supported raster
    // scale, including a glyph clipped at the top of the framebuffer. Neither
    // edge may sample outside its atlas subrectangle.
    for(unsigned scale:{1u,2u,4u,8u})for(int height=1;height<=24;++height)for(int y=0;y<=4;++y) {
        auto sample=glyph;sample[0]=(sample[0]&~4095u)|unsigned((y+height)*4);
        sample[1]=(sample[1]&~4095u)|unsigned(y*4);sample[2]=(4*32<<16)|(4*32);
        require(cap.ordered_font_rectangle(sample.data(),4,512,1365,0,16.0/9.0,draw,scale), "font phase rectangle rejected");
        const int first=(draw[1]&4095)*scale/4,last=(draw[0]&4095)*scale/4-1;
        for(int row:{first,last}) {
            const double texel=(int16_t(draw[2]&65535)+(double(row)/scale-int((draw[1]&4095)/4))*(draw[3]&65535)/32.0)/32;
            require(texel>=4 && texel<4+height, "font phase escaped atlas rows");
        }
    }
    auto topGlyph=glyph;topGlyph[1]&=~4095u;topGlyph[0]=(topGlyph[0]&~4095u)|32;
    require(cap.ordered_font_rectangle(topGlyph.data(),4,512,1024,0,16.0/9.0,draw),"top-edge glyph rejected");
    require((draw[1]&4095)==0 && (draw[2]&65535)==38,"top-edge T offset or coordinate wrap");
    auto unknown=glyph;unknown[3]=0x08000400;
    require(!cap.ordered_font_rectangle(unknown.data(),4,512,1024,0,16.0/9.0,draw),"unknown font rewritten");
    require(!cap.ordered_font_rectangle(glyph.data(),4,0,1024,0,16.0/9.0,draw),"invalid VI rewritten");
    capture({0xE7000000,JfgHudRaster::Marker|8},true);
    require(!cap.ordered_font_rectangle(glyph.data(),4,512,1024,0,16.0/9.0,draw),"map scope leaked");

    // Multiplayer counters retain each player's right-hand anchor and all
    // source digits, including vertically flipped atlas sampling.
    for (unsigned anchor : {80u, 160u, 280u}) {
        capture({0xE7000000 | (anchor * 4), JfgHudRaster::Marker | 9}, true);
        std::array<uint32_t,4> number = {0xE4000000 | (anchor*4<<12) | 840,
            ((anchor-8)*4<<12) | 808, 0, 0x0400FC00};
        require(!cap.command(number.data(),4,false), "multiplayer digits entered overlay");
        require(cap.ordered_number_rectangle(number.data(),4,draw), "multiplayer counter rewrite");
        require(((draw[0]>>12)&4095)==anchor*4 && ((draw[1]>>12)&4095)==(anchor-6)*4,
            "multiplayer counter anchor/width");
        require(draw[3]==0x0555FC00, "multiplayer counter lost source columns or vertical flip");
        capture({0xE7000000,JfgHudRaster::Marker|10},true);
        require(!cap.ordered_number_rectangle(number.data(),4,draw), "multiplayer counter scope leaked");
    }

    // Submit corrected labels around a later textured frame. The frame must
    // cover the first label while a label submitted after it stays visible.
    // No color-key or rectangular UI mask is used in production.
    const unsigned ramSize=4*1024*1024;
    RDP::CommandProcessor p(device,nullptr,0,ramSize,ramSize/2,
        RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
        RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT);
    require(p.device_is_supported(),"map test RDP device");
    auto ram=static_cast<uint8_t *>(p.begin_read_rdram());std::memset(ram,0,ramSize);
    std::fill(reinterpret_cast<uint16_t *>(ram+0x1000),reinterpret_cast<uint16_t *>(ram+0x1000)+64,0xF801);
    std::fill(reinterpret_cast<uint16_t *>(ram+0x2000),reinterpret_cast<uint16_t *>(ram+0x2000)+64,0x07C1);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        reinterpret_cast<uint16_t *>(ram+0x3000)[(y*8+x)^1]=y&1?0xF801:0x07C1;
    p.end_write_rdram();std::memset(p.begin_read_hidden_rdram(),3,p.get_hidden_rdram_size());p.end_write_hidden_rdram();
    std::vector<uint32_t> batch;
    auto emit=[&](std::initializer_list<uint32_t> w) {batch.push_back(unsigned(w.size()));batch.insert(batch.end(),w.begin(),w.end());};
    emit({0xFF10013F,0x100000});emit({0xED000000,(320*4<<12)|(240*4)});
    emit({0xEF000CF0,0x00404240});emit({0xFCFFFFFF,0xFFFCF279});
    auto texture=[&](uint32_t address) {
        emit({0xE7000000,0});emit({0xFD100007,address});emit({0xF5100400,0x00080200});
        emit({0xF4000000,0x0001C01C});emit({0xF2000000,0x0001C01C});
    };
    capture({0xFF10013F,0x100000});capture({0xE7000000,JfgHudRaster::Marker|7},true);
    require(cap.ordered_font_rectangle(glyph.data(),4,512,1024,0,16.0/9.0,draw),"map GPU glyph rewrite");
    texture(0x1000);emit({draw[0],draw[1],draw[2],draw[3]});
    texture(0x2000);emit({0xE4000000|(64*4<<12)|(44*4),(32*4<<12)|(40*4),0,0x01000400});
    texture(0x1000);emit({draw[0]+(16*4<<12),draw[1]+(16*4<<12),draw[2],draw[3]});
    // Alternating native rows expose unintended interpolation before VI.
    texture(0x3000);emit({0xEF002CF0,0x00404240});
    const std::array<uint32_t,4> stripes={0xE4000000|(108*4<<12)|(88*4),(100*4<<12)|(80*4),0,0x04000400};
    require(cap.ordered_font_commands(stripes.data(),4,512,1024,0,16.0/9.0,batch), "font sampler GPU commands");
    // A menu fade drawn after a corrected label must cover it in the scene.
    emit({0xE7000000,0});emit({0xEF000000,0x00504340});
    emit({0xFCFFFFFF,0xFFFDF6FB});emit({0xFA000000,0x000000FF});
    emit({0xF6000000|(64*4<<12)|(50*4),(56*4<<12)|(44*4)});
    emit({0xE9000000,0});p.enqueue_command_batch(unsigned(batch.size()),batch.data());p.idle();
    auto pixels=reinterpret_cast<const uint16_t *>(static_cast<const uint8_t *>(p.begin_read_rdram())+0x100000);
    require(rgb(pixels[(42*320+43)^1])==rgb(0x07C1),"text still covers later textured frame");
    require(rgb(pixels[(46*320+43)^1])==rgb(0xF801),"visible part of map text lost");
    require(rgb(pixels[(42*320+59)^1])==rgb(0xF801),"later label incorrectly hidden by frame");
    require(rgb(pixels[(46*320+59)^1])==rgb(0x0001), "corrected menu label escaped later black fade");
    for(unsigned y=80;y<88;++y)for(unsigned x=102;x<106;++x) {
        const auto c=rgb(pixels[(y*320+x)^1]);
        require(c==rgb(0xF801) || c==rgb(0x07C1), "native font rows blended before VI");
    }
    cap.next();require(cap.scope==0 && cap.glyphs.empty(),"map scope survived frame reset");

    // At 2x, a 4/3-height font must use half-pixel rows, rather than first
    // snapping to native rows and duplicating them. The neighbouring unmarked
    // rectangle must still obey NativeTexRects. Inspect scanout with VI filters
    // disabled here; the separate VI tests exercise the real filtered path.
    auto highOwner=std::make_unique<RDP::CommandProcessor>(device,nullptr,0,ramSize,ramSize/2,
        RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
        RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT |
        RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT);
    auto &high=*highOwner;
    require(high.device_is_supported(), "font precision GPU support");
    ram=static_cast<uint8_t *>(high.begin_read_rdram());std::memset(ram,0,ramSize);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        reinterpret_cast<uint16_t *>(ram+0x3000)[(y*8+x)^1]=(x^y)&1?0xF801:0x07C1;
    // A glyph is a subrectangle of an atlas, not necessarily the complete
    // tile. Green guard rows expose sampling beyond its lower boundary.
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x)
        reinterpret_cast<uint16_t *>(ram+0x4000)[(y*8+x)^1]=y>=4 && y<12?0xF801:0x07C1;
    high.end_write_rdram();std::memset(high.begin_read_hidden_rdram(),3,high.get_hidden_rdram_size());high.end_write_hidden_rdram();
    batch.clear();emit({0xFF10013F,0x100000});emit({0xED000000,(320*4<<12)|(240*4)});
    emit({0xEF000CF0,0x00404240});emit({0xFCFFFFFF,0xFFFCF279});texture(0x3000);
    capture({0xFF10013F,0x100000});capture({0xE7000000,JfgHudRaster::Marker|7},true);
    cap.otherModes={0xEF000CF0,0x00404240};
    require(cap.ordered_font_commands(stripes.data(),4,512,1365,0,16.0/9.0,batch,{},2), "2x font commands");
    require(cap.ordered_font_rectangle(stripes.data(),4,512,1365,0,16.0/9.0,draw,2), "2x reference rectangle");
    emit({draw[0]+(16*4<<12),draw[1]+(16*4<<12),draw[2],draw[3]});
    emit({0xE7000000,0});emit({0xFD100007,0x4000});emit({0xF5100400,0x00080200});
    emit({0xF4000000,0x0001C03C});emit({0xF2000000,0x0001C03C});
    auto atlasGlyph=stripes;atlasGlyph[0]+=40*4<<12;atlasGlyph[1]+=40*4<<12;atlasGlyph[2]=4*32;
    require(cap.ordered_font_commands(atlasGlyph.data(),4,512,1365,0,16.0/9.0,batch,{},2), "atlas glyph commands");
    emit({0xE9000000,0});high.enqueue_command_batch(unsigned(batch.size()),batch.data());high.idle();
    high.set_vi_register(RDP::VIRegister::Control,RDP::VI_CONTROL_TYPE_RGBA5551_BIT | RDP::VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT);
    high.set_vi_register(RDP::VIRegister::Origin,0x100000);high.set_vi_register(RDP::VIRegister::Width,320);
    high.set_vi_register(RDP::VIRegister::VSync,RDP::VI_V_SYNC_NTSC);
    high.set_vi_register(RDP::VIRegister::XScale,RDP::make_vi_scale_register(512,0));
    high.set_vi_register(RDP::VIRegister::YScale,RDP::make_vi_scale_register(1024,0));
    high.set_vi_register(RDP::VIRegister::HStart,RDP::make_vi_start_register(108,748));
    high.set_vi_register(RDP::VIRegister::VStart,RDP::make_vi_start_register(34,514));
    RDP::ScanoutOptions options;options.vi={false,false,false,false,false,false};
    std::vector<RDP::RGBA> scanout;unsigned w=0,h=0;
    high.begin_frame_context();high.scanout_sync(scanout,w,h,options);
    require(w==1280 && h==480, "2x precision scanout dimensions");
    unsigned preciseChanges=0,stockChanges=0;
    for(unsigned y=160;y<176;y+=2) {
        auto differs=[&](unsigned x) {return std::memcmp(&scanout[y*w+x],&scanout[(y+1)*w+x],4)!=0;};
        preciseChanges+=differs(104*4);stockChanges+=differs(120*4);
    }
    require(preciseChanges>0, "corrected font still rasterized at native resolution");
    require(stockChanges==0, "font override changed following stock rectangle resolution");
    auto transitions=[&](unsigned start) {
        unsigned changes=0;
        for(unsigned x=start+1;x<start+24;++x)
            changes+=std::memcmp(&scanout[164*w+x-1],&scanout[164*w+x],4)!=0;
        return changes;
    };
    require(transitions(100*4)==7, "compressed 2x font lost a source column");
    require(transitions(116*4)<7, "stock compressed rectangle unexpectedly uses subpixels");
    const auto bottom=scanout[178*w+142*4];
    require(bottom.r>200 && bottom.g==0, "font bottom sampled the following atlas row");
    const auto top=scanout[157*w+142*4];
    require(top.r>200 && top.g==0, "font top sampled the preceding atlas row");
    std::cout<<"Map text: square proportions, native scene order, textured occlusion, top edge and scope isolation: OK\n";
}
