#pragma once
#include <windows.h>
#include <functional>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include "grouping.h"
#include "artwork.h"
#include "search_model.h"
#include "special_columns.h"

namespace modern_playlist {
// Retains the panel's small LVM/WM_NOTIFY contract while owning pixel geometry.
// This is a custom virtual viewport, not a subclassed native list view.
struct viewport_style {
    int row_height = 30, header_height = 31, padding = 6;
    COLORREF row = 0, alternate = 0, text = 0, selection = 0, selected_text = 0, focus = 0;
    bool alternating = true, group_parity = false, extra_line = false, derived_extra_color = true;
    unsigned selection_alpha = 255, focus_alpha = 255, tooltip_delay = 650;
    bool tooltips = false, enqueue_default = false, selected_tooltips = true;
    COLORREF secondary = 0;
    artwork_settings artwork;
    int cover_margin=4;
    bool show_scrollbar=true;
    unsigned scrollbar_dpi=96;
    bool rating_dots=false;
    int mood_icon_size=15, rating_icon_size=16, rating_dot_size=2;
    int state_check_size=18, state_play_size=17;
    unsigned group_header_rows=2;
    group_font_styles group_fonts=default_group_fonts;

};
// Resolve once at panel creation, before installing child subclasses.
inline constexpr UINT viewport_scrollbar_window = WM_APP + 216;
inline constexpr UINT viewport_horizontal_offset = WM_APP + 215;
inline constexpr UINT viewport_content_width = WM_APP + 213, viewport_scrollbar_capture = WM_APP + 214;
inline constexpr UINT viewport_width_changed = 0x80001008;
inline constexpr UINT viewport_enqueue_query = WM_APP + 208;
inline constexpr UINT viewport_row_info = 0x80001001, viewport_cell_info = 0x80001002, viewport_tooltip_info = 0x80001003;
struct viewport_row_request {
    NMHDR hdr{}; int row = 0;
    size_t global_index = 0, group_index = 0;
    bool playing = false, paused = false;
    std::wstring queue;
};
struct viewport_cell_request {
    NMHDR hdr{}; int row = 0, column = 0;
    bool state_column = false; std::wstring secondary;
    special_column special = special_column::none;
    bool cover=false, artist=false;
};
inline constexpr UINT viewport_row_cover=0x80001009;
struct viewport_cover_request { NMHDR hdr{}; int row=-1; bool load=true, artist=false; std::shared_ptr<cover_pixels> pixels; };
inline constexpr UINT viewport_special_edit=0x80001007;
struct viewport_special_request { NMHDR hdr{}; int row=-1, column=-1, value=0; };
struct viewport_tooltip_request { NMHDR hdr{}; int row = 0; std::wstring text; };
inline constexpr UINT viewport_background=0x80001006;
struct viewport_background_request { NMHDR hdr{}; std::shared_ptr<cover_pixels> pixels; POINT origin{}; };
struct viewport_group {
    group_band band;
    std::wstring l1,r1,l2,r2;
    bool collapsed=false, cover=false, artist_art=false;
    bool artwork_in_header=true;
    int cover_column=-1, artist_column=-1;
};
inline constexpr UINT viewport_group_select=0x8000100a;
inline constexpr UINT viewport_group_toggle=0x80001004, viewport_group_cover=0x80001005;
struct viewport_group_request { NMHDR hdr{}; int group=-1; bool load=true; std::shared_ptr<cover_pixels> pixels; bool artist=false; };
struct viewport_search {
    std::vector<std::wstring> terms;
    std::wstring overlay;
    COLORREF color=0;
    bool found=true;
};
inline constexpr UINT viewport_drop_hit = WM_APP + 211, viewport_drop_clear = WM_APP + 212;
struct viewport_drop_position {
    POINT point{};
    int row=-1, group=-1;
    bool after=false, valid=false, scroll=false;
};
void set_playlist_search(HWND window,const viewport_search& search);
void set_playlist_groups(HWND window,const std::vector<viewport_group>& groups);
void set_playlist_playback(HWND window, int row, bool paused);
void set_playlist_playing_group(HWND window, int group);
HWND create_playlist_viewport(HWND parent, HINSTANCE instance);
void configure_playlist_viewport(HWND window, const viewport_style& style);
// Apply both sides of a divider without exposing an intermediate column layout.
void resize_playlist_column_pair(HWND window,int left,int right,int left_width,int right_width);
void invalidate_playlist_row(HWND window, int row); // drops cached text and coalesces dirty rects
// The predicate is called synchronously, only for bounded cached rows.
void invalidate_playlist_rows(HWND window, const std::function<bool(int)>& affected);
void reset_playlist_scroll(HWND window);
void suspend_playlist_input(HWND window, bool suspended);
}
