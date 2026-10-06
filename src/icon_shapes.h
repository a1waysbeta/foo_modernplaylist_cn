#pragma once
#include "artwork.h"
#include <cstdint>

namespace modern_playlist {
struct icon_point { double x,y; };
// The Material heart and star paths used by foo_nowbar, in a 960-unit square.
// Rasterize at the requested physical size with 4x coverage so both renderers
// use the same smooth contours without depending on a symbol font.
inline std::shared_ptr<cover_pixels> special_icon(bool heart,unsigned size,uint32_t color,unsigned opacity=255) {
    size=std::clamp(size,1U,256U);
    opacity=std::min(opacity,255U);
    std::vector<icon_point> points;
    if(heart) {
        icon_point start{480,840}; points.push_back(start);
        const icon_point curves[][3]={
            {{200,660},{80,460},{80,326}}, {{80,180},{200,106},{300,106}},
            {{400,106},{480,160},{480,240}}, {{480,160},{560,106},{660,106}},
            {{760,106},{880,180},{880,326}}, {{880,460},{760,660},{480,840}}};
        for(const auto& curve:curves) {
            for(int i=1;i<=24;++i) {
                const double t=i/24.0,u=1-t;
                points.push_back({u*u*u*start.x+3*u*u*t*curve[0].x+3*u*t*t*curve[1].x+t*t*t*curve[2].x,
                    u*u*u*start.y+3*u*u*t*curve[0].y+3*u*t*t*curve[1].y+t*t*t*curve[2].y});
            }
            start=curve[2];
        }
    } else points={{233,840},{298,559},{80,370},{368,345},{480,80},{592,345},{880,370},{662,559},{727,840},{480,691}};
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    std::vector<unsigned char> coverage(size_t(size)*size);
    const unsigned extent=size*4;
    std::vector<double> crossings; crossings.reserve(points.size());
    for(unsigned sy=0;sy<extent;++sy) {
        const double y=(sy+.5)*960/extent; crossings.clear();
        for(size_t i=0,j=points.size()-1;i<points.size();j=i++) {
            const auto a=points[i],b=points[j];
            if((a.y>y)!=(b.y>y)) crossings.push_back((a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y))*extent/960);
        }
        std::sort(crossings.begin(),crossings.end());
        for(size_t i=0;i+1<crossings.size();i+=2) {
            const int left=std::clamp(int(std::ceil(crossings[i]-.5)),0,int(extent));
            const int right=std::clamp(int(std::ceil(crossings[i+1]-.5)),0,int(extent));
            for(int sx=left;sx<right;++sx) ++coverage[size_t(sy/4)*size+unsigned(sx)/4];
        }
    }
    for(size_t i=0;i<coverage.size();++i) {
        const unsigned alpha=coverage[i]*opacity/16;
        pixels->bgra[i*4]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i*4+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i*4+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i*4+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// A round dot replaces an empty star in Style 2. Keep its alpha premultiplied
// so the real row background (including selection/artwork) shows through.
inline std::shared_ptr<cover_pixels> rating_dot_icon(unsigned size,uint32_t color,unsigned opacity) {
    size=std::clamp(size,1U,256U); opacity=std::min(opacity,255U);
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    const double radius=size/2.0;
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double dx=x+(sx+.5)/4-radius,dy=y+(sy+.5)/4-radius;
            if(dx*dx+dy*dy<=radius*radius) ++coverage;
        }
        const unsigned alpha=coverage*opacity/16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// Supersampled round-ended strokes, mirrored in both directions. A shared alpha
// mask keeps search, scrollbar and tab arrows identical at fractional DPI/zoom.
// With horizontal set, down selects right instead of down (otherwise left/up).
inline std::shared_ptr<cover_pixels> chevron_icon(int radius,int thickness,bool down,uint32_t color,bool horizontal=false) {
    radius=std::clamp(radius,1,64); thickness=std::clamp(thickness,1,32);
    auto pixels=std::make_shared<cover_pixels>();
    pixels->width=2*radius+thickness+2; pixels->height=radius+thickness+2;
    if(horizontal) std::swap(pixels->width,pixels->height);
    pixels->bgra.resize(size_t(pixels->width)*pixels->height*4);
    const double cx=(horizontal?pixels->height:pixels->width)/2.0, top=1+thickness/2.0, half=thickness/2.0;
    for(unsigned y=0;y<pixels->height;++y) for(unsigned x=0;x<pixels->width;++x) {
        unsigned coverage=0;
        for(int sy=0;sy<4;++sy) for(int sx=0;sx<4;++sx) {
            const double px=horizontal?y+(sy+.5)/4:x+(sx+.5)/4;
            const double forward=horizontal?x+(sx+.5)/4:y+(sy+.5)/4;
            const double py=down?forward:(horizontal?pixels->width:pixels->height)-forward;
            // Fold at the centre, then project onto the left arm's segment.
            const double dx=radius-std::abs(px-cx),dy=py-top;
            const double t=std::clamp((dx+dy)/(2*radius),0.0,1.0);
            const double ex=dx-t*radius,ey=dy-t*radius;
            if(ex*ex+ey*ey<=half*half) ++coverage;
        }
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*pixels->width+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}

// Filled speaker and circular sound waves, sampled into premultiplied alpha.
// The same coverage renderer used by the manager arrows avoids jagged GDI edges.
inline std::shared_ptr<cover_pixels> speaker_icon(unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double px=(x+(sx+.5)/4)*16/size, py=(y+(sy+.5)/4)*16/size-8;
            const bool body=px>=1.5 && px<=4 && std::abs(py)<=2;
            const bool cone=px>=3.5 && px<=7.5 && std::abs(py)<=px-1.5;
            const double dx=px-6.5, radius=std::sqrt(dx*dx+py*py);
            const bool waves=dx>0 && std::abs(py)<=dx*1.05 &&
                (std::abs(radius-4)<=.65 || std::abs(radius-7)<=.65);
            if(body || cone || waves) ++coverage;
        }
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}

// Matching plus and concentric-circle icons for the manager's action buttons.
// Explicit geometry gives them the same visual size regardless of the tab font.
// Strokes follow the scrollbar arrows (SM_CXVSCROLL/8, rounded down): a 16px
// icon at 100% and a 20px icon at 125% both use 2px instead of 2.5px.
inline std::shared_ptr<cover_pixels> manager_action_icon(bool reveal,unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    const double center=size/2.0, half=std::max(1.0,std::floor(size*17.0/128))/2, arm=size*3.0/8;
    const double outer=size*13.0/32, inner=size*3.0/16;
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double dx=x+(sx+.5)/4-center, dy=y+(sy+.5)/4-center;
            if(reveal) {
                const double distance=std::sqrt(dx*dx+dy*dy);
                if(std::abs(distance-outer)<=half || std::abs(distance-inner)<=half) ++coverage;
            } else {
                const double ex=std::max(0.0,std::abs(dx)-arm), ey=std::max(0.0,std::abs(dy)-arm);
                if(ex*ex+dy*dy<=half*half || dx*dx+ey*ey<=half*half) ++coverage;
            }
        }
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}

// Excel-style sheet with small rounded free corners and concave joining flares.
// Active and hovered tabs use the same shadow-free, mirrored contour.
struct sheet_tab_geometry {
    int width=0, height=0, radius=0, flare=0;
    bool attached_top=false;
    int margin() const { return std::clamp(flare,0,std::max(0,height)); }
};
inline std::shared_ptr<cover_pixels> sheet_tab_shape(const sheet_tab_geometry& g,uint32_t color,unsigned opacity) {
    const int w=std::clamp(g.width,1,4096), h=std::clamp(g.height,1,1024);
    const int flare=std::clamp(g.flare,0,h);
    const double r=std::clamp(double(g.radius),0.0,std::min(w,h)/2.0);
    opacity=std::min(opacity,255U);
    auto pixels=std::make_shared<cover_pixels>();
    pixels->width=unsigned(w+2*flare); pixels->height=unsigned(h);
    pixels->bgra.resize(size_t(pixels->width)*pixels->height*4);
    auto inside=[&](double x,double y) {
        // Work in top-manager coordinates, reflecting only the contour.
        if(g.attached_top) y=h-y;
        if(x>=0 && x<w) {
            if(y>=r) return true;
            const double cx=std::clamp(x,r,w-r);
            return (x-cx)*(x-cx)+(y-r)*(y-r)<=r*r;
        }
        if(!flare || y<h-flare) return false;
        const double cx=x<0?-flare:w+flare, cy=h-flare;
        return (x-cx)*(x-cx)+(y-cy)*(y-cy)>double(flare)*flare;
    };
    for(unsigned py=0;py<pixels->height;++py) for(unsigned px=0;px<pixels->width;++px) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx)
            if(inside(px+(sx+.5)/4-flare,py+(sy+.5)/4)) ++coverage;
        const unsigned alpha=coverage*opacity/16;
        const size_t i=(size_t(py)*pixels->width+px)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// Restore the exact panel artwork through the sheet's antialiased coverage.
// The strip is tinted first, so the active sheet joins the image in the playlist
// without adding a differently colored island. Hover restores half the image.
inline void sheet_tab_artwork(cover_pixels& mask,const cover_pixels& surface,int left,int top) {
    for(unsigned y=0;y<mask.height;++y) for(unsigned x=0;x<mask.width;++x) {
        const size_t i=(size_t(y)*mask.width+x)*4;
        const int sx=left+int(x), sy=top+int(y);
        if(sx<0 || sy<0 || sx>=int(surface.width) || sy>=int(surface.height)) {
            for(int c=0;c<4;++c) mask.bgra[i+c]=0;
            continue;
        }
        const size_t source=(size_t(sy)*surface.width+unsigned(sx))*4;
        const unsigned alpha=mask.bgra[i+3];
        for(int c=0;c<4;++c) mask.bgra[i+c]=static_cast<unsigned char>(surface.bgra[source+c]*alpha/255);
    }
}

}
