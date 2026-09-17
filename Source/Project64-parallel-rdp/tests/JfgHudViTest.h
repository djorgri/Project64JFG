#pragma once

static void test_hud_before_vi(Vulkan::Device &device)
{
    using namespace JfgHudLayer;
    // Geometry is derived from the display mapping, rather than a hard-coded
    // 3/4 compression. It must work with both VI resolutions and output ratios.
    for (unsigned nativeWidth : {320u, 448u}) for (double aspect : {4.0/3.0, 16.0/9.0})
    {
        auto f=std::make_shared<Frame>();f->target={0x100000,nativeWidth,nativeWidth*3/4};
        Layer l;l.target=f->target;l.x=80;l.y=40;l.width=32;l.height=16;
        l.pixels.resize(32*16,Pixel{{100,0,0},{0,0,0}});f->layers.push_back(l);
        View v;v.frame=f;v.vi.xscale=nativeWidth*512/320;v.vi.yscale=nativeWidth==320?1024:1433;
        auto plane=before_vi(v,8,aspect);
        int lo=int(plane.width*8),hi=-1;
        for(unsigned x=0;x<plane.width*8;++x)
            if(plane.pixels[(40*8*plane.width*8+x)*2]){lo=std::min(lo,int(x));hi=int(x);}
        require(lo==60*8,"pre-VI HUD anchor moved");
        const double expected=32*(640.0/240.0)/aspect*(v.vi.xscale&4095)/(v.vi.yscale&4095)*8;
        require(std::abs(hi-lo+1-expected)<=1,"pre-VI aspect compensation");
        f->layers[0].text=true;f->layers[0].clip={81*4,41*4,90*4,45*4};
        plane=before_vi(v,2,aspect);
        for(unsigned y=0;y<plane.height*2;++y)for(unsigned x=0;x<plane.width*2;++x)
            if(x<162 || x>=180 || y<82 || y>=90)
                require(plane.pixels[(y*plane.width*2+x)*2]==0,"pre-VI text clip escaped");
        f->layers.clear();require(before_vi(v,2,aspect).pixels.empty(),"empty frame retained HUD");
    }

    // Compare injection with an independently precomposed framebuffer, using
    // the actual VI shaders, including gamma and resampling. This detects a
    // misplaced post-filter overlay, wrong origin, double blending and writes
    // to guest RAM. Exercise native and upscaled scene domains.
    for(unsigned scale:{1u,2u})for(bool rgba16:{false,true})
    {
        const unsigned ramSize=4*1024*1024,origin=0x100000,width=320,height=240;
        const unsigned flags=RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT |
            RDP::COMMAND_PROCESSOR_FLAG_SINGLE_THREADED_COMMAND_BIT |
            (scale==2?RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT:0);
        RDP::CommandProcessor p(device,nullptr,0,ramSize,ramSize/2,flags);
        require(p.device_is_supported(),"VI test device support");
        auto setup=[&](bool gamma,unsigned offset) {
            p.set_vi_register(RDP::VIRegister::Control,
                (rgba16?RDP::VI_CONTROL_TYPE_RGBA5551_BIT:RDP::VI_CONTROL_TYPE_RGBA8888_BIT) |
                RDP::VI_CONTROL_DIVOT_ENABLE_BIT | RDP::VI_CONTROL_DITHER_FILTER_ENABLE_BIT |
                (gamma?RDP::VI_CONTROL_GAMMA_ENABLE_BIT:0));
            p.set_vi_register(RDP::VIRegister::Origin,origin+offset*(rgba16?2:4));
            p.set_vi_register(RDP::VIRegister::Width,width);
            p.set_vi_register(RDP::VIRegister::VSync,RDP::VI_V_SYNC_NTSC);
            p.set_vi_register(RDP::VIRegister::XScale,RDP::make_vi_scale_register(512,0));
            p.set_vi_register(RDP::VIRegister::YScale,RDP::make_vi_scale_register(1024,0));
            p.set_vi_register(RDP::VIRegister::HStart,RDP::make_vi_start_register(108,748));
            p.set_vi_register(RDP::VIRegister::VStart,RDP::make_vi_start_register(34,514));
        };
        RDP::VIOverlay overlay;overlay.origin=origin;overlay.width=width;overlay.height=height;overlay.scale=scale;
        overlay.pixels.resize(width*height*scale*scale*2);
        for(size_t i=1;i<overlay.pixels.size();i+=2)overlay.pixels[i]=0xFFFFFF;
        for(unsigned y=40*scale;y<70*scale;++y)for(unsigned x=60*scale;x<96*scale;++x) {
            auto i=(y*width*scale+x)*2;
            overlay.pixels[i]=0x600020;overlay.pixels[i+1]=0x808080;
        }
        auto fill=[&](bool hud) {
            p.idle();auto ram=static_cast<uint8_t *>(p.begin_read_rdram());std::memset(ram,0,ramSize);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                unsigned r=64,g=96,b=128;
                if(hud && x>=60 && x<96 && y>=40 && y<70){r=96+(128*r+127)/255;g=(128*g+127)/255;b=32+(128*b+127)/255;}
                if(rgba16) reinterpret_cast<uint16_t *>(ram+origin)[(y*width+x)^1]=uint16_t((r>>3)<<11|(g>>3)<<6|(b>>3)<<1|1);
                else reinterpret_cast<uint32_t *>(ram+origin)[y*width+x]=r<<24|g<<16|b<<8|0xE0;
            }
            p.end_write_rdram();std::memset(p.begin_read_hidden_rdram(),3,p.get_hidden_rdram_size());p.end_write_hidden_rdram();
        };
        auto scan=[&](const RDP::VIOverlay *o) {
            RDP::ScanoutOptions opts;opts.overlay=o;opts.vi.gamma_dither=false;
            std::vector<RDP::RGBA> result;unsigned w=0,h=0;
            p.begin_frame_context();p.scanout_sync(result,w,h,opts);
            require(w && h && !result.empty(),"VI scanout empty");return result;
        };
        auto same=[](const std::vector<RDP::RGBA>&a,const std::vector<RDP::RGBA>&b) {
            return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0;
        };
        for(bool gamma:{false,true})for(unsigned offset:{0u,324u}) {
            setup(gamma,offset);fill(false);auto base=scan(nullptr);auto injected=scan(&overlay);
            require(!same(base,injected),"VI overlay was not applied");
            require(same(injected,scan(&overlay)),"repeated VI scanout double blended");
            require(same(base,scan(nullptr)),"VI overlay changed scene memory");
            fill(true);auto reference=scan(nullptr);
            if(!same(injected,reference)) {
                unsigned count=0;
                for(size_t i=0;i<injected.size();++i)if(std::memcmp(&injected[i],&reference[i],4)) {
                    if(count++<5)std::cerr<<"VI mismatch scale="<<scale<<" rgba16="<<rgba16<<" gamma="<<gamma<<" offset="<<offset<<" i="<<i
                        <<" actual="<<unsigned(injected[i].r)<<","<<unsigned(injected[i].g)<<","<<unsigned(injected[i].b)
                        <<" ref="<<unsigned(reference[i].r)<<","<<unsigned(reference[i].g)<<","<<unsigned(reference[i].b)<<"\n";
                }
                std::cerr<<"Different pixels: "<<count<<"\n";
            }
            require(same(injected,reference),"pre-VI overlay differs from filtered framebuffer reference");
            fill(false);overlay.origin=0x200000;
            require(same(base,scan(&overlay)),"foreign framebuffer received overlay");overlay.origin=origin;
        }
        // Reticle lines now traverse exactly the same VI filters as a CPU
        // framebuffer reference, in both resolutions and with/without gamma.
        JfgReticleOverlay::View reticle;
        reticle.frame={origin,width,height,{{76,60,84,60,80,60,0},{120,60,120,64,120,60,0x10}}};
        reticle.origin=origin;reticle.xscale=512;reticle.yscale=1024;
        auto reticlePlane=overlay;
        JfgReticleOverlay::before_vi(reticle,scale,4.0/3.0,reticlePlane);
        for(bool gamma:{false,true}) {
            setup(gamma,0);fill(false);auto actual=scan(&reticlePlane);
            fill(true);p.idle();auto memory=static_cast<uint8_t *>(p.begin_read_rdram());
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const unsigned red=x==120 && y>=60 && y<=64?192:0;
                const unsigned green=y==60 && x>=76 && x<=84?64:0;
                if(!red && !green)continue;
                if(rgba16) {
                    auto &value=reinterpret_cast<uint16_t *>(memory+origin)[(y*width+x)^1];
                    const unsigned r=std::min(31u,(value>>11)+red/8),g=std::min(31u,((value>>6)&31)+green/8);
                    value=uint16_t((value&63)|(r<<11)|(g<<6));
                } else {
                    auto &value=reinterpret_cast<uint32_t *>(memory+origin)[y*width+x];
                    const unsigned r=std::min(255u,(value>>24)+red),g=std::min(255u,((value>>16)&255)+green);
                    value=(value&65535)|(r<<24)|(g<<16);
                }
            }
            p.end_write_rdram();
            require(same(actual,scan(nullptr)),"reticle bypassed VI or additive HUD composition changed");
        }
        // GPU submissions retain their own uploaded plane across CPU changes
        // and frame-context reuse; a later empty frame must not inherit it.
        setup(false,0);fill(false);auto expected=scan(&overlay),empty=scan(nullptr);
        for(unsigned repeat=0;repeat<8;++repeat) {
            RDP::VIScanoutBuffer pending[2];
            {
                auto temporary=overlay;RDP::ScanoutOptions opts;opts.overlay=&temporary;opts.vi.gamma_dither=false;
                p.begin_frame_context();p.scanout_async_buffer(pending[0],opts);
                std::fill(temporary.pixels.begin(),temporary.pixels.end(),0);
            }
            RDP::ScanoutOptions opts;opts.vi.gamma_dither=false;
            p.begin_frame_context();p.scanout_async_buffer(pending[1],opts);
            for(unsigned i=0;i<2;++i) {
                require(bool(pending[i].fence),"async VI scanout missing fence");pending[i].fence->wait();
                std::vector<RDP::RGBA> pixels(pending[i].width*pending[i].height);
                std::memcpy(pixels.data(),device.map_host_buffer(*pending[i].buffer,Vulkan::MEMORY_ACCESS_READ_BIT),pixels.size()*4);
                device.unmap_host_buffer(*pending[i].buffer,Vulkan::MEMORY_ACCESS_READ_BIT);
                require(same(pixels,i?empty:expected),"in-flight VI overlay ownership or stale HUD");
            }
        }
    }
    std::cout<<"HUD before VI: aspect/clipping, real VI reference, gamma, origins, 16/32-bit, 1x/2x, no guest writes: OK\n";
}
