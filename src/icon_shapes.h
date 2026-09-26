#pragma once
#include "artwork.h"
#include <cstdint>

namespace modern_playlist {
struct icon_point { double x,y; };
// The Material heart and star paths used by foo_nowbar, in a 960-unit square.
// Rasterize at the requested physical size with 4x coverage so both renderers
// use the same smooth contours without depending on a symbol font.
inline std::shared_ptr<cover_pixels> special_icon(bool heart,unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
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
        const unsigned alpha=coverage[i]*255/16;
        pixels->bgra[i*4]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i*4+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i*4+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i*4+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// Supersampled round-ended strokes, mirrored in both directions. A shared alpha
// mask keeps search/scrollbar arrows identical in GDI at fractional DPI/zoom.
inline std::shared_ptr<cover_pixels> chevron_icon(int radius,int thickness,bool down,uint32_t color) {
    radius=std::clamp(radius,1,64); thickness=std::clamp(thickness,1,32);
    auto pixels=std::make_shared<cover_pixels>();
    pixels->width=2*radius+thickness+2; pixels->height=radius+thickness+2;
    pixels->bgra.resize(size_t(pixels->width)*pixels->height*4);
    const double cx=pixels->width/2.0, top=1+thickness/2.0, half=thickness/2.0;
    for(unsigned y=0;y<pixels->height;++y) for(unsigned x=0;x<pixels->width;++x) {
        unsigned coverage=0;
        for(int sy=0;sy<4;++sy) for(int sx=0;sx<4;++sx) {
            const double px=x+(sx+.5)/4, py=down?y+(sy+.5)/4:pixels->height-y-(sy+.5)/4;
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

}
