#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace modern_playlist {
struct cover_pixels { unsigned width=0, height=0; std::vector<unsigned char> bgra; };
// Keep visited thumbnails across scrolling and playlist switches. Identical
// decoded album images share storage, while lookup keys still identify tracks.
class artwork_cache {
    struct entry {
        std::shared_ptr<cover_pixels> pixels;
        uint64_t hash;
        std::list<std::string>::iterator recent;
    };
    std::map<std::string,entry> entries_;
    std::list<std::string> recent_;
    std::map<uint64_t,std::vector<std::weak_ptr<cover_pixels>>> images_;
    std::map<const cover_pixels*,size_t> references_;
    std::set<std::string> stale_;
    size_t bytes_=0, byte_limit_, entry_limit_;
public:
    explicit artwork_cache(size_t byte_limit=256U*1024*1024,size_t entry_limit=16384)
        : byte_limit_(byte_limit),entry_limit_(entry_limit) {}
    size_t size() const { return entries_.size(); }
    size_t bytes() const { return bytes_; }
    bool lookup(const std::string& key,std::shared_ptr<cover_pixels>& pixels) {
        const auto found=entries_.find(key);
        if(found==entries_.end()) return false;
        recent_.splice(recent_.begin(),recent_,found->second.recent);
        pixels=found->second.pixels; return true; // Null is a cached failed lookup.
    }
    void erase(const std::string& key) {
        const auto found=entries_.find(key); if(found==entries_.end()) return;
        const auto& e=found->second;
        if(e.pixels && --references_.at(e.pixels.get())==0) {
            bytes_-=e.pixels->bgra.size(); references_.erase(e.pixels.get());
            auto pool=images_.find(e.hash);
            auto& candidates=pool->second;
            candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& weak) {
                auto p=weak.lock(); return !p || p==e.pixels;
            }),candidates.end());
            if(candidates.empty()) images_.erase(pool);
        }
        stale_.erase(key); // Before recent_.erase: key may refer to recent_.back().
        recent_.erase(e.recent); entries_.erase(found);
    }
    // Tag edits may replace embedded art, but most (ratings, play counts) do
    // not. Keep drawing the cached image until its reload completes.
    void mark_stale(const std::string& key) { if(entries_.count(key)) stale_.insert(key); }
    bool stale(const std::string& key) const { return stale_.count(key)!=0; }
    void store(const std::string& key,std::shared_ptr<cover_pixels> pixels) {
        erase(key);
        uint64_t hash=14695981039346656037ULL;
        if(pixels) {
            for(auto byte:pixels->bgra) { hash^=byte; hash*=1099511628211ULL; }
            auto& candidates=images_[hash];
            for(const auto& weak:candidates) if(auto other=weak.lock()) {
                if(other->width==pixels->width && other->height==pixels->height && other->bgra==pixels->bgra) {
                    pixels=std::move(other); break;
                }
            }
            if(++references_[pixels.get()]==1) { bytes_+=pixels->bgra.size(); candidates.push_back(pixels); }
        }
        recent_.push_front(key); entries_.emplace(key,entry{std::move(pixels),hash,recent_.begin()});
        while(!recent_.empty() && (bytes_>byte_limit_ || entries_.size()>entry_limit_)) erase(recent_.back());
    }
    void clear() { entries_.clear(); recent_.clear(); images_.clear(); references_.clear(); stale_.clear(); bytes_=0; }
};
struct artwork_settings {
    bool aspect=true, artist=false, enabled=false;
    unsigned margin=4, source=2, opacity=255, blur=0, mode=1, region=0, dimming=192;
    // Stable source IDs: custom (1), front cover (2), pseudo transparency (3), artist (4).
    // Enabled is independent of source; legacy Off (0) migrates to disabled front cover.
    // Background modes retain their saved IDs: center crop (1), top crop (4).
    // region: whole panel / playlist (including status); artist is a legacy flag.
    std::string path;
};
inline bool valid_artwork(const artwork_settings& s) {
    return s.margin<=24 && s.source>=1 && s.source<=4 && s.opacity<=255 && s.blur<=32 &&
        (s.mode==1 || s.mode==4) && s.region<=1 && s.dimming<=255 && s.path.size()<=16384;
}
struct image_rect { double x=0,y=0,w=0,h=0; };
inline image_rect image_placement(unsigned width,unsigned height,double w,double h,unsigned mode) {
    if(!width || !height || w<=0 || h<=0) return {};
    double scale=1;
    // Modes 0/2/3 remain available to thumbnail and surface composition callers.
    // Both crop modes preserve proportions, including square covers. Top Crop
    // centers horizontal overflow and removes 1/4 of vertical overflow at the top.
    if(mode==0) return {0,0,w,h};
    if(mode==1 || mode==4) scale=std::max(w/width,h/height);
    if(mode==2) scale=std::min(w/width,h/height);
    return {(w-width*scale)/2,(h-height*scale)/(mode==4?4:2),width*scale,height*scale};
}
// Separable, clamped-edge box blur: O(pixels), independent of radius.
inline void box_blur(cover_pixels& image,unsigned radius) {
    if(!radius || !image.width || !image.height) return;
    radius=std::min(32U,radius);
    auto pass=[&](bool vertical) {
        auto input=image.bgra;
        const unsigned length=vertical?image.height:image.width, lines=vertical?image.width:image.height;
        auto at=[&](unsigned line,int pos,unsigned c) {
            const unsigned p=unsigned(std::clamp(pos,0,int(length)-1));
            return (vertical?size_t(p)*image.width+line:size_t(line)*image.width+p)*4+c;
        };
        for(unsigned line=0;line<lines;++line) for(unsigned c=0;c<4;++c) {
            unsigned sum=0;
            for(int k=-int(radius);k<=int(radius);++k) sum+=input[at(line,k,c)];
            for(unsigned p=0;p<length;++p) {
                image.bgra[at(line,int(p),c)]=static_cast<unsigned char>(sum/(2*radius+1));
                sum-=input[at(line,int(p)-int(radius),c)];
                sum+=input[at(line,int(p)+int(radius)+1,c)];
            }
        }
    };
    pass(false); pass(true);
}
// Composite premultiplied artwork over an opaque BGRA surface.
inline void composite_image(cover_pixels& dest,const cover_pixels& source,unsigned opacity,unsigned mode) {
    const auto r=image_placement(source.width,source.height,dest.width,dest.height,mode);
    if(r.w<=0 || r.h<=0) return;
    opacity=std::min(255U,opacity);
    for(unsigned y=0;y<dest.height;++y) for(unsigned x=0;x<dest.width;++x) {
        if(x<r.x || y<r.y || x>=r.x+r.w || y>=r.y+r.h) continue;
        const unsigned sx=std::min(source.width-1,unsigned((x-r.x)*source.width/r.w));
        const unsigned sy=std::min(source.height-1,unsigned((y-r.y)*source.height/r.h));
        const size_t s=(size_t(sy)*source.width+sx)*4,d=(size_t(y)*dest.width+x)*4;
        const unsigned a=source.bgra[s+3]*opacity/255;
        for(unsigned c=0;c<3;++c) dest.bgra[d+c]=static_cast<unsigned char>(std::min(255U,source.bgra[s+c]*opacity/255+dest.bgra[d+c]*(255-a)/255));
        dest.bgra[d+3]=255;
    }
}
struct default_cover_style {
    unsigned size=0, outer_diameter=0, inner_diameter=0;
    uint32_t base=0, ring=0, figure=0; // Opaque RGB channels in COLORREF order (0x00BBGGRR).
};
inline default_cover_style make_default_cover_style(unsigned size,uint32_t background,uint32_t text) {
    const auto blend=[&](unsigned amount) {
        uint32_t result=0;
        for(unsigned shift:{0U,8U,16U}) {
            const auto bg=(background>>shift)&255, fg=(text>>shift)&255;
            result|=((bg*(255-amount)+fg*amount+127)/255)<<shift;
        }
        return result;
    };
    // Alternating rows already blend toward text by 4% (about 10/255).
    // Keep the square another step above that fill, with a distinct ring. The
    // Artist Art figure is smaller and more detailed, so it takes a stronger step.
    return {size,unsigned(std::round(size*.6)),unsigned(std::round(size*.2)),blend(20),blend(30),blend(60)};
}
// GDI has no antialiased ellipse fill. Sample the same three opaque layers at
// the final pixel size; only circle edges receive coverage, never the square.
inline std::shared_ptr<cover_pixels> raster_default_cover(const default_cover_style& style) {
    auto p=std::make_shared<cover_pixels>(); p->width=p->height=style.size;
    p->bgra.resize(size_t(style.size)*style.size*4);
    const double center=style.size/2.0, outer=style.outer_diameter/2.0, inner=style.inner_diameter/2.0;
    for(unsigned y=0;y<style.size;++y) for(unsigned x=0;x<style.size;++x) {
        unsigned ring_samples=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double dx=x+(sx+.5)/4-center, dy=y+(sy+.5)/4-center;
            const double distance=dx*dx+dy*dy;
            if(distance<outer*outer && distance>=inner*inner) ++ring_samples;
        }
        const size_t i=(size_t(y)*style.size+x)*4;
        for(unsigned c=0;c<3;++c) {
            const unsigned shift=(2-c)*8, base=(style.base>>shift)&255, ring=(style.ring>>shift)&255;
            p->bgra[i+c]=static_cast<unsigned char>((base*(16-ring_samples)+ring*ring_samples+8)/16);
        }
        p->bgra[i+3]=255;
    }
    return p;
}
// The Artist Art silhouette on the missing-cover square. Both colors blend the
// row background toward the text color, so it follows the theme like the disc
// placeholder. 4x coverage at the final thumbnail size keeps small row icons'
// head/shoulder contours antialiased.
inline std::shared_ptr<cover_pixels> artist_art_placeholder(const default_cover_style& style) {
    const unsigned size=std::clamp(style.size,1U,4096U);
    auto p=std::make_shared<cover_pixels>(); p->width=p->height=size; p->bgra.resize(size_t(size)*size*4);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double dx=(x+(sx+.5)/4)*64/size-32,dy=(y+(sy+.5)/4)*64/size-32;
            const bool head=dx*dx+(dy+9)*(dy+9)<=64;
            const bool shoulders=dx*dx+(dy-17)*(dy-17)<=225 && std::abs(dy-10)<=7;
            if(head || shoulders) ++coverage;
        }
        const size_t i=(size_t(y)*size+x)*4;
        for(unsigned c=0;c<3;++c) {
            const unsigned shift=(2-c)*8, base=(style.base>>shift)&255, figure=(style.figure>>shift)&255;
            p->bgra[i+c]=static_cast<unsigned char>((base*(16-coverage)+figure*coverage+8)/16);
        }
        p->bgra[i+3]=255;
    }
    return p;
}
}
