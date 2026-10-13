#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
namespace modern_playlist {
struct group_pattern {
    std::string label="Album", key="$if2(%album artist%,%artist%)$char(31)%album%$char(31)%discnumber%";
    std::string l1="%album%", r1="[$date(%date%)]", l2="$if2(%album artist%,%artist%)", r2="[%codec%]";
    std::string l3, r3; // The third line, shown with the Larger header height.
    std::string sort_order="%album artist% | %album% | %discnumber% | %tracknumber% | %title%", playlist_filter="";
    bool show_headers=true;
    // Shipped templates (Album, No grouping) are listed first in Panel Settings,
    // even after being renamed or edited. Added templates are not built in.
    bool builtin=false;
};
inline group_pattern ungrouped_pattern() {
    group_pattern pattern;
    pattern.label="No grouping"; pattern.key="%path%";
    pattern.l1.clear(); pattern.r1.clear(); pattern.l2.clear(); pattern.r2.clear(); pattern.l3.clear(); pattern.r3.clear();
    pattern.sort_order.clear(); pattern.playlist_filter.clear(); pattern.show_headers=false;
    pattern.builtin=true;
    return pattern;
}
inline std::vector<group_pattern> default_patterns() {
    group_pattern album; album.builtin=true;
    return {album,ungrouped_pattern()};
}
struct group_font_style {
    int size_offset=0; // Points relative to the main playlist font, from -2 to +4.
    bool bold=false;
};
using group_font_styles = std::array<group_font_style,4>;
inline constexpr group_font_styles default_group_fonts{{{1,true},{1,true},{-1,false},{-1,false}}};
struct grouping_settings {
    bool enabled=false, playlist_filter=false, collapse_default=false, autocollapse=false;
    bool artwork_in_header=true;
    unsigned header_rows=2, pattern=0; // Header height: 2 (Default, two lines) or 3 (Larger, three lines).
    group_font_styles fonts=default_group_fonts; // Top left/right, then bottom left/right; shared by all templates.
    std::vector<group_pattern> patterns=default_patterns();
    // A playlist filter selects a template without changing the master switch
    // or the panel's shared columns.
    bool active() const { return enabled && pattern<patterns.size() && patterns[pattern].show_headers; }
};
inline std::vector<std::string> filter_names(const std::string& filter) {
    std::vector<std::string> names;
    for(size_t start=0;start<=filter.size();) {
        auto end=filter.find(';',start); if(end==std::string::npos) end=filter.size();
        auto name=filter.substr(start,end-start);
        const auto a=name.find_first_not_of(" \t\r\n"), b=name.find_last_not_of(" \t\r\n");
        if(a!=std::string::npos) names.push_back(name.substr(a,b-a+1));
        if(end==filter.size()) break; start=end+1;
    }
    return names;
}
// Exact playlist names override the first default (*) in displayed template
// order. With no assignment, the user's manual choice stays in effect.
inline size_t assigned_pattern(const std::vector<group_pattern>& patterns,const std::string& name,const std::vector<size_t>& order={}) {
    size_t default_pattern=patterns.size();
    for(size_t slot=0;slot<patterns.size();++slot) {
        const size_t i=order.empty()?slot:order[slot];
        for(const auto& part:filter_names(patterns[i].playlist_filter)) {
            if(part==name && part!="*") return i;
            if(part=="*" && default_pattern==patterns.size()) default_pattern=i;
        }
    }
    return default_pattern;
}
inline size_t matching_pattern(const std::vector<group_pattern>& patterns,const std::string& name,size_t fallback,const std::vector<size_t>& order={}) {
    if(patterns.empty()) return 0;
    const auto assigned=assigned_pattern(patterns,name,order);
    return assigned<patterns.size()?assigned:std::min(fallback,patterns.size()-1);
}
inline bool apply_playlist_filter(grouping_settings& settings,const std::string& name,const std::vector<size_t>& order={}) {
    if(!settings.enabled || !settings.playlist_filter || settings.patterns.empty()) return false;
    const auto pattern=matching_pattern(settings.patterns,name,settings.pattern,order);
    if(pattern==settings.pattern) return false;
    settings.pattern=static_cast<unsigned>(pattern);
    return true;
}
inline void migrate_default_group_filter(grouping_settings& settings) {
    const group_pattern original;
    for(auto& p:settings.patterns) if(p.builtin && p.playlist_filter=="*" &&
        p.label==original.label && p.key==original.key && p.l1==original.l1 && p.r1==original.r1 &&
        p.l2==original.l2 && p.r2==original.r2 && p.l3==original.l3 && p.r3==original.r3 &&
        p.sort_order==original.sort_order && p.show_headers==original.show_headers)
        p.playlist_filter.clear();
}
struct group_band { size_t first=0, count=0; unsigned padding=0; };
struct visual_slot { int track=-1, group=-1, line=-1; };
struct group_geometry {
    std::vector<visual_slot> slots;
    std::vector<size_t> track_slots;
    std::vector<size_t> group_slots, group_ends;
    void build(size_t tracks,const std::vector<group_band>& bands,unsigned header_rows=2) {
        slots.clear(); track_slots.assign(tracks,0);
        group_slots.clear(); group_ends.clear();
        size_t row=0;
        auto add=[&] { track_slots[row]=slots.size(); slots.push_back({static_cast<int>(row++),-1,-1}); };
        for(size_t g=0;g<bands.size();++g) {
            const auto& band=bands[g];
            while(row<std::min(tracks,band.first)) add();
            group_slots.push_back(slots.size());
            for(unsigned line=0;line<std::clamp(header_rows,2U,3U);++line)
                slots.push_back({-1,static_cast<int>(g),static_cast<int>(line)});
            const auto end=std::min(tracks,band.first+band.count);
            while(row<end) add();
            for(unsigned n=0;n<band.padding;++n) slots.push_back({-1,static_cast<int>(g),-1});
            group_ends.push_back(slots.size());
        }
        while(row<tracks) add();
        measure(row_height,line_height);
    }
    // Track rows and artwork padding are row_height tall and group header
    // lines line_height, so a grouped track height leaves headers unchanged.
    // tops holds each slot's offset from the first, then the content height.
    int row_height=1, line_height=1;
    std::vector<double> tops{0};
    void measure(int rows,int lines) {
        row_height=std::max(1,rows); line_height=std::max(1,lines);
        tops.assign(slots.size()+1,0);
        for(size_t i=0;i<slots.size();++i) tops[i+1]=tops[i]+height(i);
    }
    int height(size_t visual) const { return visual<slots.size() && slots[visual].line>=0?line_height:row_height; }
    double top(size_t visual) const { return tops[std::min(visual,tops.size()-1)]; }
    double content() const { return tops.back(); }
    // The slot at an offset from the first slot's top, or -1 outside them.
    int at(double offset) const {
        if(offset<0 || offset>=content()) return -1;
        return int(std::upper_bound(tops.begin(),tops.end(),offset)-tops.begin())-1;
    }
    // The slots overlapping [offset, offset+page).
    std::pair<size_t,size_t> visible(double offset,double page) const {
        if(page<=0 || slots.empty()) return {0,0};
        const auto first=std::upper_bound(tops.begin(),tops.end(),std::max(0.0,offset))-tops.begin()-1;
        const auto end=std::lower_bound(tops.begin(),tops.end(),offset+page)-tops.begin();
        return {std::min(slots.size(),size_t(first)),std::min(slots.size(),size_t(end))};
    }
};
}
