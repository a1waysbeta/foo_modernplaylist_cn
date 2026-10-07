#pragma once
#include <algorithm>
#include <cmath>

namespace modern_playlist {
enum class scrollbar_part { none, up, down, thumb, page_up, page_down };
// Pixel geometry and interaction policy, independent of Win32 and any playlist.
struct scrollbar_model {
    int length=0, width=0, arrow=0;
    double content=0, page=0, position=0;
    scrollbar_part pressed=scrollbar_part::none;
    int grab=0;
    double maximum() const { return std::max(0.0,content-page); }
    int track_length() const { return std::max(0,length-2*arrow); }
    int thumb_length() const {
        const int track=track_length();
        if(content<=0) return track;
        return std::clamp(int(std::round(track*std::clamp(page/content,0.0,1.0))),std::min(track,std::max(1,width-2)),track);
    }
    int travel() const { return track_length()-thumb_length(); }
    int thumb_top() const {
        return arrow+(maximum()>0?int(std::round(std::clamp(position/maximum(),0.0,1.0)*travel())):0);
    }
    void geometry(int height,int thickness,int arrow_height,double total,double visible,double target) {
        length=std::max(0,height); width=std::max(0,thickness);
        arrow=std::clamp(arrow_height,0,length/2);
        content=std::max(0.0,total); page=std::max(0.0,visible);
        position=std::clamp(target,0.0,maximum());
    }
    scrollbar_part hit(int y) const {
        if(y<0 || y>=length || maximum()<=0) return scrollbar_part::none;
        if(y<arrow) return scrollbar_part::up;
        if(y>=length-arrow) return scrollbar_part::down;
        if(y<thumb_top()) return scrollbar_part::page_up;
        if(y>=thumb_top()+thumb_length()) return scrollbar_part::page_down;
        return scrollbar_part::thumb;
    }
    void begin(int y) { pressed=hit(y); grab=y-thumb_top(); }
    // Shift+click on the track or thumb: center the thumb on the pointer and
    // keep dragging it from there. Arrows keep their normal line steps.
    bool jump() {
        if(pressed!=scrollbar_part::page_up && pressed!=scrollbar_part::page_down && pressed!=scrollbar_part::thumb) return false;
        pressed=scrollbar_part::thumb; grab=thumb_length()/2; return true;
    }
    void cancel() { pressed=scrollbar_part::none; }
    double drag(int y) const {
        return travel()>0?std::clamp(double(y-grab-arrow)/travel(),0.0,1.0)*maximum():position;
    }
    // Repeat only while still over the originally pressed part; page repeats
    // stop when the thumb reaches the pointer. Dragging never changes display.
    double step(int y,double line) const {
        if(hit(y)!=pressed) return position;
        double delta=0;
        switch(pressed) {
        case scrollbar_part::up: delta=-line; break;
        case scrollbar_part::down: delta=line; break;
        case scrollbar_part::page_up: delta=-page; break;
        case scrollbar_part::page_down: delta=page; break;
        default: break;
        }
        return std::clamp(position+delta,0.0,maximum());
    }
};
}
