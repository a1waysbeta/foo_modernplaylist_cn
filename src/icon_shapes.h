#pragma once
#include "artwork.h"
#include <cstdint>

namespace modern_playlist {
struct icon_point { double x,y; };
// Sample a shape 4x4 per pixel into premultiplied alpha; inside() takes pixel
// coordinates, so strokes of any width (1.5px too) render smoothly.
template<class Inside> std::shared_ptr<cover_pixels> sampled_icon(unsigned width,unsigned height,uint32_t color,Inside&& inside) {
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=width; pixels->height=height;
    pixels->bgra.resize(size_t(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx)
            if(inside(x+(sx+.5)/4,y+(sy+.5)/4)) ++coverage;
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*width+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// Whether a point lies within half of a round-ended stroke from a to b.
inline bool near_segment(double x,double y,icon_point a,icon_point b,double half) {
    const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    const double t=length>0?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/length,0.0,1.0):0.0;
    const double ex=x-a.x-t*dx,ey=y-a.y-t*dy; return ex*ex+ey*ey<=half*half;
}
// One weight for every stroked glyph (+, ◎, the sort arrows, the view and
// Sort icons, search/clear and the list chevron): 3/32 of the glyph box,
// rounded to half pixels, so a 16px icon uses 1.5px at 100%, 2px at 125%
// (20px), 2.5px at 150% (24px) and 3px at 200% (32px).
inline double glyph_weight(int size) { return std::max(1,(6*std::max(0,size)+16)/32)/2.0; }
// Search and clear share one antialiased button face at the shared weight.
// The ✕ spans 0.22 to 0.78 of the box (9px of 16, about Material's close).
inline std::shared_ptr<cover_pixels> search_action_icon(unsigned size,uint32_t color,bool clear,double stroke) {
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,double(size));
    const double s=size, half=stroke/2;
    return sampled_icon(size,size,color,[&](double x,double y) {
        if(clear) return near_segment(x,y,{.22*s,.22*s},{.78*s,.78*s},half) || near_segment(x,y,{.22*s,.78*s},{.78*s,.22*s},half);
        const double dx=x-.42*s,dy=y-.42*s,ring=std::sqrt(dx*dx+dy*dy);
        return std::abs(ring-.23*s)<=half || near_segment(x,y,{.585*s,.585*s},{.80*s,.80*s},half);
    });
}
// A one-logical-pixel check stroke at the standard 18px size. Fractional coverage
// keeps the small indicator light, with the same proportions at every DPI/zoom.
inline std::shared_ptr<cover_pixels> state_check_icon(unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    const icon_point points[]={{.20,.50},{.40,.75},{.80,.25}};
    constexpr double radius=1.0/36;
    auto inside=[&](double x,double y) {
        for(int i=0;i<2;++i) {
            const auto a=points[i],b=points[i+1]; const double dx=b.x-a.x,dy=b.y-a.y;
            const double t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy),0.0,1.0);
            const double ex=x-a.x-t*dx,ey=y-a.y-t*dy;
            if(ex*ex+ey*ey<=radius*radius) return true;
        }
        return false;
    };
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx)
            if(inside((x+(sx+.5)/4)/size,(y+(sy+.5)/4)/size)) ++coverage;
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}
// The Material heart and star paths used by foo_nowbar, in a 960-unit square.
// Rasterize at the requested physical size with 4x coverage so both renderers
// use the same smooth contours without depending on a symbol font. Shift moves
// the shape right and down by a fraction of a pixel inside the same bitmap, so
// a shadow can sit closer than one pixel while every layer stays pixel-aligned.
inline std::shared_ptr<cover_pixels> special_icon(bool heart,unsigned size,uint32_t color,unsigned opacity=255,double shift=0) {
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
        const double y=((sy+.5)/4-shift)*960/size; crossings.clear();
        for(size_t i=0,j=points.size()-1;i<points.size();j=i++) {
            const auto a=points[i],b=points[j];
            if((a.y>y)!=(b.y>y)) crossings.push_back((a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y))*extent/960+shift*4);
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
// mask keeps search, scrollbar, list and header arrows identical at fractional
// DPI/zoom; the thickness may be fractional (the list chevron's 1.5px).
// With horizontal set, down selects right instead of down (otherwise left/up).
inline std::shared_ptr<cover_pixels> chevron_icon(int radius,double thickness,bool down,uint32_t color,bool horizontal=false) {
    radius=std::clamp(radius,1,64); thickness=std::clamp(thickness,.5,32.0);
    auto pixels=std::make_shared<cover_pixels>();
    const int extent=int(std::ceil(thickness));
    pixels->width=2*radius+extent+2; pixels->height=radius+extent+2;
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

// Padlock for locked playlists: a rounded body under a stroked shackle, sampled
// on the speaker's 16-unit grid so both tab indicators share weight and edges.
inline std::shared_ptr<cover_pixels> lock_icon(unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned coverage=0;
        for(unsigned sy=0;sy<4;++sy) for(unsigned sx=0;sx<4;++sx) {
            const double px=(x+(sx+.5)/4)*16/size, py=(y+(sy+.5)/4)*16/size;
            const double cx=std::clamp(px,4.5,11.5), cy=std::clamp(py,8.5,13.0);
            const bool body=px>=3 && px<=13 && py>=7 && py<=14.5 && (px-cx)*(px-cx)+(py-cy)*(py-cy)<=2.25;
            const double dx=px-8, dy=py-7, radius=std::sqrt(dx*dx+dy*dy);
            const bool shackle=dy<=0 && std::abs(radius-3.3)<=.7;
            if(body || shackle) ++coverage;
        }
        const unsigned alpha=coverage*255/16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(((color>>16)&255)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(((color>>8)&255)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>((color&255)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    return pixels;
}

// Matching plus and concentric-circle icons for the manager's action buttons,
// at the shared weight and centered at size/2 at every size. Explicit geometry
// gives them the same visual size regardless of the tab font: the + arms and
// the outer ring's stroke lie 3/8 of the box from the center (13.5px across in
// a 16px box), and the inner ring half as far.
inline std::shared_ptr<cover_pixels> manager_action_icon(bool reveal,unsigned size,uint32_t color,double stroke) {
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,double(size));
    const double half=stroke/2, center=size/2.0, arm=size*3.0/8, outer=size*3.0/8, inner=size*3.0/16;
    return sampled_icon(size,size,color,[&](double x,double y) {
        if(reveal) {
            const double distance=std::hypot(x-center,y-center);
            return std::abs(distance-outer)<=half || std::abs(distance-inner)<=half;
        }
        return near_segment(x,y,{center-arm,center},{center+arm,center},half) ||
            near_segment(x,y,{center,center-arm},{center,center+arm},half);
    });
}

// A square with rounded corners of the given radius, sampled like the icons so
// the corners are smooth: the face behind a hovered tab close button.
inline std::shared_ptr<cover_pixels> rounded_square(unsigned size,double radius,uint32_t color) {
    size=std::clamp(size,1U,256U); radius=std::clamp(radius,0.0,size/2.0);
    const double end=size-radius; // Where the far corners' arcs begin.
    return sampled_icon(size,size,color,[&](double x,double y) {
        const double dx=x<radius?radius-x:x>end?x-end:0, dy=y<radius?radius-y:y>end?y-end:0;
        return dx*dx+dy*dy<=radius*radius;
    });
}

// Up/down arrows for the status row's name sorting, on the manager icons' grid:
// the shaft spans the + icon's arms at the shared weight, and the head's
// round-ended diagonals meet at the tip.
inline std::shared_ptr<cover_pixels> sort_arrow_icon(bool down,unsigned size,uint32_t color,double stroke) {
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,double(size));
    const double half=stroke/2, center=size/2.0, arm=size*3.0/8, head=size*5.0/16;
    const icon_point tip{center,center-arm}, tail{center,center+arm};
    return sampled_icon(size,size,color,[&](double x,double sample) {
        const double y=down?size-sample:sample;
        return near_segment(x,y,tip,tail,half) || near_segment(x,y,tip,{center-head,tip.y+head},half) ||
            near_segment(x,y,tip,{center+head,tip.y+head},half);
    });
}

// The drop description's mark beside the dragged track count, as in Windows
// Explorer: → for a move, the sort arrow turned to point right, and + for a
// copy, the manager's own +.
inline std::shared_ptr<cover_pixels> drop_action_icon(bool move,unsigned size,uint32_t color,double stroke) {
    if(!move) return manager_action_icon(false,size,color,stroke);
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,double(size));
    const double half=stroke/2, center=size/2.0, arm=size*3.0/8, head=size*5.0/16;
    const icon_point tip{center+arm,center}, tail{center-arm,center};
    return sampled_icon(size,size,color,[&](double x,double y) {
        return near_segment(x,y,tail,tip,half) || near_segment(x,y,tip,{tip.x-head,center-head},half) ||
            near_segment(x,y,tip,{tip.x-head,center+head},half);
    });
}

// ⇅ for the search row's Sort button, in the status row's arrow style: two
// shafts 7/16 of the box apart, the left pointing up and the right down, from
// 2/16 to 14/16 of the height, with 3/16 heads at 45°. In a 16px box at 1.5px
// the ink is 14.5 x 13.5px, the size of the + icon.
inline std::shared_ptr<cover_pixels> sort_pair_icon(unsigned size,uint32_t color,double stroke) {
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,double(size));
    const double unit=size/16.0, half=stroke/2;
    auto arrow=[&](double x,double y,double shaft,double tip,double tail) {
        const double back=tip<tail?3:-3; const icon_point point{shaft*unit,tip*unit};
        return near_segment(x,y,point,{shaft*unit,tail*unit},half) ||
            near_segment(x,y,point,{(shaft-3)*unit,(tip+back)*unit},half) ||
            near_segment(x,y,point,{(shaft+3)*unit,(tip+back)*unit},half);
    };
    return sampled_icon(size,size,color,[&](double x,double y) { return arrow(x,y,4.5,2,14) || arrow(x,y,11.5,14,2); });
}

// The search row's view toggles: square-ended bars at the shared weight,
// spread at equal steps from the top of the box to its bottom. Ungrouped shows
// three rows of a bullet and a line; grouped, four bars with the second and
// fourth indented (at 0, 4.83, 9.67 and 14.5px in a 16px box at 1.5px).
// The bullets are 3/16 of the box long, rounded to whole pixels (5px rather
// than 4.5 at 150%), so their right edges are sharp; their weight is not.
inline std::shared_ptr<cover_pixels> view_icon(bool grouped,unsigned size,uint32_t color,double stroke) {
    size=std::clamp(size,1U,256U); stroke=std::clamp(stroke,.5,std::max(.5,size/4.0));
    const double unit=size/16.0, bullet=std::round(3*unit); const int rows=grouped?4:3;
    const double step=(size-stroke)/(rows-1);
    return sampled_icon(size,size,color,[&](double x,double y) {
        for(int row=0;row<rows;++row) {
            if(y<row*step || y>=row*step+stroke) continue;
            return grouped?x>=(row%2?4:0)*unit:(x<bullet || x>=6*unit);
        }
        return false;
    });
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

// What a playlist is, for the icon before its name in the playlist list.
// Each kind is a filled Material glyph, which says what the list is, against
// the stroked glyphs of the buttons, which say what they do: queue_music for a
// playlist, auto_awesome for an autoplaylist, library_music for the Media
// Library display, history for the playback history, playlist_play for the
// queue display, and a star, heart or watch_later clock for an autoplaylist
// about ratings, loved tracks or recent playback.
enum class playlist_icon { playlist, autoplaylist, library, history, queue, rated, loved, recent };
// An autoplaylist's kind from its saved configuration, which for foobar2000's
// own autoplaylists holds the query and sort text: the first topic named wins.
inline playlist_icon autoplaylist_icon(std::string configuration) {
    for(auto& c:configuration) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    auto has=[&](const char* word) { return configuration.find(word)!=std::string::npos; };
    if(has("rating")) return playlist_icon::rated;
    if(has("loved") || has("feedback") || has("mood") || has("favorite") || has("favourite")) return playlist_icon::loved;
    if(has("last_played") || has("lastplayed") || has("last played")) return playlist_icon::recent;
    return playlist_icon::autoplaylist;
}
// Fill an SVG path in Material's 24-unit box, even-odd, with special_icon's
// 4x4 coverage. Paths use M, L, H, V, C, S and Z, absolute or relative, with
// implicit repeats; numbers are parsed here, independent of the C locale.
inline std::shared_ptr<cover_pixels> material_icon(const char* path,unsigned size,uint32_t color) {
    size=std::clamp(size,1U,256U);
    std::vector<std::vector<icon_point>> outlines;
    const char* p=path;
    auto skip=[&] { while(*p==' ' || *p==',') ++p; };
    auto number=[&] {
        skip(); double sign=1, value=0, divisor=1; bool fraction=false;
        if(*p=='-' || *p=='+') sign=*p++=='-'?-1:1;
        for(;;++p) {
            if(*p>='0' && *p<='9') { value=value*10+(*p-'0'); if(fraction) divisor*=10; }
            else if(*p=='.' && !fraction) fraction=true;
            else break;
        }
        return sign*value/divisor;
    };
    icon_point current{}, start{}, control{}; char command=0, previous=0; bool open=false;
    auto point=[&](const icon_point& origin) { const double x=number(); return icon_point{origin.x+x,origin.y+number()}; };
    auto line_to=[&](icon_point next) {
        if(!open) { outlines.push_back({current}); open=true; }
        outlines.back().push_back(next); current=next;
    };
    for(skip();*p;skip()) {
        if((*p>='A' && *p<='Z') || (*p>='a' && *p<='z')) command=*p++;
        else if(!((*p>='0' && *p<='9') || *p=='-' || *p=='+' || *p=='.') || !command || command=='z' || command=='Z') break;
        else if(command=='M') command='L'; else if(command=='m') command='l';
        const bool relative=command>='a';
        const icon_point origin=relative?current:icon_point{0,0};
        const char kind=char(relative?command:command-'A'+'a');
        if(kind=='m') { current=start=point(origin); outlines.push_back({current}); open=true; }
        else if(kind=='l') line_to(point(origin));
        else if(kind=='h') line_to({origin.x+number(),current.y});
        else if(kind=='v') line_to({current.x,origin.y+number()});
        else if(kind=='c' || kind=='s') {
            const icon_point from=current;
            icon_point first=from;
            if(kind=='c') first=point(origin);
            else if(previous=='c' || previous=='s') first={2*from.x-control.x,2*from.y-control.y};
            control=point(origin); const icon_point to=point(origin);
            for(int i=1;i<=12;++i) {
                const double t=i/12.0,u=1-t;
                line_to({u*u*u*from.x+3*u*u*t*first.x+3*u*t*t*control.x+t*t*t*to.x,
                    u*u*u*from.y+3*u*u*t*first.y+3*u*t*t*control.y+t*t*t*to.y});
            }
        } else if(kind=='z') { current=start; open=false; }
        else break; // An unsupported command ends the path.
        previous=kind;
    }
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4);
    std::vector<unsigned char> coverage(size_t(size)*size);
    const unsigned extent=size*4;
    std::vector<double> crossings;
    for(unsigned sy=0;sy<extent;++sy) {
        const double y=(sy+.5)/4*24/size; crossings.clear();
        for(const auto& outline:outlines) for(size_t i=0,j=outline.size()-1;i<outline.size();j=i++) {
            const auto a=outline[i],b=outline[j];
            if((a.y>y)!=(b.y>y)) crossings.push_back((a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y))*extent/24);
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
// The heart and star are the Mood and Rating columns' own shapes, so a loved
// or rated list looks like the marks in its rows.
inline std::shared_ptr<cover_pixels> playlist_type_icon(playlist_icon kind,unsigned size,uint32_t color) {
    switch(kind) {
    case playlist_icon::loved: return special_icon(true,size,color);
    case playlist_icon::rated: return special_icon(false,size,color);
    case playlist_icon::autoplaylist: return material_icon("M19 9l1.25-2.75L23 5l-2.75-1.25L19 1l-1.25 2.75L15 5l2.75 1.25L19 9zm-7.5.5L9 4 6.5 9.5 1 12l5.5 2.5L9 20l2.5-5.5L17 12l-5.5-2.5zM19 15l-1.25 2.75L15 19l2.75 1.25L19 23l1.25-2.75L23 19l-2.75-1.25L19 15z",size,color);
    case playlist_icon::library: return material_icon("M20 2H8c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm-2 5h-3v5.5c0 1.38-1.12 2.5-2.5 2.5S10 13.88 10 12.5s1.12-2.5 2.5-2.5c.57 0 1.08.19 1.5.51V5h4v2zM4 6H2v14c0 1.1.9 2 2 2h14v-2H4V6z",size,color);
    case playlist_icon::history: return material_icon("M13 3c-4.97 0-9 4.03-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42C8.27 19.99 10.51 21 13 21c4.97 0 9-4.03 9-9s-4.03-9-9-9zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z",size,color);
    case playlist_icon::queue: return material_icon("M19 9H2v2h17V9zm0-4H2v2h17V5zM2 15h13v-2H2v2zm15-2v6l5-3-5-3z",size,color);
    case playlist_icon::recent: return material_icon("M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10 10-4.5 10-10S17.5 2 12 2zm4.2 14.2L11 13V7h1.5v5.2l4.5 2.7-.8 1.3z",size,color);
    case playlist_icon::playlist: break;
    }
    return material_icon("M15 6H3v2h12V6zm0 4H3v2h12v-2zM3 16h8v-2H3v2zM17 6v8.18c-.31-.11-.65-.18-1-.18-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3V8h3V6h-5z",size,color);
}

}
