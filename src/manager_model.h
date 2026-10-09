#pragma once
#include <algorithm>
#include <vector>
#include <numeric>
#include <string>
namespace modern_playlist {
struct manager_status_geometry { int count_width, button_width; };
inline manager_status_geometry manager_status_layout(int width,int desired_button_width) {
    width=std::max(0,width);
    const int button=std::min(std::max(0,desired_button_width),width/3);
    return {width-2*button,button};
}
inline std::wstring manager_status_text(bool active,size_t count) {
    if(!active) return L"无活动播放列表";
    return std::to_wstring(count)+L" 项";
}
// One pixel coordinate system for painting, hit testing, scrolling and drops.
// Padding is empty strip before the first and after the last tab; it is part
// of the scrollable content, so both end tabs can show their outer flares.
struct manager_geometry {
    std::vector<int> widths;
    int offset=0, viewport=0, padding=0;
    int total() const { int value=widths.empty()?0:2*padding; for(auto width:widths) value+=width; return value; }
    void clamp() { offset=std::clamp(offset,0,std::max(0,total()-viewport)); }
    int left(int index) const { int x=padding-offset; for(int i=0;i<index;++i) x+=widths[i]; return x; }
    int hit(int x) const {
        if(x<0 || x>=viewport) return -1;
        int edge=padding-offset;
        if(x<edge) return -1;
        for(size_t i=0;i<widths.size();++i) { edge+=widths[i]; if(x<edge) return int(i); }
        return -1;
    }
    int insertion(int x) const {
        int edge=padding-offset;
        for(size_t i=0;i<widths.size();++i) { if(x<edge+widths[i]/2) return int(i); edge+=widths[i]; }
        return int(widths.size());
    }
    // Scroll just far enough to show the tab together with its flare padding.
    void reveal(int index) {
        if(index<0 || size_t(index)>=widths.size()) return;
        const int x=left(index);
        if(x<padding) offset+=x-padding;
        else if(x+widths[index]>viewport-padding) offset+=x+widths[index]-(viewport-padding);
        clamp();
    }
};
inline int manager_drop_destination(int from,int before,int count,int pinned=-1) {
    if(from<0 || from>=count || from==pinned || before<0 || before>count) return -1;
    const int to=before>from?before-1:before;
    return pinned==0?std::max(1,to):to;
}
// Stable sorting keeps duplicate names in their existing order; the pinned
// library is excluded from the permutation's sortable range.
template<class Less> std::vector<size_t> manager_name_order(size_t count,bool pinned,Less less) {
    std::vector<size_t> order(count); std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin()+(pinned && count?1:0),order.end(),less);
    return order;
}

}
