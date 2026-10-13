#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
namespace modern_playlist {
enum class row_kind { track, group };
enum class track_kind { file, stream };
template<class Handle> struct playlist_row {
    row_kind type = row_kind::track;
    size_t row_index = 0, track_index = 0, group_index = 0, track_index_in_group = 0;
    Handle metadb;
    unsigned height_in_rows = 1;
    track_kind tracktype = track_kind::file;
    std::vector<size_t> queue_positions; // 1-based; repeated queue occurrences stay distinct
    bool playing = false, paused = false;
};
struct group_position { size_t group = 0, index = 0; };
inline group_position next_group_position(const std::string& previous, const std::string& key,
                                          group_position position, bool first) {
    if (first) return {};
    if (key != previous) return {position.group+1,0};
    return {position.group,position.index+1};
}
inline bool alternate_row(size_t global, size_t in_group, bool grouped) {
    return ((grouped ? in_group : global) & 1) != 0;
}
// Entries with no playlist location identify media, so every matching occurrence
// receives the marker. Located entries identify one exact playlist occurrence.
inline constexpr size_t no_location = std::numeric_limits<size_t>::max();
struct queue_entry { size_t playlist, item; const void* handle; };
inline std::unordered_map<size_t,std::vector<size_t>> queue_positions(
    size_t playlist,const std::vector<const void*>& handles,const std::vector<queue_entry>& queue,bool queue_content=false) {
    std::unordered_map<size_t,std::vector<size_t>> result;
    if(queue_content) {
        // Queue Content mirrors queue order, not the source playlist locations.
        // Wait for its asynchronous synchronization if this snapshot is stale.
        if(handles.size()!=queue.size()) return result;
        for(size_t i=0;i<handles.size();++i) if(handles[i]!=queue[i].handle) return result;
        for(size_t i=0;i<handles.size();++i) result[i].push_back(i+1);
        return result;
    }
    std::unordered_map<const void*,std::vector<size_t>> by_handle;
    bool indexed=false;
    for(size_t i=0;i<queue.size();++i) {
        const auto& entry=queue[i];
        if(entry.playlist==no_location || entry.item==no_location) {
            if(!indexed) {
                for(size_t j=0;j<handles.size();++j) by_handle[handles[j]].push_back(j);
                indexed=true;
            }
            const auto found=by_handle.find(entry.handle);
            if(found!=by_handle.end()) for(auto item:found->second) result[item].push_back(i+1);
        } else if(entry.playlist==playlist && entry.item<handles.size() && handles[entry.item]==entry.handle)
            result[entry.item].push_back(i+1);
    }
    return result;
}
inline std::wstring queue_position_text(const std::vector<size_t>& positions) {
    std::wstring text;
    for(auto position:positions) {
        if(!text.empty()) text+=L", ";
        if(position<10) text+=L'0';
        text+=std::to_wstring(position);
    }
    return text;
}
struct core_settings {
    bool enqueue_on_double_click = false;
    bool alternating = true, group_parity = false, extra_line = false, derived_extra_color = true;
    bool tooltips = false, selected_tooltips = true;
    bool hide_tab_close = true; // Keep the playing-playlist speaker visible.
    bool tab_highlight_text = false, tab_underline = true, tab_separators = true;
    bool tab_color_emoji = true;
    bool tab_custom_highlight = false;
    unsigned tab_highlight_color = 0x00d77800; // COLORREF; host highlight is the default.
    bool rating_compact = false; // Default or compact star spacing.
    bool rating_dots = false; // Style 1: empty stars; Style 2: tiny dots.
    // Track row heights replacing the automatic one, in logical pixels at
    // 96 DPI and 100% zoom: the first pair for the ungrouped view, the second
    // for grouped track rows (group header lines keep the automatic height).
    bool minimum_row_height_enabled = false;
    unsigned minimum_row_height = 30;
    bool group_minimum_row_height_enabled = false;
    unsigned group_minimum_row_height = 30;
    unsigned selection_alpha = 60, focus_alpha = 180, tooltip_delay = 650; // A light selection tint keeps row text colors.
    unsigned tooltip_alpha = 255; // Hover tooltip opacity; lower values let the panel show through.
    std::string tooltip_pattern = "%title%\n[%artist%]\n[%album%][ '('$date(%date%)')']\n[%codec% | ][%bitrate% kbps | ]%length%\n%path%";
};
// Where the track list and the playlist tabs were scrolled when the panel's
// layout was saved; restored once when the panel is created. Panel Settings
// never exports, imports, or resets it.
struct view_position {
    bool list = false;                    // The track list fields are set.
    std::array<uint32_t,4> playlist{};    // The shown playlist's GUID.
    bool group = false;                   // A group header, not a track, is at the top.
    uint32_t item = 0;                    // Playlist index of that track, or of the group's first track.
    int32_t offset = 0;                   // The view's top below the track's or header's top, in 1/1000 rows.
    bool tabs = false;                    // The tab fields are set.
    uint32_t tab = 0;                     // The leftmost visible tab.
    int32_t tab_offset = 0;               // The strip's left edge from that tab's, in 96-DPI pixels at 100% zoom.
};
}
