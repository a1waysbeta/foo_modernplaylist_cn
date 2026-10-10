#include <SDK/foobar2000.h>
#include <SDK/message_loop.h>
#include <SDK/coreDarkMode.h>
#include <helpers/playlist_position_reference_tracker.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <functional>
#include <chrono>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include "grouping.h"
#include "artwork_win.h"
#include "search_model.h"
#include <commdlg.h>
#include <shellapi.h>
#include "manager_model.h"
#include "manager_win.h"
#include "tab_text_win.h"
#include "special_playlists.h"
#include "playlist_locks.h"
#include <SDK/autoplaylist.h>
#include "model.h"
#include "drag_drop.h"
#include "playlist_viewport.h"
#include "viewport_accessibility.h"
#include "playlist_core.h"
#include "special_columns_sdk.h"
#include "playlist_view.h"
#include "resource.h"

namespace {
constexpr auto element_id = modern_playlist::element_id;
constexpr UINT refresh_message = WM_APP + 71;
constexpr UINT search_timer = 1, incremental_timer = 6;
constexpr UINT wallpaper_timer = 8, parent_timer = 9, parent_repaint_message = WM_APP + 76;
constexpr UINT rename_message = WM_APP + 77; // An inline tab rename lost focus.
constexpr UINT state_timer = 3, state_message = WM_APP + 74;
std::vector<HWND> queue_windows;
class queue_notifications : public playback_queue_callback {
    void on_changed(t_change_origin) override { for (auto window : queue_windows) PostMessageW(window,state_message,0,0); }
};
service_factory_single_t<queue_notifications> queue_factory;
constexpr UINT fit_columns_message = WM_APP + 72, header_order_message = WM_APP + 75;
constexpr wchar_t search_placeholder[] = L"搜索...";
struct palette_colors {
    COLORREF surface;
    COLORREF row;
    COLORREF alternate;
    COLORREF search_bg;
    COLORREF text;
    COLORREF selected_text;
    COLORREF muted;
    COLORREF header;
    COLORREF divider;
    COLORREF selection;
    COLORREF border;
    COLORREF highlight = 0;
};
namespace palette {
constexpr palette_colors dark = {
    RGB(32,35,37),    // surface
    RGB(29,31,32),    // row
    RGB(20,22,23),    // alternate
    RGB(20,22,23),    // search_bg
    RGB(238,238,238), // text
    RGB(238,238,238), // selected_text
    RGB(156,164,170), // muted
    RGB(44,68,82),    // header
    RGB(77,96,108),   // divider
    RGB(52,173,225),  // selection
    RGB(64,76,83)     // border
};
constexpr palette_colors light = {
    RGB(242,244,246), // surface
    RGB(255,255,255), // row
    RGB(246,248,250), // alternate
    RGB(255,255,255), // search_bg
    RGB(32,35,38),    // text
    RGB(255,255,255), // selected_text
    RGB(120,130,140), // muted
    RGB(224,232,238), // header
    RGB(198,208,218), // divider
    RGB(48,150,206),  // selection
    RGB(205,212,218)  // border
};
}
void fill(HDC dc, const RECT& r, COLORREF color) {
    SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
// Mirrored pixel columns include both endpoints; GDI Polyline omits its last
// endpoint, which made the old chevrons visibly asymmetric at some DPI settings.
void draw_chevron(HDC dc,int cx,int top,int radius,int thickness,bool down,COLORREF color) {
    for(int dx=-radius;dx<=radius;++dx) {
        const int offset=down?radius-std::abs(dx):std::abs(dx);
        RECT pixel{cx+dx,top+offset,cx+dx+1,top+offset+thickness}; fill(dc,pixel,color);
    }
}
std::wstring wide(const char* s) { return pfc::stringcvt::string_wide_from_utf8(s).get_ptr(); }
std::string utf8(const std::wstring& s) { return pfc::stringcvt::string_utf8_from_wide(s.c_str()).get_ptr(); }
std::wstring window_text(HWND w) {
    std::wstring s(GetWindowTextLengthW(w) + 1, L'\0');
    s.resize(GetWindowTextW(w, s.data(), static_cast<int>(s.size()))); return s;
}
struct column {
    std::string title, pattern;
    int width = 150, align = LVCFMT_LEFT;
    bool visible = true;
    titleformat_object::ptr script;
    std::string secondary_pattern;
    titleformat_object::ptr secondary_script;
    bool state = false;
    int percent = 0; // Visible width share in basis points.
    std::string ref = "Text", sort_pattern;
    titleformat_object::ptr sort_script;
    uint32_t primary_fields=0, secondary_fields=0;
};
column state_column() { column c{"状态","",65,LVCFMT_CENTER}; c.state=true; c.ref="State"; return c; }
std::vector<column> defaults() {
    std::vector<column> result={
        {"封面","",75}, state_column(), {"索引","",60,LVCFMT_RIGHT},
        {"#","$if2(%tracknumber%,-)",55,LVCFMT_RIGHT}, {"标题","$if2(%title%,%filename_ext%)",240},
        {"年份","$if(%date%,$year(%date%),'-')",65,LVCFMT_RIGHT},
        {"艺术家","$if(%isplaying%,%artist%,$if(%length%,%artist%,流媒体))",170},
        {"专辑","$if2(%album%,$if(%length%,'单曲','网络电台'))",170},
        {"流派","$if2(%genre%,'其它')",120}, {"喜爱","$if(%mood%,1,0)",65,LVCFMT_CENTER},
        {"等级","$if2(%rating%,0)",110,LVCFMT_CENTER},
        {"播放次数","$if2(%play_count%,0)",65,LVCFMT_RIGHT},
        {"比特率","%bitrate% kbps",85,LVCFMT_RIGHT}, {"时间","$if2(%length%,'00:00')",80,LVCFMT_RIGHT}};    const char* refs[]={"Cover","State","Index","Tracknumber","Title","Date","Artist","Album",
                        "Genre","Mood","Rating","Playcount","Bitrate","Duration"};
    for (size_t i=0;i<result.size();++i) result[i].ref=refs[i];
    for (size_t i : {0,2,5,8,9,10,11,12}) result[i].visible=false;
    result[4].secondary_pattern="$if(%length%,%artist%,)";
    result[8].secondary_pattern="$if2(%genre%,'其它')";
    result[13].secondary_pattern="%bitrate% kbps";
    const char* orders[]={
        "", "", "",
        "%tracknumber% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %title%",
        "%title% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber%",
        "%date% | %album artist% | %album% | %discnumber% | %tracknumber% | %title%",
        "%artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%album%,%artist%) | $if(%album%,%date%,'9999') | %album artist% | %discnumber% | %tracknumber% | %title%",
        "%genre% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%mood% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%rating% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%play_count%,0) | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%bitrate% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%length%,' 0:00') | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%"};
    for (size_t i=3;i<result.size();++i) result[i].sort_pattern=orders[i];
    column artist{"艺术家图片","",75}; artist.ref="ArtistArt"; artist.visible=false; result.push_back(artist);
    int total=0; for (const auto& c : result) if (c.visible) total+=c.width;
    for (auto& c : result) if (c.visible) c.percent=c.width*10000/total;
    return result;
}
void migrate_columns(std::vector<column>& columns,bool artist_cover=false,bool legacy_refs=false) {
    const auto catalog=defaults();
    for(auto& c:columns) {
        if(legacy_refs && c.ref=="Text") for(const auto& built_in:catalog) {
            // Legacy layouts had no semantic reference. Keep their formatting.
            if(c.title==built_in.title) { c.ref=built_in.ref; break; }
        }
        if(c.ref=="Artist" && (c.pattern=="$if(%length%,%artist%,'流媒体')" || c.pattern=="$if(%length%,%artist%,流媒体)"))
            c.pattern="$if(%isplaying%,%artist%,$if(%length%,%artist%,流媒体))";
        if(artist_cover && c.ref=="Cover") { c.ref="ArtistArt"; if(c.title=="封面") c.title="艺术家图片"; }
    }
    for(auto c:catalog) {
        if(columns.size()>=64) break;
        if(std::none_of(columns.begin(),columns.end(),[&](const auto& existing){return existing.ref==c.ref;})) {
            c.visible=false; c.percent=0; columns.push_back(std::move(c));
        }
    }
}
std::string tooltip_titleformat(const std::string& text) {
    std::string result;
    for(size_t i=0;i<text.size();++i) {
        if(text[i]=='\r' || text[i]=='\n') {
            if(text[i]=='\r' && i+1<text.size() && text[i+1]=='\n') ++i;
            result+="$char(10)";
        } else result+=text[i];
    }
    return result;
}
void migrate_tooltip_pattern(std::string& pattern) {
    const auto legacy="%title%\n[%artist%]\n[%album%][ '('%date%')']\n[%codec% | ][%bitrate% kbps | ]%length%\n%path%";
    if(tooltip_titleformat(pattern)==tooltip_titleformat(legacy))
        pattern=modern_playlist::core_settings{}.tooltip_pattern;
}
struct dialog_data { std::wstring value; };
// Everything a panel saves. Panel Settings stages a copy; Reset, Import and
// Export exchange this complete record.
struct panel_state {
    std::vector<column> columns=defaults();
    modern_playlist::core_settings core;
    modern_playlist::grouping_settings groups;
    modern_playlist::search_settings search;
    modern_playlist::artwork_settings artwork;
    bool show_tabs=false, show_header=true, headers_follow_alignment=false;
    bool manager_bottom=false, show_scrollbar=true, show_status=true;
    int zoom_percent=100;
};
struct column_preview { metadb_handle_ptr track; size_t index=0, total=0; bool playing=false; };
// Windows' case-insensitive natural order ("Column 2" before "Column 10").
// Ties fall back to the exact label, so the order never depends on positions.
bool natural_less(const std::wstring& a,const std::wstring& b) {
    const int order=StrCmpLogicalW(a.c_str(),b.c_str());
    return order?order<0:a<b;
}
void update_column_preview(HWND wnd,const column_preview& data) {
    std::wstring result;
    for(int id:{IDC_PATTERN,IDC_SECONDARY}) {
        const auto pattern=utf8(window_text(GetDlgItem(wnd,id)));
        titleformat_object::ptr script; pfc::string8 text;
        if(!titleformat_compiler::get()->compile(script,modern_playlist::column_display_pattern(pattern).c_str())) text="无效的标题格式";
        else if(data.track.is_valid()) {
            modern_playlist::column_format_hook hook(data.index,data.total,data.playing);
            play_control::get()->playback_format_title_ex(data.track,&hook,text,script,nullptr,play_control::display_level_all);
        } else text="请选择一首曲目以预览此格式。";
        if(!result.empty()) result+=L"\r\n";
        result+=modern_playlist::parse_colors(wide(text.c_str())).text;
    }
    SetDlgItemTextW(wnd,IDC_TF_PREVIEW,result.c_str());
}
INT_PTR CALLBACK dialog_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<dialog_data*>(GetWindowLongPtrW(wnd, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        d = reinterpret_cast<dialog_data*>(lp); SetWindowLongPtrW(wnd, DWLP_USER, lp);
        SetDlgItemTextW(wnd, IDC_VALUE, d->value.c_str());
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(wnd, IDCANCEL); return TRUE; }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK && d) {
        d->value = window_text(GetDlgItem(wnd, IDC_VALUE));
        if (d->value.empty()) return TRUE;
        EndDialog(wnd, IDOK); return TRUE;
    }
    return FALSE;
}

// Pages reload their controls from the staged settings after Reset/Import.
constexpr UINT page_load_message=WM_APP+120, page_reveal_message=WM_APP+121;
void load_core_page(HWND wnd,const modern_playlist::core_settings& settings) {
    SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_SETCURSEL,settings.enqueue_on_double_click,0);
    SendDlgItemMessageW(wnd,IDC_RATING_STYLE,CB_SETCURSEL,settings.rating_dots,0);
    SendDlgItemMessageW(wnd,IDC_RATING_SPACING,CB_SETCURSEL,settings.rating_compact,0);
    SetDlgItemInt(wnd,IDC_SELECTION_ALPHA,settings.selection_alpha,FALSE);
    SetDlgItemInt(wnd,IDC_FOCUS_ALPHA,settings.focus_alpha,FALSE);
    SetDlgItemInt(wnd,IDC_TOOLTIP_DELAY,settings.tooltip_delay,FALSE);
    SetDlgItemInt(wnd,IDC_TOOLTIP_ALPHA,settings.tooltip_alpha,FALSE);
    CheckDlgButton(wnd,IDC_MIN_ROW_HEIGHT_ENABLED,settings.minimum_row_height_enabled?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemInt(wnd,IDC_MIN_ROW_HEIGHT,settings.minimum_row_height,FALSE);
    EnableWindow(GetDlgItem(wnd,IDC_MIN_ROW_HEIGHT),settings.minimum_row_height_enabled);
    CheckDlgButton(wnd,IDC_ALTERNATING,settings.alternating?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(wnd,IDC_EXTRA_LINE,settings.extra_line?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(wnd,IDC_TOOLTIPS,settings.tooltips?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(wnd,IDC_SELECTED_TOOLTIPS,settings.selected_tooltips?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemTextW(wnd,IDC_TOOLTIP_PATTERN,wide(settings.tooltip_pattern.c_str()).c_str());
}
INT_PTR CALLBACK core_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* settings=reinterpret_cast<modern_playlist::core_settings*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if (msg==WM_INITDIALOG) {
        settings=reinterpret_cast<modern_playlist::core_settings*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        for (auto value : {L"播放",L"添加到播放队列"}) SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        for (auto value : {L"样式 1 - 星星",L"样式 2 - 圆点"}) SendDlgItemMessageW(wnd,IDC_RATING_STYLE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        for (auto value : {L"默认",L"紧凑"}) SendDlgItemMessageW(wnd,IDC_RATING_SPACING,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        SendDlgItemMessageW(wnd,IDC_TOOLTIP_PATTERN,EM_SETLIMITTEXT,16384,0);
        load_core_page(wnd,*settings); return TRUE;
    }
    if (msg==page_load_message && settings) { load_core_page(wnd,*settings); return TRUE; }
    if (msg==WM_COMMAND && LOWORD(wp)==IDCANCEL) { SendMessageW(GetParent(wnd),WM_COMMAND,IDCANCEL,0); return TRUE; }
    if (msg==WM_COMMAND && LOWORD(wp)==IDC_MIN_ROW_HEIGHT_ENABLED) {
        EnableWindow(GetDlgItem(wnd,IDC_MIN_ROW_HEIGHT),IsDlgButtonChecked(wnd,IDC_MIN_ROW_HEIGHT_ENABLED)==BST_CHECKED); return TRUE;
    }
    if (msg==WM_COMMAND && LOWORD(wp)==IDOK && settings) {
        BOOL a=FALSE,b=FALSE,c=FALSE,d=FALSE;
        const auto selection=GetDlgItemInt(wnd,IDC_SELECTION_ALPHA,&a,FALSE), focus=GetDlgItemInt(wnd,IDC_FOCUS_ALPHA,&b,FALSE), delay=GetDlgItemInt(wnd,IDC_TOOLTIP_DELAY,&c,FALSE);
        const auto tooltip=GetDlgItemInt(wnd,IDC_TOOLTIP_ALPHA,&d,FALSE);
        const auto pattern=utf8(window_text(GetDlgItem(wnd,IDC_TOOLTIP_PATTERN)));
        titleformat_object::ptr compiled;
        if (!a || !b || !c || !d || selection>255 || focus>255 || tooltip>255 || delay<100 || delay>5000 || pattern.size()>16384 ||
            (!pattern.empty() && !titleformat_compiler::get()->compile(compiled,tooltip_titleformat(pattern).c_str()))) {
            MessageBoxW(wnd,L"不透明度请使用 0-255，停留时间请使用 100-5000 毫秒，并填写有效的提示标题格式。",L"播放列表设置",MB_OK|MB_ICONINFORMATION); return FALSE;
        }
        const bool minimum_enabled=IsDlgButtonChecked(wnd,IDC_MIN_ROW_HEIGHT_ENABLED)==BST_CHECKED;
        BOOL height_valid=FALSE;
        const auto minimum_height=GetDlgItemInt(wnd,IDC_MIN_ROW_HEIGHT,&height_valid,FALSE);
        height_valid=height_valid && minimum_height>=1 && minimum_height<=300;
        if(minimum_enabled && !height_valid) {
            MessageBoxW(wnd,L"最小行高请使用 1 到 300 像素。",L"播放列表设置",MB_OK|MB_ICONINFORMATION); return FALSE;
        }
        settings->minimum_row_height_enabled=minimum_enabled;
        if(height_valid) settings->minimum_row_height=minimum_height;
        settings->selection_alpha=selection; settings->focus_alpha=focus; settings->tooltip_delay=delay; settings->tooltip_pattern=pattern;
        settings->tooltip_alpha=tooltip;
        settings->enqueue_on_double_click=SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_GETCURSEL,0,0)==1;
        settings->rating_dots=SendDlgItemMessageW(wnd,IDC_RATING_STYLE,CB_GETCURSEL,0,0)==1;
        settings->rating_compact=SendDlgItemMessageW(wnd,IDC_RATING_SPACING,CB_GETCURSEL,0,0)==1;
        settings->group_parity=false;
        settings->alternating=IsDlgButtonChecked(wnd,IDC_ALTERNATING)==BST_CHECKED;
        settings->extra_line=IsDlgButtonChecked(wnd,IDC_EXTRA_LINE)==BST_CHECKED;
        settings->derived_extra_color=true;
        settings->tooltips=IsDlgButtonChecked(wnd,IDC_TOOLTIPS)==BST_CHECKED;
        settings->selected_tooltips=IsDlgButtonChecked(wnd,IDC_SELECTED_TOOLTIPS)==BST_CHECKED;
        return TRUE;
    }
    return FALSE;
}

// Display order is independent of the source IDs stored in existing layouts.
constexpr unsigned artwork_source_ids[]={2,4,1,3};
void update_artwork_controls(HWND wnd) {
    const bool enabled=IsDlgButtonChecked(wnd,IDC_ART_ENABLED)==BST_CHECKED;
    const auto index=SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_GETCURSEL,0,0);
    const auto source=SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_GETITEMDATA,index,0);
    EnableWindow(GetDlgItem(wnd,IDC_ART_SOURCE),enabled);
    EnableWindow(GetDlgItem(wnd,IDC_ART_PATH),enabled && source==1);
    for(auto id:{IDC_ART_OPACITY,IDC_ART_BLUR,IDC_ART_REGION}) EnableWindow(GetDlgItem(wnd,id),enabled);
    for(auto id:{IDC_ART_MODE,IDC_ART_DIMMING}) EnableWindow(GetDlgItem(wnd,id),enabled && source!=3);
}
void load_artwork_page(HWND wnd,const modern_playlist::artwork_settings& settings) {
    CheckDlgButton(wnd,IDC_ART_ASPECT,settings.aspect?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemInt(wnd,IDC_ART_MARGIN,settings.margin,FALSE);
    SetDlgItemInt(wnd,IDC_ART_OPACITY,settings.opacity,FALSE); SetDlgItemInt(wnd,IDC_ART_BLUR,settings.blur,FALSE);
    SetDlgItemInt(wnd,IDC_ART_DIMMING,settings.dimming,FALSE);
    SetDlgItemTextW(wnd,IDC_ART_PATH,wide(settings.path.c_str()).c_str());
    CheckDlgButton(wnd,IDC_ART_ENABLED,settings.enabled?BST_CHECKED:BST_UNCHECKED);
    for(unsigned i=0;i<std::size(artwork_source_ids);++i)
        if(artwork_source_ids[i]==settings.source) SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_SETCURSEL,i,0);
    SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_SETCURSEL,settings.mode==4?1:0,0); SendDlgItemMessageW(wnd,IDC_ART_REGION,CB_SETCURSEL,settings.region,0);
    update_artwork_controls(wnd);
}
INT_PTR CALLBACK artwork_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* settings=reinterpret_cast<modern_playlist::artwork_settings*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        settings=reinterpret_cast<modern_playlist::artwork_settings*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        SendDlgItemMessageW(wnd,IDC_ART_PATH,EM_SETLIMITTEXT,16384,0);
        unsigned source_index=0;
        for(auto label:{L"音轨封面",L"音轨艺术家图片",L"自定义图片",L"仿真透明"}) {
            const auto index=SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_SETITEMDATA,index,artwork_source_ids[source_index++]);
        }
        for(auto label:{L"居中裁剪",L"顶部裁剪"}) SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        // The two displayed choices keep their original serialized mode IDs.
        SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_SETITEMDATA,0,1);
        SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_SETITEMDATA,1,4);
        for(auto label:{L"整个面板",L"播放列表"}) SendDlgItemMessageW(wnd,IDC_ART_REGION,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        load_artwork_page(wnd,*settings);
        return TRUE;
    }
    if(msg==page_load_message && settings) { load_artwork_page(wnd,*settings); return TRUE; }
    if(msg==WM_COMMAND && settings) {
        if(LOWORD(wp)==IDCANCEL) { SendMessageW(GetParent(wnd),WM_COMMAND,IDCANCEL,0); return TRUE; }
        if(LOWORD(wp)==IDOK) {
            auto next=*settings; BOOL a=FALSE,b=FALSE,c=FALSE,d=FALSE;
            next.aspect=IsDlgButtonChecked(wnd,IDC_ART_ASPECT)==BST_CHECKED;
            next.artist=false;
            next.margin=GetDlgItemInt(wnd,IDC_ART_MARGIN,&a,FALSE);
            next.opacity=GetDlgItemInt(wnd,IDC_ART_OPACITY,&b,FALSE); next.blur=GetDlgItemInt(wnd,IDC_ART_BLUR,&c,FALSE);
            next.dimming=GetDlgItemInt(wnd,IDC_ART_DIMMING,&d,FALSE);
            next.enabled=IsDlgButtonChecked(wnd,IDC_ART_ENABLED)==BST_CHECKED;
            const auto source=SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_GETCURSEL,0,0);
            next.source=unsigned(SendDlgItemMessageW(wnd,IDC_ART_SOURCE,CB_GETITEMDATA,source,0));
            const auto mode=SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_GETCURSEL,0,0);
            next.mode=unsigned(SendDlgItemMessageW(wnd,IDC_ART_MODE,CB_GETITEMDATA,mode,0));
            next.region=unsigned(SendDlgItemMessageW(wnd,IDC_ART_REGION,CB_GETCURSEL,0,0));
            next.path=utf8(window_text(GetDlgItem(wnd,IDC_ART_PATH)));
            titleformat_object::ptr script;
            if(!a || !b || !c || !d || !modern_playlist::valid_artwork(next) || (next.enabled && next.source==1 && next.path.empty()) ||
               (!next.path.empty() && !titleformat_compiler::get()->compile(script,next.path.c_str()))) {
                MessageBoxW(wnd,L"边距请使用 0-24，不透明度和变暗请使用 0-255，模糊请使用 0-32，并填写有效的图片路径/标题格式。",L"封面与背景",MB_OK|MB_ICONWARNING); return FALSE;
            }
            *settings=std::move(next); return TRUE;
        }
        if((LOWORD(wp)==IDC_ART_SOURCE && HIWORD(wp)==CBN_SELCHANGE) ||
           (LOWORD(wp)==IDC_ART_ENABLED && HIWORD(wp)==BN_CLICKED)) {
            update_artwork_controls(wnd);
            return TRUE;
        }
    }
    return FALSE;
}

constexpr int group_size_controls[]={IDC_GROUP_TOP_LEFT_SIZE,IDC_GROUP_TOP_RIGHT_SIZE,IDC_GROUP_BOTTOM_LEFT_SIZE,IDC_GROUP_BOTTOM_RIGHT_SIZE};
constexpr int group_bold_controls[]={IDC_GROUP_TOP_LEFT_BOLD,IDC_GROUP_TOP_RIGHT_BOLD,IDC_GROUP_BOTTOM_LEFT_BOLD,IDC_GROUP_BOTTOM_RIGHT_BOLD};
constexpr int column_editor_controls[]={IDC_COLUMN_VISIBLE,IDC_TITLE,IDC_PATTERN,IDC_SECONDARY,IDC_SORT_PATTERN,IDC_REF,IDC_ALIGN,IDC_PERCENT,IDC_TF_PREVIEW,IDC_TF_HELP};
struct panel_settings_data {
    panel_state state;
    fb2k::CCoreDarkModeHooks dark_mode; // Tracks the host setting while the dialog is open.
    COLORREF focus_border=GetSysColor(COLOR_HIGHLIGHT); // The panel's highlight color.
    column_preview preview;
    // Appearance edits apply without the group sorting that pattern edits trigger.
    bool columns_changed=false, groups_changed=false, appearance_changed=false, panel_changed=false;
    HWND pages[6]{};
    int scroll[6]{}, content[6]{};
    bool scroll_dark[6]{}; // Theme applied to a page's own scrollbar.
    int column=-1, group=-1; // Entries shown by the inline Columns/Groups editors.
    int initial_page=0, initial_column=-1;
    bool loading=false; // Suppress change notifications while editors are filled.
    std::function<void(panel_settings_data&)> apply;
    std::function<std::string(const panel_state&)> serialize;
    std::function<panel_state(const std::string&)> parse; // Throws for invalid data.
};
void select_settings_page(HWND wnd,panel_settings_data& data,int index) {
    if(index<0 || index>=6) return;
    TabCtrl_SetCurSel(GetDlgItem(wnd,IDC_PANEL_TABS),index);
    for(int i=0;i<6;++i) ShowWindow(data.pages[i],i==index?SW_SHOW:SW_HIDE);
    SetFocus(GetNextDlgTabItem(data.pages[index],nullptr,FALSE));
}

// Columns and Groups keep their list and editor on one page. Content taller
// than the tab scrolls with the scrollbar, the mouse wheel and keyboard focus.
int page_line(HWND page) { RECT unit{0,0,0,10}; MapDialogRect(page,&unit); return std::max(1,int(unit.bottom)); }
void scroll_page(HWND page,panel_settings_data& data,int index,int target) {
    RECT client{}; GetClientRect(page,&client);
    target=std::clamp(target,0,std::max(0,data.content[index]-int(client.bottom)));
    const int delta=data.scroll[index]-target;
    if(delta) {
        data.scroll[index]=target;
        ScrollWindowEx(page,0,delta,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
    }
    SCROLLINFO info{sizeof(info),SIF_POS}; info.nPos=target; SetScrollInfo(page,SB_VERT,&info,TRUE);
}
void layout_page_scrollbar(HWND page,panel_settings_data& data,int index) {
    RECT client{}; GetClientRect(page,&client);
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE};
    info.nMax=std::max(0,data.content[index]-1); info.nPage=UINT(std::max(0L,LONG(client.bottom)));
    SetScrollInfo(page,SB_VERT,&info,TRUE);
    scroll_page(page,data,index,data.scroll[index]);
}
// The host's dark mode hooks theme the page's controls but not the page's own
// scrollbar. SetWindowTheme sends WM_THEMECHANGED, so apply only changes.
void theme_page_scrollbar(HWND page,panel_settings_data& data,int index) {
    const bool dark=bool(data.dark_mode);
    if(data.scroll_dark[index]==dark) return;
    data.scroll_dark[index]=dark;
    SetWindowTheme(page,dark?L"DarkMode_Explorer":L"Explorer",nullptr);
    RedrawWindow(page,nullptr,nullptr,RDW_FRAME|RDW_INVALIDATE);
}
// The themed client edges of edit boxes and lists differ by mode and Windows
// version. The dark one keeps a white inner line inside its 1px border, which
// made the controls look heavily framed, and darkens the border under the
// mouse until it almost disappears. Focus is a blue box in Windows 10, but
// light sides over a blue bottom line in Windows 11. So the inner line takes
// the control's background, and the outer line the panel's highlight color
// with focus, otherwise the theme's normal border color, also while hovered.
// A frame Panel Settings owns (below) has no theme border to keep when disabled.
void paint_settings_frame(HWND wnd,const panel_settings_data& data,bool focused,bool owned) {
    if(HDC dc=GetWindowDC(wnd)) {
        RECT outer{}; GetWindowRect(wnd,&outer); OffsetRect(&outer,-outer.left,-outer.top);
        RECT inner=outer; InflateRect(&inner,-1,-1);
        wchar_t name[16]{}; GetClassNameW(wnd,name,16);
        const bool list=lstrcmpiW(name,WC_LISTBOXW)==0;
        const bool read_only=!IsWindowEnabled(wnd) || (GetWindowLongPtrW(wnd,GWL_STYLE)&ES_READONLY);
        // The dark mode hooks answer these with the control's background brush
        // and set the DC's background and text colors.
        auto brush=reinterpret_cast<HBRUSH>(SendMessageW(GetParent(wnd),list?WM_CTLCOLORLISTBOX:read_only?WM_CTLCOLORSTATIC:WM_CTLCOLOREDIT,
            reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(wnd)));
        if(brush) {
            FrameRect(dc,&inner,brush);
            if(focused) {
                SetDCBrushColor(dc,data.focus_border);
                FrameRect(dc,&outer,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            } else if(IsWindowEnabled(wnd) || owned) {
                // The themes' normal border, 0x7A7A7A, blends the background toward
                // the text: 144/255 from 0x202020 to 0xC0C0C0 in dark mode, and
                // 133/255 from white to black in light mode. Disabled, a quarter.
                LOGBRUSH solid{};
                const COLORREF background=brush==static_cast<HBRUSH>(GetStockObject(DC_BRUSH))?GetDCBrushColor(dc):
                    GetObjectW(brush,sizeof(solid),&solid) && solid.lbStyle==BS_SOLID?solid.lbColor:GetBkColor(dc);
                const COLORREF text=GetTextColor(dc);
                const unsigned weight=!IsWindowEnabled(wnd)?64:data.dark_mode?144:133;
                auto channel=[weight](unsigned a,unsigned b) { return (a*(255-weight)+b*weight+127)/255; };
                SetDCBrushColor(dc,RGB(channel(GetRValue(background),GetRValue(text)),channel(GetGValue(background),GetGValue(text)),
                    channel(GetBValue(background),GetBValue(text))));
                FrameRect(dc,&outer,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        }
        ReleaseDC(wnd,dc);
    }
}
// Lists and multiline edit boxes have scrollbars in their client edge, so they
// keep it and repaint its border over the theme's. Light mode only repaints
// the focused border.
LRESULT CALLBACK settings_frame_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR settings) {
    if(msg==WM_NCDESTROY) { RemoveWindowSubclass(wnd,settings_frame_proc,id); return DefSubclassProc(wnd,msg,wp,lp); }
    const auto& data=*reinterpret_cast<const panel_settings_data*>(settings);
    const bool focused=GetFocus()==wnd;
    if(msg!=WM_NCPAINT || (!data.dark_mode && !focused)) return DefSubclassProc(wnd,msg,wp,lp);
    const LRESULT result=DefSubclassProc(wnd,msg,wp,lp);
    paint_settings_frame(wnd,data,focused,false);
    return result;
}
// Windows 11 animates a themed border between states, painting frames that
// flickered over a border repainted only in WM_NCPAINT. Single-line edit boxes
// need no scrollbars, so their client edge is replaced by an equal 2px frame
// that only Panel Settings paints; without the edge the theme draws no border.
LRESULT CALLBACK settings_edit_frame_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR settings) {
    if(msg==WM_NCDESTROY) { RemoveWindowSubclass(wnd,settings_edit_frame_proc,id); return DefSubclassProc(wnd,msg,wp,lp); }
    const auto& data=*reinterpret_cast<const panel_settings_data*>(settings);
    switch(msg) {
    case WM_NCCALCSIZE: {
        const LRESULT result=DefSubclassProc(wnd,msg,wp,lp);
        InflateRect(wp?&reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0]:reinterpret_cast<RECT*>(lp),-2,-2);
        return result;
    }
    case WM_NCPAINT: paint_settings_frame(wnd,data,GetFocus()==wnd,true); return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case WM_THEMECHANGED: {
        const LRESULT result=DefSubclassProc(wnd,msg,wp,lp);
        // The control still has focus during WM_KILLFOCUS.
        paint_settings_frame(wnd,data,msg!=WM_KILLFOCUS && GetFocus()==wnd,true);
        return result;
    }
    }
    return DefSubclassProc(wnd,msg,wp,lp);
}
BOOL CALLBACK frame_settings_control(HWND child,LPARAM settings) {
    wchar_t name[16]{}; GetClassNameW(child,name,16);
    const bool edit=lstrcmpiW(name,WC_EDITW)==0;
    if(!(edit || lstrcmpiW(name,WC_LISTBOXW)==0) || !(GetWindowLongPtrW(child,GWL_EXSTYLE)&WS_EX_CLIENTEDGE)) return TRUE;
    if(edit && !(GetWindowLongPtrW(child,GWL_STYLE)&(ES_MULTILINE|WS_HSCROLL|WS_VSCROLL))) {
        SetWindowSubclass(child,settings_edit_frame_proc,2,DWORD_PTR(settings));
        SetWindowLongPtrW(child,GWL_EXSTYLE,GetWindowLongPtrW(child,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);
        SetWindowPos(child,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    } else SetWindowSubclass(child,settings_frame_proc,2,DWORD_PTR(settings));
    return TRUE;
}
LRESULT CALLBACK page_child_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_NCDESTROY) { RemoveWindowSubclass(wnd,page_child_proc,id); return DefSubclassProc(wnd,msg,wp,lp); }
    // Reveal controls reached with Tab. The wheel scrolls the page unless the
    // control can scroll itself; closed dropdowns never change their value.
    if(msg==WM_SETFOCUS) PostMessageW(GetParent(wnd),page_reveal_message,reinterpret_cast<WPARAM>(wnd),0);
    if(msg==WM_MOUSEWHEEL) {
        wchar_t name[32]{}; GetClassNameW(wnd,name,32);
        const bool combo=lstrcmpiW(name,WC_COMBOBOXW)==0;
        if((combo && !SendMessageW(wnd,CB_GETDROPPEDSTATE,0,0)) || (!combo && !(GetWindowLongPtrW(wnd,GWL_STYLE)&WS_VSCROLL)))
            return SendMessageW(GetParent(wnd),msg,wp,lp);
    }
    return DefSubclassProc(wnd,msg,wp,lp);
}
BOOL CALLBACK subclass_page_child(HWND child,LPARAM) { SetWindowSubclass(child,page_child_proc,1,0); return TRUE; }
bool page_scroll_message(HWND page,panel_settings_data& data,int index,UINT msg,WPARAM wp) {
    // The hooks send WM_THEMECHANGED when the host switches dark mode.
    if(msg==WM_THEMECHANGED) { theme_page_scrollbar(page,data,index); return false; }
    if(msg!=WM_VSCROLL && msg!=WM_MOUSEWHEEL && msg!=page_reveal_message) return false;
    RECT client{}; GetClientRect(page,&client);
    const int line=page_line(page);
    if(msg==WM_VSCROLL) {
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(page,SB_VERT,&info);
        int target=data.scroll[index];
        switch(LOWORD(wp)) {
        case SB_LINEUP: target-=line; break;
        case SB_LINEDOWN: target+=line; break;
        case SB_PAGEUP: target-=int(client.bottom); break;
        case SB_PAGEDOWN: target+=int(client.bottom); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: target=info.nTrackPos; break;
        case SB_TOP: target=0; break;
        case SB_BOTTOM: target=data.content[index]; break;
        default: return true;
        }
        scroll_page(page,data,index,target); return true;
    }
    if(msg==WM_MOUSEWHEEL) {
        UINT lines=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        const int distance=lines==WHEEL_PAGESCROLL?int(client.bottom):int(std::min(lines,100U))*line;
        scroll_page(page,data,index,data.scroll[index]-MulDiv(GET_WHEEL_DELTA_WPARAM(wp),distance,WHEEL_DELTA));
        return true;
    }
    if(msg==page_reveal_message) {
        const HWND child=reinterpret_cast<HWND>(wp);
        if(!IsWindow(child) || GetParent(child)!=page) return true;
        RECT rect{}; GetWindowRect(child,&rect); MapWindowPoints(nullptr,page,reinterpret_cast<POINT*>(&rect),2);
        if(rect.top<0) scroll_page(page,data,index,data.scroll[index]+int(rect.top)-line);
        else if(rect.bottom>client.bottom) scroll_page(page,data,index,data.scroll[index]+int(rect.bottom-client.bottom)+line);
        return true;
    }
    return false;
}
int list_item_data(HWND list,int item) { return item>=0?int(SendMessageW(list,LB_GETITEMDATA,item,0)):-1; }
int find_list_item(HWND list,int data) {
    const int count=int(SendMessageW(list,LB_GETCOUNT,0,0));
    for(int i=0;i<count;++i) if(list_item_data(list,i)==data) return i;
    return -1;
}
void relabel_list_item(HWND list,int data,const std::wstring& label) {
    const int item=find_list_item(list,data); if(item<0) return;
    const bool selected=SendMessageW(list,LB_GETCURSEL,0,0)==item;
    SendMessageW(list,LB_DELETESTRING,item,0);
    SendMessageW(list,LB_INSERTSTRING,item,reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(list,LB_SETITEMDATA,item,data);
    if(selected) SendMessageW(list,LB_SETCURSEL,item,0);
}
// The item that takes a deleted entry's place: the next one, else the previous.
int list_neighbour(HWND list,int data) {
    const int item=find_list_item(list,data), count=int(SendMessageW(list,LB_GETCOUNT,0,0));
    if(item<0) return -1;
    return item+1<count?list_item_data(list,item+1):item>0?list_item_data(list,item-1):-1;
}
void fill_settings_list(HWND list,const std::vector<std::wstring>& labels,const std::vector<size_t>& order,int select) {
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);
    int selected=order.empty()?-1:0;
    for(const size_t index:order) {
        const int item=int(SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(labels[index].c_str())));
        SendMessageW(list,LB_SETITEMDATA,item,LPARAM(index));
        if(int(index)==select) selected=item;
    }
    SendMessageW(list,LB_SETCURSEL,selected,0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0); InvalidateRect(list,nullptr,TRUE);
}

bool built_in_ref(const std::string& ref) {
    const auto catalog=defaults();
    return std::any_of(catalog.begin(),catalog.end(),[&](const auto& c){return c.ref==ref;});
}
// Loading a layout restores missing built-ins as hidden entries, so a built-in
// can be deleted only while another column still supplies its semantic ref.
const wchar_t* column_delete_problem(const std::vector<column>& columns,size_t index) {
    const auto& c=columns[index];
    if(built_in_ref(c.ref) && std::count_if(columns.begin(),columns.end(),[&](const auto& other){return other.ref==c.ref;})==1)
        return L"内置列无法删除。取消勾选“显示此列”即可隐藏它。";
    if(c.visible && std::count_if(columns.begin(),columns.end(),[](const auto& other){return other.visible;})==1)
        return L"至少必须保留一列可见。";
    return nullptr;
}
void load_column_editor(HWND page,panel_settings_data& data,int index) {
    auto& columns=data.state.columns;
    data.loading=true;
    data.column=index>=0 && size_t(index)<columns.size()?index:-1;
    const column empty{};
    const auto& c=data.column>=0?columns[data.column]:empty;
    CheckDlgButton(page,IDC_COLUMN_VISIBLE,data.column>=0 && c.visible?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemTextW(page,IDC_TITLE,wide(c.title.c_str()).c_str());
    SetDlgItemTextW(page,IDC_PATTERN,wide(c.pattern.c_str()).c_str());
    SetDlgItemTextW(page,IDC_SECONDARY,wide(c.secondary_pattern.c_str()).c_str());
    SetDlgItemTextW(page,IDC_SORT_PATTERN,wide(c.sort_pattern.c_str()).c_str());
    SetDlgItemTextW(page,IDC_REF,wide(c.ref.c_str()).c_str());
    SetDlgItemInt(page,IDC_PERCENT,unsigned(std::max(0,c.percent/100)),FALSE);
    SendDlgItemMessageW(page,IDC_ALIGN,CB_SETCURSEL,std::clamp(c.align,0,2),0);
    for(int id:column_editor_controls) EnableWindow(GetDlgItem(page,id),data.column>=0);
    EnableWindow(GetDlgItem(page,IDC_SETTINGS_DELETE),data.column>=0 && !column_delete_problem(columns,size_t(data.column)));
    EnableWindow(GetDlgItem(page,IDC_SETTINGS_ADD),columns.size()<64);
    data.loading=false;
    update_column_preview(page,data.preview);
}
void fill_column_list(HWND page,panel_settings_data& data,int select) {
    const auto& columns=data.state.columns;
    std::vector<std::wstring> titles;
    for(const auto& c:columns) titles.push_back(wide(c.title.c_str()));
    std::vector<size_t> order(columns.size()); std::iota(order.begin(),order.end(),size_t(0));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return natural_less(titles[a],titles[b]);});
    HWND list=GetDlgItem(page,IDC_SETTINGS_LIST);
    fill_settings_list(list,titles,order,select);
    load_column_editor(page,data,list_item_data(list,int(SendMessageW(list,LB_GETCURSEL,0,0))));
}
// Validate the editor and store it in the staged layout. Invalid input keeps
// the entry selected, shows the problem and focuses the offending field.
bool commit_column_editor(HWND page,panel_settings_data& data) {
    auto& columns=data.state.columns;
    if(data.column<0 || size_t(data.column)>=columns.size()) return true;
    auto edited=columns[data.column];
    const auto title=utf8(window_text(GetDlgItem(page,IDC_TITLE)));
    const auto pattern=utf8(window_text(GetDlgItem(page,IDC_PATTERN)));
    const auto secondary=utf8(window_text(GetDlgItem(page,IDC_SECONDARY)));
    const auto sort=utf8(window_text(GetDlgItem(page,IDC_SORT_PATTERN)));
    auto ref=utf8(window_text(GetDlgItem(page,IDC_REF))); if(ref.empty()) ref="Text";
    BOOL valid_weight=FALSE; const auto weight=GetDlgItemInt(page,IDC_PERCENT,&valid_weight,FALSE);
    const bool visible=IsDlgButtonChecked(page,IDC_COLUMN_VISIBLE)==BST_CHECKED;
    const bool generated=ref=="State" || ref=="Cover" || ref=="ArtistArt" || ref=="Index";
    auto compiles=[](const std::string& text) {
        titleformat_object::ptr script; return titleformat_compiler::get()->compile(script,text.c_str());
    };
    int failed=0;
    if(title.empty()) failed=IDC_TITLE;
    else if(!generated && (pattern.empty() || !compiles(modern_playlist::column_display_pattern(pattern)))) failed=IDC_PATTERN;
    else if(!secondary.empty() && !compiles(modern_playlist::column_display_pattern(secondary))) failed=IDC_SECONDARY;
    else if(!sort.empty() && !compiles(sort)) failed=IDC_SORT_PATTERN;
    else if(ref.size()>64) failed=IDC_REF;
    else if(!valid_weight || weight>100) failed=IDC_PERCENT;
    else if(!visible && edited.visible && std::count_if(columns.begin(),columns.end(),[](const auto& c){return c.visible;})==1) failed=IDC_COLUMN_VISIBLE;
    if(failed) {
        select_settings_page(GetParent(page),data,1);
        if(failed==IDC_COLUMN_VISIBLE) CheckDlgButton(page,IDC_COLUMN_VISIBLE,BST_CHECKED);
        MessageBoxW(page,failed==IDC_COLUMN_VISIBLE?L"至少必须保留一列可见。":
            L"请输入标题、有效的标题格式、不超过 64 个字符的列内部标识，以及 0 到 100 的宽度权重。",
            L"播放列表列",MB_OK|MB_ICONINFORMATION);
        SetFocus(GetDlgItem(page,failed)); return false;
    }
    edited.title=title; edited.pattern=pattern; edited.secondary_pattern=secondary;
    edited.sort_pattern=sort; edited.ref=ref; edited.state=ref=="State"; edited.visible=visible;
    // The field shows whole percent; keep the saved basis points unless edited.
    if(int(weight)!=edited.percent/100) edited.percent=static_cast<int>(weight)*100;
    // A visible column needs a share of the width when fitting to the window.
    if(visible && !edited.percent) { edited.percent=1000; SetDlgItemInt(page,IDC_PERCENT,10,FALSE); }
    edited.align=std::clamp(int(SendDlgItemMessageW(page,IDC_ALIGN,CB_GETCURSEL,0,0)),0,2);
    auto& current=columns[data.column];
    if(current.title!=edited.title || current.pattern!=edited.pattern || current.secondary_pattern!=edited.secondary_pattern ||
       current.sort_pattern!=edited.sort_pattern || current.ref!=edited.ref || current.visible!=edited.visible ||
       current.percent!=edited.percent || current.align!=edited.align) {
        current=std::move(edited); data.columns_changed=true;
    }
    return true;
}
INT_PTR CALLBACK columns_settings_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<panel_settings_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<panel_settings_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        RECT client{}; GetClientRect(wnd,&client); data->content[1]=int(client.bottom);
        for(auto* name:{L"左对齐",L"右对齐",L"居中"}) SendDlgItemMessageW(wnd,IDC_ALIGN,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        for(int id:{IDC_TITLE,IDC_PATTERN,IDC_SECONDARY,IDC_SORT_PATTERN}) SendDlgItemMessageW(wnd,id,EM_SETLIMITTEXT,16384,0);
        SendDlgItemMessageW(wnd,IDC_REF,EM_SETLIMITTEXT,64,0);
        EnumChildWindows(wnd,subclass_page_child,0);
        fill_column_list(wnd,*data,data->initial_column);
        return TRUE;
    }
    if(!data) return FALSE;
    if(page_scroll_message(wnd,*data,1,msg,wp)) return TRUE;
    if(msg==page_load_message) { data->column=-1; fill_column_list(wnd,*data,-1); return TRUE; }
    if(msg!=WM_COMMAND) return FALSE;
    const int command=LOWORD(wp), code=HIWORD(wp);
    if(command==IDCANCEL || command==IDOK) { SendMessageW(GetParent(wnd),WM_COMMAND,command,0); return TRUE; }
    if(data->loading) return FALSE;
    HWND list=GetDlgItem(wnd,IDC_SETTINGS_LIST);
    if(command==IDC_SETTINGS_LIST && code==LBN_SELCHANGE) {
        const int next=list_item_data(list,int(SendMessageW(list,LB_GETCURSEL,0,0)));
        if(next==data->column) return TRUE;
        if(!commit_column_editor(wnd,*data)) { SendMessageW(list,LB_SETCURSEL,find_list_item(list,data->column),0); return TRUE; }
        fill_column_list(wnd,*data,next); return TRUE;
    }
    if(command==IDC_SETTINGS_LIST && code==LBN_DBLCLK) { SetFocus(GetDlgItem(wnd,IDC_TITLE)); return TRUE; }
    if(command==IDC_SETTINGS_ADD && code==BN_CLICKED) {
        if(!commit_column_editor(wnd,*data) || data->state.columns.size()>=64) return TRUE;
        column added{"新建列","%title%",150}; added.percent=1000;
        data->state.columns.push_back(std::move(added)); data->columns_changed=true;
        fill_column_list(wnd,*data,int(data->state.columns.size())-1);
        HWND title=GetDlgItem(wnd,IDC_TITLE); SetFocus(title); SendMessageW(title,EM_SETSEL,0,-1);
        return TRUE;
    }
    if(command==IDC_SETTINGS_DELETE && code==BN_CLICKED) {
        if(data->column<0) return TRUE;
        const int index=data->column;
        if(const auto problem=column_delete_problem(data->state.columns,size_t(index))) {
            MessageBoxW(wnd,problem,L"播放列表列",MB_OK|MB_ICONINFORMATION); return TRUE;
        }
        // The deleted entry's unsaved edits are discarded, not validated.
        int next=list_neighbour(list,index);
        data->state.columns.erase(data->state.columns.begin()+index); data->columns_changed=true; data->column=-1;
        if(next>index) --next;
        fill_column_list(wnd,*data,next); SetFocus(list);
        return TRUE;
    }
    if(command==IDC_SETTINGS_RESET && code==BN_CLICKED) {
        data->state.columns=defaults(); data->columns_changed=true; data->column=-1;
        fill_column_list(wnd,*data,-1); return TRUE;
    }
    if(code==EN_CHANGE && (command==IDC_PATTERN || command==IDC_SECONDARY)) { update_column_preview(wnd,data->preview); return TRUE; }
    if(code==EN_CHANGE && command==IDC_TITLE) {
        // Show the edited name immediately; the list re-sorts after the entry is committed.
        relabel_list_item(list,data->column,window_text(GetDlgItem(wnd,IDC_TITLE)));
        return TRUE;
    }
    if(command==IDC_TF_HELP && code==BN_CLICKED) {
        ShellExecuteW(wnd,L"open",L"https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Title_Formatting_Reference",nullptr,nullptr,SW_SHOWNORMAL); return TRUE;
    }
    return FALSE;
}

std::wstring group_list_label(const modern_playlist::group_pattern& pattern) {
    const auto label=wide(pattern.label.c_str());
    return pattern.builtin?label+L"  (内置)":label;
}
void load_group_appearance(HWND page,const modern_playlist::grouping_settings& groups) {
    SendDlgItemMessageW(page,IDC_GROUP_HEADER_ROWS,CB_SETCURSEL,std::clamp(groups.header_rows,2U,3U)-2,0);
    for(int i=0;i<4;++i) {
        SendDlgItemMessageW(page,group_size_controls[i],CB_SETCURSEL,std::clamp(groups.fonts[i].size_offset,-2,4)+2,0);
        CheckDlgButton(page,group_bold_controls[i],groups.fonts[i].bold?BST_CHECKED:BST_UNCHECKED);
    }
}
// Header height and fonts are shared by every template and need no validation.
void read_group_appearance(HWND page,panel_settings_data& data) {
    auto& groups=data.state.groups;
    const auto rows=SendDlgItemMessageW(page,IDC_GROUP_HEADER_ROWS,CB_GETCURSEL,0,0);
    if(rows>=0 && rows<=1) groups.header_rows=unsigned(rows+2);
    for(int i=0;i<4;++i) {
        const auto size=SendDlgItemMessageW(page,group_size_controls[i],CB_GETCURSEL,0,0);
        if(size>=0 && size<=6) groups.fonts[i].size_offset=int(size)-2;
        groups.fonts[i].bold=IsDlgButtonChecked(page,group_bold_controls[i])==BST_CHECKED;
    }
    data.appearance_changed=true;
}
void update_group_header_fields(HWND page,bool editing) {
    const bool headers=editing && IsDlgButtonChecked(page,IDC_GROUP_HEADERS)==BST_CHECKED;
    for(int id=1301;id<=1306;++id) EnableWindow(GetDlgItem(page,id),headers);
}
void load_group_editor(HWND page,panel_settings_data& data,int index) {
    auto& patterns=data.state.groups.patterns;
    data.loading=true;
    data.group=index>=0 && size_t(index)<patterns.size()?index:-1;
    const modern_playlist::group_pattern empty{};
    const auto& pattern=data.group>=0?patterns[data.group]:empty;
    const std::string* fields[]={&pattern.label,&pattern.key,&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2,&pattern.sort_order,&pattern.playlist_filter};
    for(int i=0;i<8;++i) SetDlgItemTextW(page,1300+i,wide(fields[i]->c_str()).c_str());
    CheckDlgButton(page,IDC_GROUP_HEADERS,pattern.show_headers?BST_CHECKED:BST_UNCHECKED);
    for(int id:{1300,1307,int(IDC_GROUP_HEADERS)}) EnableWindow(GetDlgItem(page,id),data.group>=0);
    update_group_header_fields(page,data.group>=0);
    EnableWindow(GetDlgItem(page,IDC_SETTINGS_DELETE),data.group>=0 && patterns.size()>1);
    EnableWindow(GetDlgItem(page,IDC_SETTINGS_ADD),patterns.size()<64);
    data.loading=false;
}
std::vector<size_t> group_list_order(const std::vector<modern_playlist::group_pattern>& patterns) {
    std::vector<size_t> order(patterns.size()); std::iota(order.begin(),order.end(),size_t(0));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b) {
        if(patterns[a].builtin!=patterns[b].builtin) return patterns[a].builtin;
        return !patterns[a].builtin && natural_less(wide(patterns[a].label.c_str()),wide(patterns[b].label.c_str()));
    });
    return order;
}
void fill_group_list(HWND page,panel_settings_data& data,int select) {
    const auto& patterns=data.state.groups.patterns;
    std::vector<std::wstring> names, labels;
    for(const auto& pattern:patterns) { names.push_back(wide(pattern.label.c_str())); labels.push_back(group_list_label(pattern)); }
    const auto order=group_list_order(patterns);
    HWND list=GetDlgItem(page,IDC_SETTINGS_LIST);
    fill_settings_list(list,labels,order,select);
    load_group_editor(page,data,list_item_data(list,int(SendMessageW(list,LB_GETCURSEL,0,0))));
}
bool commit_group_editor(HWND page,panel_settings_data& data) {
    auto& patterns=data.state.groups.patterns;
    if(data.group<0 || size_t(data.group)>=patterns.size()) return true;
    auto edited=patterns[data.group];
    edited.show_headers=IsDlgButtonChecked(page,IDC_GROUP_HEADERS)==BST_CHECKED;
    std::string* fields[]={&edited.label,&edited.key,&edited.l1,&edited.r1,&edited.l2,&edited.r2,&edited.sort_order,&edited.playlist_filter};
    int failed=0;
    for(int i=0;i<8;++i) {
        *fields[i]=utf8(window_text(GetDlgItem(page,1300+i)));
        titleformat_object::ptr script;
        if(!failed && edited.show_headers && i>=1 && i<=6 && !fields[i]->empty() && !titleformat_compiler::get()->compile(script,fields[i]->c_str())) failed=1300+i;
    }
    if(edited.label.empty()) failed=1300;
    else if(!failed && edited.show_headers && edited.key.empty()) failed=1301;
    if(failed) {
        select_settings_page(GetParent(page),data,2);
        MessageBoxW(page,L"请输入标签。分组标题还需要填写分组依据和有效的标题格式。",L"分组模板",MB_OK|MB_ICONWARNING);
        SetFocus(GetDlgItem(page,failed)); return false;
    }
    auto& current=patterns[data.group];
    if(current.label!=edited.label || current.key!=edited.key || current.l1!=edited.l1 || current.r1!=edited.r1 ||
       current.l2!=edited.l2 || current.r2!=edited.r2 || current.sort_order!=edited.sort_order ||
       current.playlist_filter!=edited.playlist_filter || current.show_headers!=edited.show_headers) {
        current=std::move(edited); data.groups_changed=true;
    }
    return true;
}
INT_PTR CALLBACK groups_settings_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<panel_settings_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<panel_settings_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        RECT client{}; GetClientRect(wnd,&client); data->content[2]=int(client.bottom);
        for(int id=1300;id<=1307;++id) SendDlgItemMessageW(wnd,id,EM_SETLIMITTEXT,16384,0);
        for(auto value:{L"2",L"3"}) SendDlgItemMessageW(wnd,IDC_GROUP_HEADER_ROWS,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        for(int control:group_size_controls) for(int offset=-2;offset<=4;++offset) {
            const auto label=std::to_wstring(offset);
            SendDlgItemMessageW(wnd,control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        EnumChildWindows(wnd,subclass_page_child,0);
        load_group_appearance(wnd,data->state.groups);
        fill_group_list(wnd,*data,int(data->state.groups.pattern));
        return TRUE;
    }
    if(!data) return FALSE;
    if(page_scroll_message(wnd,*data,2,msg,wp)) return TRUE;
    if(msg==page_load_message) {
        load_group_appearance(wnd,data->state.groups);
        data->group=-1; fill_group_list(wnd,*data,int(data->state.groups.pattern)); return TRUE;
    }
    if(msg!=WM_COMMAND) return FALSE;
    const int command=LOWORD(wp), code=HIWORD(wp);
    if(command==IDCANCEL || command==IDOK) { SendMessageW(GetParent(wnd),WM_COMMAND,command,0); return TRUE; }
    if(data->loading) return FALSE;
    HWND list=GetDlgItem(wnd,IDC_SETTINGS_LIST);
    auto& groups=data->state.groups;
    if(command==IDC_SETTINGS_LIST && code==LBN_SELCHANGE) {
        const int next=list_item_data(list,int(SendMessageW(list,LB_GETCURSEL,0,0)));
        if(next==data->group) return TRUE;
        if(!commit_group_editor(wnd,*data)) { SendMessageW(list,LB_SETCURSEL,find_list_item(list,data->group),0); return TRUE; }
        fill_group_list(wnd,*data,next); return TRUE;
    }
    if(command==IDC_SETTINGS_LIST && code==LBN_DBLCLK) { SetFocus(GetDlgItem(wnd,1300)); return TRUE; }
    if(command==IDC_SETTINGS_ADD && code==BN_CLICKED) {
        if(!commit_group_editor(wnd,*data) || groups.patterns.size()>=64) return TRUE;
        modern_playlist::group_pattern added; added.label="新建模板"; added.playlist_filter.clear(); added.builtin=false;
        groups.patterns.push_back(std::move(added)); data->groups_changed=true;
        fill_group_list(wnd,*data,int(groups.patterns.size())-1);
        HWND label=GetDlgItem(wnd,1300); SetFocus(label); SendMessageW(label,EM_SETSEL,0,-1);
        return TRUE;
    }
    if(command==IDC_SETTINGS_DELETE && code==BN_CLICKED) {
        if(data->group<0 || groups.patterns.size()<=1) return TRUE;
        const int index=data->group;
        int next=list_neighbour(list,index);
        groups.patterns.erase(groups.patterns.begin()+index); data->groups_changed=true; data->group=-1;
        if(groups.pattern==unsigned(index)) groups.pattern=0;
        else if(groups.pattern>unsigned(index)) --groups.pattern;
        if(next>index) --next;
        fill_group_list(wnd,*data,next); SetFocus(list);
        return TRUE;
    }
    if(command==IDC_GROUP_HEADERS && code==BN_CLICKED) { update_group_header_fields(wnd,data->group>=0); return TRUE; }
    if(command==1300 && code==EN_CHANGE && data->group>=0) {
        auto pattern=groups.patterns[data->group]; pattern.label=utf8(window_text(GetDlgItem(wnd,1300)));
        relabel_list_item(list,data->group,group_list_label(pattern)); return TRUE;
    }
    const bool size_control=std::find(std::begin(group_size_controls),std::end(group_size_controls),command)!=std::end(group_size_controls);
    const bool bold_control=std::find(std::begin(group_bold_controls),std::end(group_bold_controls),command)!=std::end(group_bold_controls);
    if((code==CBN_SELCHANGE && (command==IDC_GROUP_HEADER_ROWS || size_control)) || (code==BN_CLICKED && bold_control)) {
        read_group_appearance(wnd,*data); return TRUE;
    }
    return FALSE;
}
INT_PTR CALLBACK search_settings_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* settings=reinterpret_cast<modern_playlist::search_settings*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        settings=reinterpret_cast<modern_playlist::search_settings*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        for(unsigned i=1;i<modern_playlist::search_field_count;++i)
            SendDlgItemMessageW(wnd,IDC_TYPING_FIELD,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(modern_playlist::search_fields[i].label));
        SendDlgItemMessageW(wnd,IDC_TYPING_FIELD,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"分组依据"));
    }
    if((msg==WM_INITDIALOG || msg==page_load_message) && settings) {
        SendDlgItemMessageW(wnd,IDC_TYPING_FIELD,CB_SETCURSEL,settings->typing_field-1,0); return TRUE;
    }
    if(msg==WM_COMMAND && LOWORD(wp)==IDCANCEL) { SendMessageW(GetParent(wnd),WM_COMMAND,IDCANCEL,0); return TRUE; }
    return FALSE;
}
INT_PTR CALLBACK manager_settings_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<panel_settings_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<panel_settings_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
    }
    if((msg==WM_INITDIALOG || msg==page_load_message) && data) {
        const auto& core=data->state.core;
        CheckDlgButton(wnd,IDC_MANAGER_BOTTOM,data->state.manager_bottom?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TAB_COLOR_EMOJI,core.tab_color_emoji?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_HIDE_TAB_CLOSE,core.hide_tab_close?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TAB_HIGHLIGHT_TEXT,core.tab_highlight_text?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TAB_UNDERLINE,core.tab_underline?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TAB_SEPARATORS,core.tab_separators?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TAB_CUSTOM_HIGHLIGHT,core.tab_custom_highlight?BST_CHECKED:BST_UNCHECKED);
        EnableWindow(GetDlgItem(wnd,IDC_TAB_HIGHLIGHT_COLOR),core.tab_custom_highlight);
        return TRUE;
    }
    if(msg==WM_COMMAND && data && LOWORD(wp)==IDC_TAB_CUSTOM_HIGHLIGHT) {
        EnableWindow(GetDlgItem(wnd,IDC_TAB_HIGHLIGHT_COLOR),IsDlgButtonChecked(wnd,IDC_TAB_CUSTOM_HIGHLIGHT)==BST_CHECKED);
        return TRUE;
    }
    if(msg==WM_COMMAND && data && LOWORD(wp)==IDC_TAB_HIGHLIGHT_COLOR) {
        static COLORREF custom[16]{};
        CHOOSECOLORW choose{sizeof(choose)}; choose.hwndOwner=wnd;
        choose.rgbResult=data->state.core.tab_highlight_color; choose.lpCustColors=custom;
        choose.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&choose)) data->state.core.tab_highlight_color=choose.rgbResult;
        return TRUE;
    }
    if(msg==WM_COMMAND && (LOWORD(wp)==IDCANCEL || LOWORD(wp)==IDOK)) {
        SendMessageW(GetParent(wnd),WM_COMMAND,LOWORD(wp),0); return TRUE;
    }
    return FALSE;
}
// Validate every page and stage its values without applying them.
bool commit_settings_pages(HWND wnd,panel_settings_data& data) {
    if(!core_dialog(data.pages[0],WM_COMMAND,IDOK,0)) { select_settings_page(wnd,data,0); return false; }
    if(!artwork_dialog(data.pages[5],WM_COMMAND,IDOK,0)) { select_settings_page(wnd,data,5); return false; }
    if(!commit_column_editor(data.pages[1],data) || !commit_group_editor(data.pages[2],data)) return false;
    data.state.search.typing_field=unsigned(std::clamp<LRESULT>(SendDlgItemMessageW(data.pages[4],IDC_TYPING_FIELD,CB_GETCURSEL,0,0),0,modern_playlist::search_field_count-1))+1;
    auto& core=data.state.core; const auto page=data.pages[3];
    data.state.manager_bottom=IsDlgButtonChecked(page,IDC_MANAGER_BOTTOM)==BST_CHECKED;
    core.tab_color_emoji=IsDlgButtonChecked(page,IDC_TAB_COLOR_EMOJI)==BST_CHECKED;
    core.hide_tab_close=IsDlgButtonChecked(page,IDC_HIDE_TAB_CLOSE)==BST_CHECKED;
    core.tab_highlight_text=IsDlgButtonChecked(page,IDC_TAB_HIGHLIGHT_TEXT)==BST_CHECKED;
    core.tab_underline=IsDlgButtonChecked(page,IDC_TAB_UNDERLINE)==BST_CHECKED;
    core.tab_separators=IsDlgButtonChecked(page,IDC_TAB_SEPARATORS)==BST_CHECKED;
    core.tab_custom_highlight=IsDlgButtonChecked(page,IDC_TAB_CUSTOM_HIGHLIGHT)==BST_CHECKED;
    return true;
}
// A file holds this tag followed by the panel's saved configuration record.
constexpr char settings_file_tag[8]={'M','P','L','S','E','T','S','1'};
bool choose_settings_file(HWND owner,bool save,std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    if(save) lstrcpynW(buffer,L"现代播放列表设置.mpsettings",MAX_PATH);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=owner; dialog.lpstrFile=buffer; dialog.nMaxFile=MAX_PATH;
    dialog.lpstrFilter=L"现代播放列表设置 (*.mpsettings)\0*.mpsettings\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrDefExt=L"mpsettings";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))) return false;
    path=buffer; return true;
}
bool write_settings_file(const std::wstring& path,const std::string& record) {
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    const auto contents=std::string(settings_file_tag,sizeof(settings_file_tag))+record;
    DWORD written=0;
    const bool saved=WriteFile(file,contents.data(),DWORD(contents.size()),&written,nullptr) && written==contents.size();
    CloseHandle(file);
    return saved;
}
bool read_settings_file(const std::wstring& path,std::string& record) {
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; std::string contents;
    bool loaded=GetFileSizeEx(file,&size) && size.QuadPart>=LONGLONG(sizeof(settings_file_tag)) && size.QuadPart<=1024*1024;
    if(loaded) {
        contents.resize(size_t(size.QuadPart)); DWORD read=0;
        loaded=ReadFile(file,contents.data(),DWORD(contents.size()),&read,nullptr) && read==contents.size();
    }
    CloseHandle(file);
    if(!loaded || contents.compare(0,sizeof(settings_file_tag),settings_file_tag,sizeof(settings_file_tag))!=0) return false;
    record=contents.substr(sizeof(settings_file_tag)); return true;
}
void reload_settings_pages(panel_settings_data& data) {
    // Everything staged may differ from the panel now, including settings
    // without a page such as the search row, header and manager placement.
    data.column=data.group=-1;
    data.columns_changed=data.groups_changed=data.appearance_changed=data.panel_changed=true;
    for(auto page:data.pages) SendMessageW(page,page_load_message,0,0);
}
INT_PTR CALLBACK panel_settings_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<panel_settings_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<panel_settings_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        HWND tabs=GetDlgItem(wnd,IDC_PANEL_TABS);
        for(auto label:{L"常规",L"列",L"分组",L"播放列表管理器",L"搜索",L"封面和背景"}) {
            TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(label);
            TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&item);
        }
        data->dark_mode.AddDialogWithControls(wnd);
        data->pages[0]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PLAYLIST_CORE),wnd,core_dialog,reinterpret_cast<LPARAM>(&data->state.core));
        data->pages[1]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PANEL_COLUMNS),wnd,columns_settings_dialog,reinterpret_cast<LPARAM>(data));
        data->pages[2]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PANEL_GROUPS),wnd,groups_settings_dialog,reinterpret_cast<LPARAM>(data));
        data->pages[3]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PANEL_MANAGER),wnd,manager_settings_dialog,reinterpret_cast<LPARAM>(data));
        data->pages[4]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PANEL_SEARCH),wnd,search_settings_dialog,reinterpret_cast<LPARAM>(&data->state.search));
        data->pages[5]=CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_ARTWORK),wnd,artwork_dialog,reinterpret_cast<LPARAM>(&data->state.artwork));
        for(auto page:data->pages) if(!page) { EndDialog(wnd,IDCANCEL); return TRUE; }
        for(auto page:data->pages) {
            data->dark_mode.AddDialogWithControls(page);
            EnumChildWindows(page,frame_settings_control,reinterpret_cast<LPARAM>(data));
        }
        RECT area{}; GetClientRect(tabs,&area); TabCtrl_AdjustRect(tabs,FALSE,&area);
        MapWindowPoints(tabs,wnd,reinterpret_cast<POINT*>(&area),2);
        for(auto page:data->pages) SetWindowPos(page,HWND_TOP,area.left,area.top,area.right-area.left,area.bottom-area.top,SWP_NOACTIVATE);
        for(int index:{1,2}) {
            theme_page_scrollbar(data->pages[index],*data,index);
            layout_page_scrollbar(data->pages[index],*data,index);
        }
        select_settings_page(wnd,*data,std::clamp(data->initial_page,0,5)); return FALSE;
    }
    if(msg==WM_NOTIFY && data && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE) {
        select_settings_page(wnd,*data,TabCtrl_GetCurSel(GetDlgItem(wnd,IDC_PANEL_TABS))); return TRUE;
    }
    if(msg==WM_COMMAND && data) {
        if(LOWORD(wp)==IDCANCEL) { EndDialog(wnd,IDCANCEL); return TRUE; }
        if(LOWORD(wp)==IDOK || LOWORD(wp)==IDC_PANEL_APPLY) {
            if(!commit_settings_pages(wnd,*data)) return TRUE;
            // Catalog edits are validated by their editors and staged with the
            // other pages. Cancel discards only changes since the last Apply.
            data->apply(*data);
            data->columns_changed=data->groups_changed=data->appearance_changed=data->panel_changed=false;
            if(LOWORD(wp)==IDOK) { EndDialog(wnd,IDOK); return TRUE; }
            // Re-sort committed names and show any filter-selected template.
            fill_column_list(data->pages[1],*data,data->column);
            fill_group_list(data->pages[2],*data,data->group);
            return TRUE;
        }
        if(LOWORD(wp)==IDC_PANEL_RESET) {
            if(MessageBoxW(wnd,L"将此面板的所有设置重置为默认值？默认值将在你点击“应用”或“确定”后生效。",
                L"面板设置",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return TRUE;
            data->state=panel_state{}; reload_settings_pages(*data); return TRUE;
        }
        if(LOWORD(wp)==IDC_PANEL_IMPORT) {
            std::wstring path; std::string record;
            if(!choose_settings_file(wnd,false,path)) return TRUE;
            try {
                if(!read_settings_file(path,record)) throw std::runtime_error("不是设置文件");
                data->state=data->parse(record);
            } catch(const std::exception&) {
                MessageBoxW(wnd,L"该文件不是有效的现代播放列表设置文件。",L"导入设置",MB_OK|MB_ICONWARNING); return TRUE;
            }
            reload_settings_pages(*data);
            MessageBoxW(wnd,L"设置已导入。将在你点击“应用”或“确定”后生效。",L"导入设置",MB_OK|MB_ICONINFORMATION);
            return TRUE;
        }
        if(LOWORD(wp)==IDC_PANEL_EXPORT) {
            // Export what the dialog shows, including edits not yet applied.
            if(!commit_settings_pages(wnd,*data)) return TRUE;
            std::wstring path;
            if(!choose_settings_file(wnd,true,path)) return TRUE;
            if(!write_settings_file(path,data->serialize(data->state)))
                MessageBoxW(wnd,L"无法写入设置文件。",L"导出设置",MB_OK|MB_ICONWARNING);
            return TRUE;
        }
    }
    return FALSE;
}

struct autoplaylist_data {
    std::string name="新建智能列表",query="ALL",sort="%album artist% | %album% | %discnumber% | %tracknumber%";
    bool force=false;
};
INT_PTR CALLBACK autoplaylist_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<autoplaylist_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<autoplaylist_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        SetDlgItemTextW(wnd,IDC_AUTO_NAME,wide(data->name.c_str()).c_str());
        SetDlgItemTextW(wnd,IDC_AUTO_QUERY,wide(data->query.c_str()).c_str());
        SetDlgItemTextW(wnd,IDC_AUTO_SORT,wide(data->sort.c_str()).c_str());
        for(int id:{IDC_AUTO_NAME,IDC_AUTO_QUERY,IDC_AUTO_SORT}) SendDlgItemMessageW(wnd,id,EM_SETLIMITTEXT,16384,0);
        CheckDlgButton(wnd,IDC_AUTO_FORCE,data->force?BST_CHECKED:BST_UNCHECKED); return TRUE;
    }
    if(msg==WM_COMMAND && data) {
        if(LOWORD(wp)==IDCANCEL) { EndDialog(wnd,IDCANCEL); return TRUE; }
        if(LOWORD(wp)==IDOK) {
            auto edited=*data;
            edited.name=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_NAME)));
            edited.query=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_QUERY)));
            edited.sort=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_SORT)));
            edited.force=IsDlgButtonChecked(wnd,IDC_AUTO_FORCE)==BST_CHECKED;
            try {
                if(edited.name.empty() || edited.query.empty()) throw std::runtime_error("请输入名称和查询。");
                search_filter_manager::get()->create(edited.query.c_str());
                titleformat_object::ptr script;
                if(!edited.sort.empty() && !titleformat_compiler::get()->compile(script,edited.sort.c_str())) throw std::runtime_error("无效的排序标题格式。");
                *data=std::move(edited); EndDialog(wnd,IDOK);
            } catch(const std::exception& e) { MessageBoxW(wnd,wide(e.what()).c_str(),L"智能列表",MB_OK|MB_ICONWARNING); }
            return TRUE;
        }
    }
    return FALSE;
}
class playlist_view : public ui_element_instance, private playlist_callback_impl_base, private play_callback_impl_base, private ui_config_callback_impl, private message_filter_impl_base {
public:
    playlist_view(HWND parent, ui_element_config::ptr config, ui_element_instance_callback_ptr callback, bool visible = true)
        : playlist_callback_impl_base(0), play_callback_impl_base(0), message_filter_impl_base(WM_KEYDOWN,WM_KEYDOWN), callback_(callback) {
        read_config(config);
        INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
        WNDCLASSW wc{}; wc.hInstance = core_api::get_my_instance(); wc.lpfnWndProc = window_proc;
        wc.lpszClassName = L"foo_modernplaylist.view"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"现代播放列表",
            WS_CHILD | (visible ? WS_VISIBLE : 0) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0,0,0,0,parent,nullptr,wc.hInstance,this);
        if (!hwnd_) throw std::runtime_error("无法创建现代播放列表窗口");
        set_callback_flags(static_cast<t_uint32>(playlist_callback::flag_all));
        play_callback_reregister(play_callback::flag_on_playback_new_track | play_callback::flag_on_playback_stop | play_callback::flag_on_playback_pause);
        queue_windows.push_back(hwnd_);
    }
    ~playlist_view() {
        begin_destroy();
        if (hwnd_) DestroyWindow(hwnd_);
        clear_surface();
        if (background_) DeleteObject(background_);
        if (edit_background_) DeleteObject(edit_background_);
        if (font_) DeleteObject(font_);
        if (bold_font_) DeleteObject(bold_font_);
        if (tabs_font_) DeleteObject(tabs_font_);
        if (default_font_) DeleteObject(default_font_);
        if (close_font_) DeleteObject(close_font_);
    }
    HWND get_wnd() override { return hwnd_; }
    GUID get_guid() override { return element_id; }
    GUID get_subclass() override { return ui_element_subclass_playlist_renderers; }
    void set_default_focus() override { SetFocus(list_); }
    void set_configuration(ui_element_config::ptr config) override {
        read_config(config); theme(); make_columns(); refresh(); layout();
    }
    ui_element_config::ptr get_configuration() override {
        save_playlist_columns();
        return write_state(current_state());
    }
    void notify(const GUID&, t_size, const void*, t_size) override { if (hwnd_ && !destroying_) { theme(); layout(); invalidate_all(); } }
private:
    HWND hwnd_ = nullptr, tabs_ = nullptr, search_ = nullptr, list_ = nullptr, header_ = nullptr, notice_ = nullptr, add_ = nullptr, tab_left_ = nullptr, tab_right_ = nullptr;
    HWND search_field_=nullptr, search_scope_=nullptr, search_action_=nullptr;
    bool search_action_hovered_=false;
    HWND ungrouped_view_=nullptr, grouped_view_=nullptr, grouping_tooltip_=nullptr;
    HWND hovered_grouping_button_=nullptr, mouse_focused_grouping_=nullptr;
    RECT search_row_rect_{}, search_edit_face_{};
    bool tabs_join_header_=false; // Top manager directly above the column header.
    // Window themes applied by theme()/update_search_controls(). Reapplying an
    // unchanged theme repaints the control immediately, which flickers on Apply.
    bool search_themed_=false; int themed_dark_=-1;
    bool startup_filled_=false; // The first visible erase covered the panel.
    HWND scrollbar_=nullptr, search_field_list_=nullptr, search_scope_list_=nullptr;
    HWND hovered_search_selector_=nullptr, open_search_selector_=nullptr;
    bool search_selector_mouse_=false;
    int search_field_pixels_=0, search_scope_pixels_=0;
    HWND status_=nullptr, sort_az_=nullptr, sort_za_=nullptr, reveal_active_=nullptr;
    HWND hovered_manager_button_=nullptr, mouse_focused_manager_=nullptr;
    modern_playlist::search_settings search_settings_;
    modern_playlist::incremental_search incremental_;
    std::vector<std::wstring> highlight_terms_;
    std::wstring applied_query_;
    t_size search_reveal_=pfc::infinite_size;
    titleformat_object::ptr typing_search_, wallpaper_script_;
    ui_element_instance_callback_ptr callback_;
    HBRUSH background_ = nullptr, edit_background_ = nullptr;
    HFONT font_ = nullptr, bold_font_ = nullptr, tabs_font_ = nullptr, default_font_ = nullptr;
    palette_colors colors_ = palette::dark;
    int row_pixels_ = 30, tab_pixels_ = 30, text_pixels_ = 19, header_pixels_ = 31;
    UINT dpi_ = 96;
    int zoom_percent_ = 100, zoom_wheel_ = 0;
    bool destroying_ = false;
    bool tabs_dirty_ = true;
    bool manager_bottom_=false;
    modern_playlist::manager_geometry manager_;
    modern_playlist::tab_text_renderer tab_text_;
    t_size measured_playing_=pfc::infinite_size;
    std::shared_ptr<modern_playlist::cover_pixels> speaker_pixels_, lock_pixels_;
    COLORREF speaker_color_=0, lock_color_=0;
    std::vector<std::wstring> tab_names_;
    int drag_before_=-1;
    size_t drag_epoch_=0;
    HFONT close_font_=nullptr;
    Microsoft::WRL::ComPtr<modern_playlist::viewport_accessibility> manager_accessible_;
    bool show_scrollbar_ = true, show_status_ = true;
    bool show_tabs_ = false, show_header_ = true, headers_follow_alignment_ = false;
    bool is_dark_mode() const noexcept {
        if (callback_.is_valid()) return callback_->is_dark_mode();
        return ui_config_manager::g_is_dark_mode();
    }
    bool is_layout_editing() const {
        return callback_.is_valid() && callback_->is_edit_mode_enabled();
    }
    const palette_colors& current_palette() const noexcept { return colors_; }
    std::wstring notice_text_;
    // One column layout belongs to the panel and is used by every playlist.
    std::vector<column> columns_;
    std::vector<int> visible_columns_;
    bool columns_artwork_in_header_=false;
    std::vector<t_size> rows_;
    std::vector<modern_playlist::playlist_row<metadb_handle_ptr>> row_data_;
    modern_playlist::core_settings core_;
    modern_playlist::grouping_settings grouping_;
    std::vector<t_size> filtered_rows_;
    std::vector<modern_playlist::viewport_group> groups_;
    std::vector<std::vector<t_size>> group_members_;
    std::vector<std::string> group_ids_;
    std::map<std::string,bool> collapsed_;
    // The track a click on a collapsed group's header focused, and when.
    t_size group_click_item_=pfc::infinite_size; ULONGLONG group_click_time_=0;
    titleformat_object::ptr group_labels_[4], group_sort_;
    t_size auto_item_=pfc::infinite_size;
    bool apply_filter_next_=true;
    modern_playlist::artwork_cache covers_;
    std::future<std::shared_ptr<modern_playlist::cover_pixels>> cover_job_;
    std::string cover_job_key_;
    modern_playlist::artwork_settings artwork_;
    size_t artwork_epoch_=0, cover_job_epoch_=0, wallpaper_job_epoch_=0;
    std::future<std::shared_ptr<modern_playlist::cover_pixels>> wallpaper_job_;
    std::shared_ptr<modern_playlist::cover_pixels> wallpaper_, surface_;
    std::string wallpaper_key_, wallpaper_job_key_;
    bool wallpaper_known_=false, composing_surface_=false;
    HBRUSH surface_brush_=nullptr, search_surface_brush_=nullptr;
    // Pseudo transparency's blurred parent capture. Recomposing the surface
    // reuses it; check_parent_background() replaces it when the parent's
    // pixels or this panel's place in the parent change. F5 discards it.
    std::shared_ptr<modern_playlist::cover_pixels> parent_capture_;
    modern_playlist::cover_pixels parent_raw_, parent_scratch_; // Unblurred, for change detection.
    std::array<RECT,3> parent_capture_key_{};
    ULONGLONG parent_check_due_=0, parent_move_due_=0, parent_busy_until_=0;
    bool parent_watch_=false, parent_repaint_posted_=false;
    void forget_parent_capture() { parent_capture_.reset(); parent_raw_={}; }
    void clear_surface() {
        surface_.reset();
        if(search_surface_brush_) { DeleteObject(search_surface_brush_); search_surface_brush_=nullptr; }
        if(surface_brush_) { DeleteObject(surface_brush_); surface_brush_=nullptr; }
    }
    void toggle_background() {
        artwork_.enabled=!artwork_.enabled;
        // Preserve the source/options while hiding the background, and reject stale jobs.
        refresh_artwork(false,false);
    }
    void refresh_artwork(bool retain_background=false,bool clear_covers=true) {
        ++artwork_epoch_; if(clear_covers) covers_.clear();
        if(!retain_background) { wallpaper_.reset(); forget_parent_capture(); }
        wallpaper_known_=false;
        clear_surface(); invalidate_all();
    }

    abort_callback_impl cover_abort_;
    titleformat_object::ptr group_script_, tooltip_script_;
    metadb_handle_list items_;
    std::vector<std::wstring> queries_;
    t_size active_ = pfc::infinite_size;
    modern_playlist::drop_registrations drop_targets_;
    std::shared_ptr<bool> drag_alive_=std::make_shared<bool>(true);
    IDataObject* drag_object_=nullptr;
    GUID drag_playlist_{};
    metadb_handle_list drag_items_;
    std::vector<t_size> drag_indices_;
    size_t drag_content_epoch_=0;
    ULONGLONG drop_scroll_time_=0;
    int drop_tab_=-1;
    bool rebuilding_ = false, pending_ = false, dragging_tracks_ = false;
    size_t playlist_epoch_ = 0, content_epoch_ = 0;
    // Register the tracker with the panel, never from inside a playlist callback.
    // Its destructor unregisters it when the panel is released.
    playlist_position_reference_tracker ensure_visible_position_;
    size_t ensure_visible_request_ = 0;
    bool fitting_columns_ = false;
    int sort_column_ = -1, sort_direction_ = 1, drag_tab_ = -1;
    HWND drag_ghost_ = nullptr; POINT drag_tab_start_{}, drag_grab_{}; bool drag_tab_moved_ = false;
    // Inline tab rename: the edit box (a child of the strip), the playlist it
    // renames, the strip area its frame covers, and where focus returns.
    HWND rename_edit_ = nullptr, rename_focus_ = nullptr;
    GUID rename_guid_{}, clicked_tab_{}; // clicked_tab_: the tab the last single click activated.
    RECT rename_frame_{};
    std::wstring cell_;
    int header_candidate_=-1, header_before_=-1;
    int header_resize_left_=-1, header_resize_right_=-1;
    int header_resize_x_=0, header_resize_left_width_=0, header_resize_right_width_=0;
    POINT header_start_{};
    HWND header_ghost_=nullptr;
    HBITMAP header_bitmap_=nullptr;
    SIZE header_ghost_size_{};
    enum class hover_area { none, search, playlist };
    hover_area hover_ = hover_area::none;
    int hovered_tab_ = -1;
    int tab_wheel_accumulator_ = 0;
    int scale(int n) const { return MulDiv(n, static_cast<int>(dpi_) * zoom_percent_, 9600); }
    void update_dpi() {
        using get_dpi_t = UINT(WINAPI*)(HWND);
        const auto get_dpi = reinterpret_cast<get_dpi_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (get_dpi) dpi_ = std::max(96U, get_dpi(hwnd_));
        else { HDC dc=GetDC(hwnd_); dpi_=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(hwnd_,dc); }
    }
    void zoom(short delta) {
        zoom_wheel_ += delta;
        const int steps=zoom_wheel_/WHEEL_DELTA; zoom_wheel_%=WHEEL_DELTA;
        const int next=std::clamp(zoom_percent_+steps*10,50,250);
        if (next==zoom_percent_) return;
        capture_columns(); zoom_percent_=next;
        const int sorted=sort_column_;
        theme(); make_columns(); sort_column_=sorted; layout(); invalidate_all();
    }
    void forget_child(HWND wnd) noexcept {
        if(wnd==tabs_ && manager_accessible_) manager_accessible_->disconnect();
        if(wnd==hovered_manager_button_) hovered_manager_button_=nullptr;
        if(wnd==mouse_focused_manager_) mouse_focused_manager_=nullptr;
        if(wnd==hovered_grouping_button_) hovered_grouping_button_=nullptr;
        if(wnd==mouse_focused_grouping_) mouse_focused_grouping_=nullptr;
        for (HWND* child : {&tabs_,&search_,&list_,&header_,&notice_,&add_,&tab_left_,&tab_right_,&search_field_,&search_scope_,&search_action_,&ungrouped_view_,&grouped_view_,&status_,&sort_az_,&sort_za_,&reveal_active_,&scrollbar_,&search_field_list_,&search_scope_list_})
            if (*child==wnd) *child=nullptr;
    }
    void begin_destroy() noexcept {
        if (destroying_) return;
        destroying_=true;
        end_rename(false);
        if(grouping_tooltip_) { DestroyWindow(grouping_tooltip_); grouping_tooltip_=nullptr; }
        cancel_header_drag();
        stop_track_drop();
        if(manager_accessible_) manager_accessible_->disconnect();
        cover_abort_.abort();
        if(cover_job_.valid()) cover_job_.wait();
        if(wallpaper_job_.valid()) wallpaper_job_.wait();
        if(hwnd_) { KillTimer(hwnd_,4); KillTimer(hwnd_,wallpaper_timer); KillTimer(hwnd_,parent_timer); KillTimer(hwnd_,5); KillTimer(hwnd_,incremental_timer); }
        queue_windows.erase(std::remove(queue_windows.begin(),queue_windows.end(),hwnd_),queue_windows.end());
        if (hwnd_) KillTimer(hwnd_,state_timer);
        // Default UI can destroy the HWND before releasing this service object.
        // Capture while the controls still exist; later get_configuration() uses
        // the saved column data without sending messages to dead/reused HWNDs.
        try { save_playlist_columns(); } catch (...) { /* Keep the last saved layout. */ }
        set_callback_flags(0);
        play_callback_reregister(0);
        pending_=false;
        dragging_tracks_=false;
        drag_tab_=-1;
        if (drag_ghost_) { DestroyWindow(drag_ghost_); drag_ghost_=nullptr; }
        if (hwnd_) KillTimer(hwnd_,search_timer);
        for (HWND child : {tabs_,search_,list_,header_,add_,tab_left_,tab_right_,search_field_,search_scope_,search_action_,ungrouped_view_,grouped_view_,status_,sort_az_,sort_za_,reveal_active_,notice_,scrollbar_,search_field_list_,search_scope_list_})
            if (child) RemoveWindowSubclass(child,child_proc,1);
        if ((list_ && GetCapture()==list_) || (tabs_ && GetCapture()==tabs_)) ReleaseCapture();
    }
    static void write_search(ui_element_config_builder& b,const modern_playlist::search_settings& settings) {
        b << t_uint32(settings.visible) << t_uint32(0) << t_uint32(0)
          << t_uint32(settings.field) << t_uint32(settings.scope) << t_uint32(settings.color);
    }
    static modern_playlist::search_settings read_search(ui_element_config_parser& p,t_uint32 version) {
        modern_playlist::search_settings settings;
        if(version<11) return settings;
        t_uint32 visible,group,locate,field,scope,color;
        p >> visible >> group >> locate >> field >> scope >> color;
        if(visible>1 || group>1 || locate>1 || field>=(version>=16?modern_playlist::search_field_count:4U) || scope>1 || color>0xffffff)
            throw std::runtime_error("无效的搜索设置");
        settings.visible=visible!=0; settings.typing_field=group?modern_playlist::search_field_count:1;
        settings.field=field; settings.scope=scope; settings.color=color; return settings;
    }
    static void write_artwork(ui_element_config_builder& b,const modern_playlist::artwork_settings& s) {
        // Preserve the retired reflection field's slot for existing configurations.
        b << t_uint32(s.aspect) << t_uint32(0) << t_uint32(s.artist) << t_uint32(s.margin)
          << t_uint32(s.source) << t_uint32(s.opacity) << t_uint32(s.blur) << t_uint32(s.mode) << t_uint32(s.region) << pfc::string8(s.path.c_str());
    }
    static modern_playlist::artwork_settings read_artwork(ui_element_config_parser& p,t_uint32 version) {
        modern_playlist::artwork_settings s; if(version<12) return s;
        t_uint32 aspect,reflection,artist; pfc::string8 path;
        p >> aspect >> reflection >> artist >> s.margin >> s.source >> s.opacity >> s.blur >> s.mode >> s.region >> path;
        s.aspect=aspect!=0; s.artist=artist!=0; s.path=path.c_str();
        if(version<23) {
            if(s.source>3 || s.mode>4 || s.region>2) throw std::runtime_error("无效的旧版封面设置");
            if(s.mode!=1 && s.mode!=4) s.mode=4;
            if(s.region==2) s.region=1;
        }
        s.enabled=s.source!=0;
        if(version<29 && s.source==0) s.source=2; // Off keeps backgrounds disabled, with front cover selected.
        if(aspect>1 || reflection>1 || artist>1 || !modern_playlist::valid_artwork(s)) throw std::runtime_error("无效的封面设置");
        return s;
    }
    static void read_artwork_enabled(ui_element_config_parser& p,t_uint32 version,modern_playlist::artwork_settings& s) {
        if(version<29) return; // read_artwork already migrated the old Off/source choice.
        t_uint32 enabled; p >> enabled;
        if(enabled>1) throw std::runtime_error("无效的背景启用标志");
        s.enabled=enabled!=0;
    }
    static bool read_scrollbar(ui_element_config_parser& p,t_uint32 version) {
        if(version<13) return true;
        t_uint32 visible; p >> visible;
        if(visible>1) throw std::runtime_error("无效的滚动条可见性");
        return visible!=0;
    }
    static bool read_status(ui_element_config_parser& p,t_uint32 version) {
        if(version<14) return true;
        t_uint32 visible; p >> visible;
        if(visible>1) throw std::runtime_error("无效的状态栏可见性");
        return visible!=0;
    }
    static bool read_tooltip_target(ui_element_config_parser& p,t_uint32 version) {
        if(version<15) return true;
        t_uint32 selected; p >> selected;
        if(selected>1) throw std::runtime_error("无效的提示目标");
        return selected!=0;
    }
    static bool read_rating_dots(ui_element_config_parser& p,t_uint32 version) {
        if(version<17) return false;
        t_uint32 dots; p >> dots;
        if(dots>1) throw std::runtime_error("无效的等级样式");
        return dots!=0;
    }
    static bool read_group_artwork_in_header(ui_element_config_parser& p,t_uint32 version) {
        if(version<20) return true;
        t_uint32 header_artwork; p >> header_artwork;
        if(header_artwork>1) throw std::runtime_error("无效的分组封面位置");
        return header_artwork!=0;
    }
    static void write_appearance(ui_element_config_builder& b,const modern_playlist::core_settings& core,const modern_playlist::grouping_settings& groups) {
        b << t_uint32(core.minimum_row_height_enabled) << t_uint32(core.minimum_row_height) << t_uint32(groups.header_rows);
        // Store signed point offsets as dropdown indices (0-6).
        for(const auto& font:groups.fonts) b << t_uint32(font.size_offset+2) << t_uint32(font.bold);
    }
    static void read_appearance(ui_element_config_parser& p,t_uint32 version,modern_playlist::core_settings& core,modern_playlist::grouping_settings& groups) {
        if(version<21) return;
        t_uint32 enabled,height,rows; p >> enabled >> height >> rows;
        if(enabled>1 || height<1 || height>300 || rows<2 || rows>3) throw std::runtime_error("无效的行高");
        auto fonts=modern_playlist::default_group_fonts;
        for(auto& font:fonts) {
            t_uint32 size,bold; p >> size >> bold;
            if(size>6 || bold>1) throw std::runtime_error("无效的分组字体");
            font.size_offset=static_cast<int>(size)-2; font.bold=bold!=0;
        }
        core.minimum_row_height_enabled=enabled!=0; core.minimum_row_height=height;
        groups.header_rows=rows; groups.fonts=fonts;
    }
    static void write_groups(ui_element_config_builder& b,const modern_playlist::grouping_settings& settings) {
        b << t_uint32(settings.enabled) << t_uint32(settings.playlist_filter) << t_uint32(settings.collapse_default)
          << t_uint32(settings.autocollapse) << t_uint32(0) << t_uint32(0) // Retired minimum/extra padding slots.
          << t_uint32(settings.pattern) << t_uint32(settings.patterns.size());
        for(const auto& p:settings.patterns)
            b << pfc::string8(p.label.c_str()) << pfc::string8(p.key.c_str()) << pfc::string8(p.l1.c_str()) << pfc::string8(p.r1.c_str())
              << pfc::string8(p.l2.c_str()) << pfc::string8(p.r2.c_str()) << pfc::string8(p.sort_order.c_str()) << pfc::string8(p.playlist_filter.c_str())
              << t_uint32(p.show_headers);
    }
    static modern_playlist::grouping_settings read_groups(ui_element_config_parser& p,t_uint32 version) {
        modern_playlist::grouping_settings settings;
        t_uint32 enabled,filter,collapsed,automatic,minimum,extra,index,count;
        p >> enabled >> filter >> collapsed >> automatic >> minimum >> extra >> index >> count;
        if(enabled>1 || filter>1 || collapsed>1 || automatic>1 || minimum>100 || extra>100 || count<1 || count>64 || index>=count)
            throw std::runtime_error("无效的分组设置");
        settings.enabled=enabled!=0; settings.playlist_filter=filter!=0; settings.collapse_default=collapsed!=0; settings.autocollapse=automatic!=0;
        // Consume legacy padding counts without applying them. Artwork columns
        // still add the space they need using the current viewport geometry.
        settings.pattern=index; settings.patterns.clear();
        for(t_uint32 i=0;i<count;++i) {
            modern_playlist::group_pattern pattern;
            std::string* fields[]={&pattern.label,&pattern.key,&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2,&pattern.sort_order,&pattern.playlist_filter};
            for(auto field:fields) { pfc::string8 text; p >> text; if(text.length()>16384) throw std::runtime_error("分组模板过长"); *field=text.c_str(); }
            if(version>=19) {
                t_uint32 headers; p >> headers;
                if(headers>1) throw std::runtime_error("无效的分组标题标志");
                pattern.show_headers=headers!=0;
            }
            if(pattern.label.empty() || (pattern.show_headers && pattern.key.empty())) throw std::runtime_error("分组模板为空");
            if(pattern.r1=="[%date%]") pattern.r1=modern_playlist::group_pattern{}.r1;
            settings.patterns.push_back(std::move(pattern));
        }
        // Older templates all show headers. Offer the new opt-out without
        // changing their filters, selected template or master grouping switch.
        if(version<19 && settings.patterns.size()<64)
            settings.patterns.push_back(modern_playlist::ungrouped_pattern());
        return settings;
    }
    static bool read_manager_position(ui_element_config_parser& p,t_uint32 version) {
        if(version<10) return false;
        t_uint32 bottom; p >> bottom;
        if(bottom>1) throw std::runtime_error("无效的播放列表管理器位置");
        return bottom!=0;
    }
    static void write_columns(ui_element_config_builder& b, const std::vector<column>& columns) {
        b << t_uint32(columns.size());
        for (const auto& c : columns)
            b << pfc::string8(c.title.c_str()) << pfc::string8(c.pattern.c_str())
              << t_uint32(c.width) << t_uint32(c.align) << t_uint32(c.visible)
              << pfc::string8(c.secondary_pattern.c_str()) << t_uint32(c.state)
              << t_uint32(c.percent) << pfc::string8(c.ref.c_str()) << pfc::string8(c.sort_pattern.c_str());
    }
    static std::vector<column> read_columns(ui_element_config_parser& p,t_uint32 version) {
        t_uint32 count; p >> count;
        if (count == 0 || count > 64) throw std::runtime_error("无效的列数");
        std::vector<column> loaded;
        for (t_uint32 i=0;i<count;++i) {
            pfc::string8 title, pattern; t_uint32 width, align, visible;
            p >> title >> pattern >> width >> align >> visible;
            if (width < 20 || width > 4000 || align > 2)
                throw std::runtime_error("无效的列尺寸");
            loaded.push_back({title.c_str(),pattern.c_str(),static_cast<int>(width),static_cast<int>(align),visible != 0});
            if (version>=7) {
                pfc::string8 secondary; t_uint32 state; p >> secondary >> state;
                if (state>1) throw std::runtime_error("无效的列类型");
                loaded.back().secondary_pattern=secondary.c_str(); loaded.back().state=state!=0;
            } else if (pattern=="%title%") loaded.back().secondary_pattern="[%artist%]";
            if (version>=8) {
                t_uint32 percent; pfc::string8 ref, sort; p >> percent >> ref >> sort;
                if (percent>100000 || ref.length()>64 || sort.length()>16384) throw std::runtime_error("无效的列元数据");
                loaded.back().percent=static_cast<int>(percent); loaded.back().ref=ref.c_str(); loaded.back().sort_pattern=sort.c_str();
            } else loaded.back().ref=loaded.back().state?"State":"Text";
        }
        if (std::none_of(loaded.begin(),loaded.end(),[](const auto& c){return c.visible;}))
            loaded[0].visible=true;
        if (version<7 && loaded.size()<64) loaded.insert(loaded.begin(),state_column());
        if (version<8) {
            int total=0; for (const auto& c:loaded) if (c.visible) total+=c.width;
            for (auto& c:loaded) if (c.visible) c.percent=std::max(1,c.width*10000/total);
        }
        return loaded;
    }
    static std::vector<column> read_legacy_playlist_columns(ui_element_config_parser& p,t_uint32 version,
        std::vector<column> loaded,const GUID* active_id) {
        if(version<5 || version>=18) return loaded;
        t_uint32 count; p >> count;
        if(count>65536) throw std::runtime_error("无效的播放列表布局数");
        bool matched_active=false;
        // Select one shared layout. Consume all legacy records even after a
        // match, so the following panel settings stay aligned in the stream.
        for(t_uint32 i=0;i<count;++i) {
            GUID id; p >> id;
            auto legacy=read_columns(p,version);
            const bool matches=active_id && id==*active_id;
            if(i==0 || (!matched_active && matches)) {
                loaded=std::move(legacy); matched_active=matches;
            }
        }
        return loaded;
    }
    static void write_tab_appearance(ui_element_config_builder& b,const modern_playlist::core_settings& core) {
        b << t_uint32(core.tab_highlight_text) << t_uint32(core.tab_underline) << t_uint32(core.tab_separators)
          << t_uint32(core.tab_custom_highlight) << t_uint32(core.tab_highlight_color) << t_uint32(core.tab_color_emoji);
    }
    static void read_tab_appearance(ui_element_config_parser& p,t_uint32 version,modern_playlist::core_settings& core) {
        if(version<25) return;
        t_uint32 text,underline,separators,custom,color;
        p >> text >> underline >> separators >> custom >> color;
        if(text>1 || underline>1 || separators>1 || custom>1 || color>0xffffff)
            throw std::runtime_error("无效的标签外观");
        core.tab_highlight_text=text!=0; core.tab_underline=underline!=0; core.tab_separators=separators!=0;
        core.tab_custom_highlight=custom!=0; core.tab_highlight_color=color;
        if(version>=26) {
            t_uint32 colored; p >> colored;
            if(colored>1) throw std::runtime_error("无效的标签表情颜色设置");
            core.tab_color_emoji=colored!=0;
        }
    }
    static void write_typing_search(ui_element_config_builder& b,const modern_playlist::search_settings& search) {
        b << t_uint32(search.typing_field);
    }
    static void read_typing_search(ui_element_config_parser& p,t_uint32 version,modern_playlist::search_settings& search) {
        if(version<28) return;
        t_uint32 field; p >> field;
        if(field<1 || field>modern_playlist::search_field_count) throw std::runtime_error("无效的键入搜索字段");
        search.typing_field=field;
    }
    static void write_tooltip_opacity(ui_element_config_builder& b,const modern_playlist::core_settings& core) {
        b << t_uint32(core.tooltip_alpha);
    }
    static void read_tooltip_opacity(ui_element_config_parser& p,t_uint32 version,modern_playlist::core_settings& core) {
        if(version<30) return; // Older tooltips were opaque.
        t_uint32 alpha; p >> alpha;
        if(alpha>255) throw std::runtime_error("无效的提示不透明度");
        core.tooltip_alpha=alpha;
    }
    static void write_rating_spacing(ui_element_config_builder& b,const modern_playlist::core_settings& core) {
        b << t_uint32(core.rating_compact);
    }
    static void read_rating_spacing(ui_element_config_parser& p,t_uint32 version,modern_playlist::core_settings& core) {
        if(version<27) return;
        t_uint32 compact; p >> compact;
        if(compact>1) throw std::runtime_error("无效的等级间距");
        core.rating_compact=compact!=0;
    }
    void save_playlist_columns() {
        // Capture the panel's shared layout, including when no playlist exists.
        finish_header_resize(false);
        capture_columns();
    }
    static void write_group_origins(ui_element_config_builder& b,const modern_playlist::grouping_settings& settings) {
        b << t_uint32(settings.patterns.size());
        for(const auto& pattern:settings.patterns) b << t_uint32(pattern.builtin);
    }
    static void read_group_origins(ui_element_config_parser& p,t_uint32 version,modern_playlist::grouping_settings& settings) {
        if(version<24) {
            // Every panel started with the Album template at index 0, and
            // version 19 added the headerless No grouping template.
            for(auto& pattern:settings.patterns) pattern.builtin=false;
            if(settings.patterns.empty()) return;
            settings.patterns.front().builtin=true;
            const auto none=modern_playlist::ungrouped_pattern();
            const auto found=std::find_if(settings.patterns.begin()+1,settings.patterns.end(),
                [&](const auto& pattern){return !pattern.show_headers && pattern.label==none.label;});
            if(found!=settings.patterns.end()) found->builtin=true;
            return;
        }
        t_uint32 count; p >> count;
        if(count!=settings.patterns.size()) throw std::runtime_error("无效的分组模板来源");
        for(auto& pattern:settings.patterns) {
            t_uint32 builtin; p >> builtin;
            if(builtin>1) throw std::runtime_error("无效的分组模板来源");
            pattern.builtin=builtin!=0;
        }
    }
    static ui_element_config::ptr write_state(const panel_state& s) {
        ui_element_config_builder b;
        b << t_uint32(30);
        write_columns(b,s.columns);
        b << t_uint32(s.show_tabs) << t_uint32(0) << t_uint32(1);
        b << t_uint32(s.zoom_percent);
        const auto& core=s.core;
        b << t_uint32(core.enqueue_on_double_click) << t_uint32(core.alternating) << t_uint32(core.group_parity)
          << t_uint32(core.extra_line) << t_uint32(core.derived_extra_color) << t_uint32(core.tooltips)
          << t_uint32(core.selection_alpha) << t_uint32(core.focus_alpha) << t_uint32(core.tooltip_delay)
          << pfc::string8(core.tooltip_pattern.c_str()) << t_uint32(s.show_header) << t_uint32(s.headers_follow_alignment);
        write_groups(b,s.groups);
        b << t_uint32(s.manager_bottom);
        write_search(b,s.search);
        write_artwork(b,s.artwork);
        b << t_uint32(s.show_scrollbar) << t_uint32(s.show_status) << t_uint32(core.selected_tooltips);
        b << t_uint32(core.rating_dots);
        b << t_uint32(s.groups.artwork_in_header);
        write_appearance(b,core,s.groups);
        b << t_uint32(core.hide_tab_close) << t_uint32(s.artwork.dimming);
        write_group_origins(b,s.groups);
        write_tab_appearance(b,core);
        write_rating_spacing(b,core);
        write_typing_search(b,s.search);
        b << t_uint32(s.artwork.enabled);
        write_tooltip_opacity(b,core);
        return b.finish(element_id);
    }
    // Parse a saved panel (or an imported settings file). Throws on invalid data.
    static panel_state read_state(ui_element_config::ptr config) {
        panel_state s;
        ui_element_config_parser p(config); t_uint32 version; p >> version;
        if (version < 1 || version > 30) throw std::runtime_error("无效的列配置");
        auto loaded=read_columns(p,version);
        t_uint32 tabs=0, fit=1;
        if (version >= 2) p >> tabs;
        if (version >= 3) { t_uint32 reserved; p >> reserved; }
        if (version >= 4) p >> fit;
        if (version >= 5 && version < 18) {
            auto pm=playlist_manager_v5::get();
            const auto active=pm->get_active_playlist();
            const bool have_active=active<pm->get_playlist_count();
            const GUID active_id=have_active?pm->playlist_get_guid(active):GUID{};
            loaded=read_legacy_playlist_columns(p,version,std::move(loaded),have_active?&active_id:nullptr);
        }
        t_uint32 zoom=100;
        if (version >= 6) { p >> zoom; if (zoom < 50 || zoom > 250) throw std::runtime_error("无效的缩放"); }
        auto& core=s.core;
        if (version>=7) {
            t_uint32 enqueue, alternating, parity, extra, derived, tips, selection, focus, delay; pfc::string8 pattern;
            p >> enqueue >> alternating >> parity >> extra >> derived >> tips >> selection >> focus >> delay >> pattern;
            if (enqueue>1 || alternating>1 || parity>1 || extra>1 || derived>1 || tips>1 || selection>255 || focus>255 || delay<100 || delay>5000 || pattern.length()>16384)
                throw std::runtime_error("无效的播放列表设置");
            core.enqueue_on_double_click=enqueue!=0; core.alternating=alternating!=0; core.group_parity=parity!=0;
            core.extra_line=extra!=0; core.derived_extra_color=derived!=0; core.tooltips=tips!=0;
            core.selection_alpha=selection; core.focus_alpha=focus; core.tooltip_delay=delay; core.tooltip_pattern=pattern.c_str();
            migrate_tooltip_pattern(core.tooltip_pattern);
            if (version>=8) {
                t_uint32 header, alignment; p >> header >> alignment;
                if (header>1 || alignment>1) throw std::runtime_error("无效的标题设置");
                s.show_header=header!=0; s.headers_follow_alignment=alignment!=0;
            }
        }
        if(version>=9) s.groups=read_groups(p,version);
        s.manager_bottom=read_manager_position(p,version);
        s.search=read_search(p,version);
        s.artwork=read_artwork(p,version);
        s.show_scrollbar=read_scrollbar(p,version);
        s.show_status=read_status(p,version);
        core.selected_tooltips=read_tooltip_target(p,version);
        core.rating_dots=read_rating_dots(p,version);
        s.groups.artwork_in_header=read_group_artwork_in_header(p,version);
        read_appearance(p,version,core,s.groups);
        if(version>=22) {
            t_uint32 hidden; p >> hidden;
            if(hidden>1) throw std::runtime_error("无效的标签关闭按钮可见性");
            core.hide_tab_close=hidden!=0;
        }
        if(version>=23) {
            t_uint32 dimming; p >> dimming;
            if(dimming>255) throw std::runtime_error("无效的图片变暗");
            s.artwork.dimming=dimming;
        }
        read_group_origins(p,version,s.groups);
        if(version<28) modern_playlist::migrate_default_group_filter(s.groups);
        read_tab_appearance(p,version,core);
        read_rating_spacing(p,version,core);
        read_typing_search(p,version,s.search);
        read_artwork_enabled(p,version,s.artwork);
        read_tooltip_opacity(p,version,core);
        if(version<15) core.tooltips=false;
        core.group_parity=false; core.derived_extra_color=true;
        migrate_columns(loaded,s.artwork.artist,version<8);
        s.artwork.artist=false;
        s.zoom_percent=static_cast<int>(zoom);
        s.columns=std::move(loaded);
        s.show_tabs=tabs != 0;
        return s;
    }
    panel_state current_state() const {
        panel_state s;
        s.columns=columns_; s.core=core_; s.groups=grouping_; s.search=search_settings_; s.artwork=artwork_;
        s.show_tabs=show_tabs_; s.show_header=show_header_;
        s.headers_follow_alignment=headers_follow_alignment_; s.manager_bottom=manager_bottom_;
        s.show_scrollbar=show_scrollbar_; s.show_status=show_status_; s.zoom_percent=zoom_percent_;
        return s;
    }
    void read_config(ui_element_config::ptr config) {
        artwork_={}; refresh_artwork();
        incremental_.clear(); applied_query_.clear(); highlight_terms_.clear();
        collapsed_.clear(); apply_filter_next_=true;
        sort_column_=-1; sort_direction_=1;
        panel_state state;
        if (config.is_valid() && config->get_data_size()) try {
            state=read_state(config);
        } catch (const std::exception&) {
            console::print("现代播放列表：保存的布局无效；将使用默认设置。");
            state=panel_state{};
        }
        columns_=std::move(state.columns); core_=state.core; grouping_=std::move(state.groups);
        search_settings_=state.search; artwork_=std::move(state.artwork);
        show_tabs_=state.show_tabs; show_header_=state.show_header;
        headers_follow_alignment_=state.headers_follow_alignment; manager_bottom_=state.manager_bottom;
        show_scrollbar_=state.show_scrollbar; show_status_=state.show_status; zoom_percent_=state.zoom_percent;
        compile_columns();
    }
    void compile_columns() {
        for (auto& c: columns_) {
            c.primary_fields=modern_playlist::search_format_fields(c.pattern);
            c.secondary_fields=modern_playlist::search_format_fields(c.secondary_pattern);
            c.state=c.ref=="State";
            if(c.ref=="Mood" && c.pattern=="$if(%FEEDBACK%,1,0)") c.pattern="$if(%mood%,1,0)";
            if(c.ref=="Mood" && c.sort_pattern=="%FEEDBACK% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%") c.sort_pattern.replace(0,10,"%mood%");
            titleformat_compiler::get()->compile_safe(c.script,modern_playlist::column_display_pattern(c.pattern).c_str());
            titleformat_compiler::get()->compile_safe(c.secondary_script,modern_playlist::column_display_pattern(c.secondary_pattern).c_str());
            titleformat_compiler::get()->compile_safe(c.sort_script,c.sort_pattern.empty()?c.pattern.c_str():c.sort_pattern.c_str());
        }
        titleformat_compiler::get()->compile_safe(typing_search_,modern_playlist::search_fields[search_settings_.typing_field<modern_playlist::search_field_count?search_settings_.typing_field:1].pattern);
        titleformat_compiler::get()->compile_safe(wallpaper_script_,artwork_.path.c_str());
        const auto& pattern=grouping_.patterns[grouping_.pattern];
        titleformat_compiler::get()->compile_safe(group_script_,pattern.key.c_str());
        const std::string* labels[]={&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2};
        for(int i=0;i<4;++i) titleformat_compiler::get()->compile_safe(group_labels_[i],labels[i]->c_str());
        titleformat_compiler::get()->compile_safe(group_sort_,pattern.sort_order.c_str());
        titleformat_compiler::get()->compile_safe(tooltip_script_,tooltip_titleformat(core_.tooltip_pattern).c_str());
    }
    void capture_columns() {
        if (!list_ || !header_ || visible_columns_.empty()) return;
        std::vector<int> order(visible_columns_.size());
        if (!ListView_GetColumnOrderArray(list_,static_cast<int>(order.size()),order.data())) return;
        // Reorder only displayed slots. Artwork moved into group headers keeps
        // its enabled state, width and saved position in the shared layout.
        auto slots=visible_columns_;
        std::sort(slots.begin(),slots.end());
        auto result=columns_;
        std::vector<int> column_mapping(columns_.size()), old_mapping(order.size());
        std::iota(column_mapping.begin(),column_mapping.end(),0);
        for(size_t i=0;i<order.size();++i) {
            const int logical=order[i], from=visible_columns_[logical], to=slots[i];
            auto c=columns_[from];
            result[to]=std::move(c);
            column_mapping[from]=to; old_mapping[logical]=to;
        }
        columns_ = std::move(result);
        if (sort_column_>=0 && static_cast<size_t>(sort_column_)<column_mapping.size())
            sort_column_=column_mapping[sort_column_];
        // The old control's logical indices still describe its original insertion order.
        visible_columns_ = std::move(old_mapping);
    }
    void normalize_percents() {
        int total=0; for (const auto& c:columns_) if (c.visible) total+=c.width;
        for (auto& c:columns_) c.percent=c.visible && total ? std::max(1,c.width*10000/total) : 0;
    }
    modern_playlist::rating_cell_metrics rating_dimensions() const {
        LOGFONTW font{}; if(font_) GetObjectW(font_,sizeof(font),&font);
        return modern_playlist::rating_cells(scale(14),row_pixels_,
            std::max(int(std::abs(font.lfHeight)),3*scale(6)),scale(6),scale(1),core_.rating_compact);
    }
    int column_minimum_width(const column& c) const {
        return c.ref=="Rating"?std::max(scale(32),rating_dimensions().minimum_width):scale(32);
    }
    int column_minimum_width(int logical) const {
        return logical>=0 && size_t(logical)<visible_columns_.size()
            ?column_minimum_width(columns_[visible_columns_[logical]]):scale(32);
    }
    void make_columns() {
        cancel_header_drag();
        rebuilding_ = true;
        SendMessageW(list_,WM_SETREDRAW,FALSE,0);
        while (ListView_DeleteColumn(list_,0)) {}
        visible_columns_.clear();
        columns_artwork_in_header_=grouping_.active() && grouping_.artwork_in_header;
        for (size_t i=0;i<columns_.size();++i) if (columns_[i].visible) {
            if(columns_artwork_in_header_ && (columns_[i].ref=="Cover" || columns_[i].ref=="ArtistArt")) continue;
            auto& c = columns_[i]; auto title = wide(c.title.c_str());
            LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.pszText = title.data(); col.cx = std::max(scale(c.width),column_minimum_width(c)); col.fmt = c.align;
            ListView_InsertColumn(list_,static_cast<int>(visible_columns_.size()),&col);
            visible_columns_.push_back(static_cast<int>(i));
        }
        std::vector<int> order(visible_columns_.size()); std::iota(order.begin(),order.end(),0);
        if(!order.empty()) ListView_SetColumnOrderArray(list_,int(order.size()),order.data());
        // DPI/zoom rebuilds can assign new logical indices after a header drag.
        int cover_column=-1, artist_column=-1;
        for(size_t i=0;i<visible_columns_.size();++i) {
            const auto& ref=columns_[visible_columns_[i]].ref;
            if(ref=="Cover") cover_column=int(i);
            else if(ref=="ArtistArt") artist_column=int(i);
        }
        for(auto& group:groups_) {
            group.cover_column=cover_column; group.artist_column=artist_column;
            group.artwork_in_header=grouping_.artwork_in_header;
        }
        modern_playlist::set_playlist_groups(list_,groups_);
        SendMessageW(list_,WM_SETREDRAW,TRUE,0);
        rebuilding_ = false;
    }
    void fit_columns() {
        if (destroying_ || !list_ || !header_ || fitting_columns_ || header_resize_left_>=0 || visible_columns_.empty()) return;
        fitting_columns_ = true;
        const int count=static_cast<int>(visible_columns_.size());
        {
            // Always fit the viewport. If minimum widths cannot fit, clip the
            // excess at the right edge; Rating keeps all five slots.
            std::vector<int> logicals(count), weights(count), widths(count), minimums(count);
            int minimum_total=0;
            for(int i=0;i<count;++i) minimum_total+=column_minimum_width(i);
            for (int pass=0;pass<2;++pass) {
                const int available=std::max(int(SendMessageW(list_,modern_playlist::viewport_content_width,0,0)),minimum_total);
                int remaining=available, remaining_weight=0;
                for (int order=0;order<count;++order) {
                    const int logical=static_cast<int>(SendMessageW(header_,HDM_ORDERTOINDEX,order,0));
                    logicals[order]=logical; minimums[order]=column_minimum_width(logical);
                    const auto& col=columns_[visible_columns_[logical]];
                    weights[order]=std::max(1,col.percent>0?col.percent:col.width);
                    remaining_weight+=weights[order];
                    widths[order]=0;
                }
                // Reserve minimum widths first, then proportionally share the rest.
                bool changed;
                do {
                    changed=false;
                    for (int order=0;order<count;++order) if (!widths[order] &&
                        static_cast<long long>(weights[order])*remaining<static_cast<long long>(minimums[order])*remaining_weight) {
                        widths[order]=minimums[order];
                        remaining-=minimums[order]; remaining_weight-=weights[order]; changed=true;
                    }
                } while (changed && remaining_weight>0);
                int assigned=0; long long cumulative=0;
                for (int order=0;order<count;++order) if (!widths[order]) {
                    cumulative+=weights[order];
                    const int edge=static_cast<int>(cumulative*remaining/remaining_weight);
                    widths[order]=edge-assigned; assigned=edge;
                }
                for (int order=0;order<count;++order) if (ListView_GetColumnWidth(list_,logicals[order])!=widths[order])
                    ListView_SetColumnWidth(list_,logicals[order],widths[order]);
            }
            const int offset = int(SendMessageW(list_,modern_playlist::viewport_horizontal_offset,0,0));
            if (offset) ListView_Scroll(list_,-offset,0);
            fitting_columns_=false;
            InvalidateRect(list_,nullptr,FALSE);
            InvalidateRect(header_,nullptr,FALSE);
            return;
        }
    }
    void ui_fonts_changed() override { if (hwnd_ && !destroying_) { theme(); layout(); invalidate_all(); } }
    void ui_colors_changed() override {
        if (hwnd_ && !destroying_) {
            theme();
            layout();
            invalidate_all();
        }
    }
    void invalidate_all() {
        if (!hwnd_ || destroying_) return;
        InvalidateRect(hwnd_, nullptr, TRUE);
        if (list_) InvalidateRect(list_, nullptr, TRUE);
        if (header_) InvalidateRect(header_, nullptr, TRUE);
        if (scrollbar_) InvalidateRect(scrollbar_, nullptr, FALSE);
        if (tabs_) InvalidateRect(tabs_, nullptr, TRUE);
        if (search_) InvalidateRect(search_, nullptr, TRUE);
        if (notice_) InvalidateRect(notice_, nullptr, TRUE);
        if (add_) InvalidateRect(add_, nullptr, TRUE);
        if (tab_left_) InvalidateRect(tab_left_, nullptr, TRUE);
        if (tab_right_) InvalidateRect(tab_right_, nullptr, TRUE);
        for(auto child:{status_,sort_az_,sort_za_,reveal_active_,search_field_,search_scope_,search_action_,ungrouped_view_,grouped_view_}) if(child) InvalidateRect(child,nullptr,FALSE);
    }
    static COLORREF blend(COLORREF a, COLORREF b, int percent) {
        return RGB((GetRValue(a)*(100-percent)+GetRValue(b)*percent)/100,
                   (GetGValue(a)*(100-percent)+GetGValue(b)*percent)/100,
                   (GetBValue(a)*(100-percent)+GetBValue(b)*percent)/100);
    }
    HFONT copy_ui_font(const GUID& role, bool bold = false) {
        LOGFONTW lf{};
        const HFONT host=callback_.is_valid()?callback_->query_font_ex(role):nullptr;
        if (!host || !GetObjectW(host,sizeof(lf),&lf)) {
            lf.lfHeight=-scale(13); lf.lfWeight=bold?FW_SEMIBOLD:FW_NORMAL;
            lf.lfQuality=CLEARTYPE_QUALITY; lstrcpyW(lf.lfFaceName,L"Microsoft YaHei UI");
        }
        else lf.lfHeight=MulDiv(lf.lfHeight,zoom_percent_,100);
        // Host fonts are borrowed; create our own copy so replacement/destruction is safe.
        return CreateFontIndirectW(&lf);
    }
    // Keep the current font when the new copy describes the same font.
    static HFONT keep_font(HFONT current, HFONT created) {
        LOGFONTW a{}, b{};
        if (!current || !created || GetObjectW(current,sizeof(a),&a)!=sizeof(a) || GetObjectW(created,sizeof(b),&b)!=sizeof(b) ||
            memcmp(&a,&b,offsetof(LOGFONTW,lfFaceName))!=0 || wcsncmp(a.lfFaceName,b.lfFaceName,LF_FACESIZE)!=0) return created;
        DeleteObject(created); return current;
    }
    int font_height(HFONT font) const {
        HDC dc=GetDC(hwnd_); const auto old=SelectObject(dc,font);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc,&metrics);
        SelectObject(dc,old); ReleaseDC(hwnd_,dc);
        return metrics.tmHeight;
    }
    void theme() {
        cancel_header_drag();
        if(drag_tab_>=0) cancel_tab_drag();
        const bool dark=is_dark_mode();
        colors_=dark?palette::dark:palette::light;
        colors_.highlight=colors_.selection;
        if (callback_.is_valid()) {
            colors_.text=callback_->query_std_color(ui_color_text);
            colors_.row=callback_->query_std_color(ui_color_background);
            colors_.surface=colors_.search_bg=colors_.alternate=colors_.row;
            colors_.selection=callback_->query_std_color(ui_color_selection);
            colors_.highlight=callback_->query_std_color(ui_color_highlight);
            colors_.header=blend(colors_.row,colors_.text,8);
            colors_.border=colors_.divider=blend(colors_.row,colors_.text,25);
            colors_.muted=blend(colors_.row,colors_.text,65);
            // Default UI exposes no selected-text role; choose readable contrast.
            const auto c=colors_.selection;
            colors_.selected_text=(299*GetRValue(c)+587*GetGValue(c)+114*GetBValue(c)>=128000)
                ? RGB(0,0,0) : RGB(255,255,255);
        }
        const auto& pal=current_palette();
        const HFONT old_font=font_, old_header=bold_font_, old_tabs=tabs_font_, old_default=default_font_;
        font_=keep_font(old_font,copy_ui_font(ui_font_playlists));
        bold_font_=keep_font(old_header,copy_ui_font(ui_font_playlists,true));
        tabs_font_=keep_font(old_tabs,copy_ui_font(ui_font_tabs));
        tab_text_.configure(tabs_font_);
        default_font_=keep_font(old_default,copy_ui_font(ui_font_default));
        // The automatic height is the roomy modern default. An enabled minimum
        // replaces it, down to compact classic rows that still fit the text.
        const int text_height=font_height(font_);
        if(core_.minimum_row_height_enabled)
            row_pixels_=std::max(scale(int(core_.minimum_row_height)),core_.extra_line?text_height*19/10+scale(2):text_height+scale(2));
        else row_pixels_=core_.extra_line ? std::max(scale(36),text_height*19/10+scale(4)) : std::max(scale(30),text_height+scale(10));
        header_pixels_=std::max(scale(31),font_height(bold_font_)+scale(10));
        tab_pixels_=std::max(scale(30),font_height(tabs_font_)+scale(10));
        text_pixels_=std::max(scale(19),font_height(default_font_));
        // Measure every label, so opening/selecting an item cannot resize its
        // neighbour. Host fonts, panel zoom and DPI all contribute to the width.
        HDC search_dc=GetDC(hwnd_); auto search_font=SelectObject(search_dc,default_font_);
        search_field_pixels_=search_scope_pixels_=0;
        for(const auto& field:modern_playlist::search_fields) { SIZE size{}; GetTextExtentPoint32W(search_dc,field.label,lstrlenW(field.label),&size); search_field_pixels_=std::max(search_field_pixels_,int(size.cx)+scale(30)); }
        for(const auto* label:modern_playlist::search_scopes) { SIZE size{}; GetTextExtentPoint32W(search_dc,label,lstrlenW(label),&size); search_scope_pixels_=std::max(search_scope_pixels_,int(size.cx)+scale(30)); }
        SelectObject(search_dc,search_font); ReleaseDC(hwnd_,search_dc);
        // A combo box re-measures and repaints itself on WM_SETFONT; only
        // controls whose font actually changed receive it.
        if (default_font_!=old_default)
            for (HWND child : {search_,notice_,search_field_,search_scope_,search_action_,ungrouped_view_,grouped_view_,grouping_tooltip_})
                if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(default_font_),TRUE);
        if (tabs_font_!=old_tabs)
            for (HWND child : {tabs_,add_,tab_left_,tab_right_,status_,sort_az_,sort_za_,reveal_active_,rename_edit_})
                if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(tabs_font_),TRUE);
        if (font_!=old_font) SendMessageW(list_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        if (bold_font_!=old_header) SendMessageW(header_,WM_SETFONT,reinterpret_cast<WPARAM>(bold_font_),TRUE);
        for (auto [old,current] : {std::pair{old_font,font_},std::pair{old_header,bold_font_},std::pair{old_tabs,tabs_font_},std::pair{old_default,default_font_}})
            if (old && old!=current) DeleteObject(old);
        if (background_) DeleteObject(background_);
        if (edit_background_) DeleteObject(edit_background_);
        background_=CreateSolidBrush(pal.surface);
        edit_background_=CreateSolidBrush(pal.search_bg);
        modern_playlist::viewport_style style{row_pixels_,header_pixels_,scale(6),
            pal.row,blend(pal.row,pal.text,4),pal.text,pal.selection,pal.selected_text,pal.highlight};
        style.show_scrollbar=show_scrollbar_; style.scrollbar_dpi=scale(96);
        style.header_height=show_header_?header_pixels_:0;
        ShowWindow(header_,show_header_?SW_SHOWNA:SW_HIDE);
        style.alternating=core_.alternating; style.group_parity=core_.group_parity; style.extra_line=core_.extra_line;
        style.derived_extra_color=core_.derived_extra_color; style.secondary=pal.muted;
        style.selection_alpha=core_.selection_alpha; style.focus_alpha=core_.focus_alpha;
        style.tooltips=core_.tooltips; style.selected_tooltips=core_.selected_tooltips; style.tooltip_delay=core_.tooltip_delay;
        style.tooltip_alpha=core_.tooltip_alpha;
        style.enqueue_default=core_.enqueue_on_double_click;
        style.rating_dots=core_.rating_dots;
        const auto rating=rating_dimensions();
        style.rating_pitch=rating.pitch; style.rating_shadow_offset=float(scale(100))/200; // Half a logical pixel.
        style.mood_icon_size=scale(15); style.rating_icon_size=rating.icon_size; style.rating_dot_size=std::max(1,scale(2));
        style.state_check_size=scale(18); style.state_play_size=scale(17);
        style.artwork=artwork_; style.cover_margin=scale(int(artwork_.margin));
        style.group_header_rows=grouping_.header_rows; style.group_fonts=grouping_.fonts;
        forget_parent_capture(); clear_surface(); // Host color changes can repaint the parent too.
        modern_playlist::configure_playlist_viewport(list_,style);
        if(close_font_) DeleteObject(close_font_);
        close_font_=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");
        update_search_controls(); update_search_visuals();
        measure_tabs();
        // Segoe Fluent Icons is absent on older Windows; MDL2 has the same close glyph.
        HDC icon_dc=GetDC(hwnd_); auto old_icon=SelectObject(icon_dc,close_font_); WORD glyph=0;
        const DWORD glyph_result=GetGlyphIndicesW(icon_dc,L"\uE711",1,&glyph,GGI_MARK_NONEXISTING_GLYPHS);
        SelectObject(icon_dc,old_icon); ReleaseDC(hwnd_,icon_dc);
        if(glyph_result==GDI_ERROR || glyph==0xffff) {
            DeleteObject(close_font_);
            close_font_=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe MDL2 Assets");
        }
        if(themed_dark_!=int(dark)) {
            themed_dark_=int(dark);
            SetWindowTheme(list_,dark?L"DarkMode_Explorer":L"Explorer",nullptr);
            SetWindowTheme(search_,dark?L"DarkMode_CFD":nullptr,nullptr);
            if(rename_edit_) SetWindowTheme(rename_edit_,dark?L"DarkMode_CFD":nullptr,nullptr);
            SetWindowTheme(header_,L"",L"");
        }
    }
    bool tab_has_close(int index,t_size playing) const {
        return !core_.hide_tab_close && t_size(index)!=playing && modern_playlist::can_close_playlist(index);
    }
    // Icons sit right to left: the speaker or close button, then the lock next
    // to the name. A locked playlist cannot be closed, so at most two appear.
    int tab_icon_count(int index,t_size playing) const {
        return int(t_size(index)==playing || tab_has_close(index,playing))+int(modern_playlist::user_locked(index));
    }
    // Compact Excel-style geometry. Name-only tabs keep 8px on both sides;
    // icons tighten the padding around the name and keep 2px between them.
    int tab_chrome_width(int index,t_size playing) const {
        const int icons=tab_icon_count(index,playing);
        return icons?scale(6)+icons*(scale(2)+scale(14))+scale(4):2*scale(8);
    }
    RECT tab_icon_rect(const RECT& tab,int slot=0) const {
        RECT icon=tab; icon.right-=scale(4)+slot*(scale(14)+scale(2)); icon.left=icon.right-scale(14); return icon;
    }
    RECT tab_label_rect(const RECT& tab,int icons) const {
        RECT label=tab; label.left+=scale(icons?6:8);
        label.right=icons?tab_icon_rect(tab,icons-1).left-scale(2):tab.right-scale(8);
        return label;
    }
    // The free edge of every tab leaves a strip-colored gap; the active sheet
    // reaches the playlist across the joined edge.
    int tab_gap() const { return scale(3); }
    RECT tab_content_rect(const RECT& tab) const {
        RECT content=tab;
        if(manager_bottom_) content.bottom-=tab_gap(); else content.top+=tab_gap();
        return content;
    }
    int tab_text_width(HDC dc,const std::wstring& text) {
        const int width=tab_text_.measure(text);
        if(width>=0) return width;
        SIZE size{}; GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);
        return int(size.cx);
    }
    void draw_tab_text(HDC dc,const std::wstring& text,const RECT& rect,COLORREF color,bool center=false) {
        if(tab_text_.draw(dc,text,rect,color,core_.tab_color_emoji,center)) return;
        SetTextColor(dc,color); RECT label=rect;
        DrawTextW(dc,text.c_str(),int(text.size()),&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(center?DT_CENTER:DT_LEFT));
    }
    void measure_tabs() {
        const auto playing=playing_playlist(); measured_playing_=playing;
        std::vector<int> widths; widths.reserve(tab_names_.size());
        HDC dc=GetDC(hwnd_); auto old=SelectObject(dc,tabs_font_);
        for(size_t i=0;i<tab_names_.size();++i) {
            const auto& name=tab_names_[i];
            const int text_width=tab_text_width(dc,name);
            // No minimum tab width: reserve only text, padding and a visible icon.
            widths.push_back(std::min(text_width+tab_chrome_width(int(i),playing),scale(240)));
        }
        SelectObject(dc,old); ReleaseDC(hwnd_,dc);
        if(widths!=manager_.widths) {
            if(drag_tab_>=0) cancel_tab_drag();
            manager_.widths=std::move(widths);
        }
        manager_.clamp();
    }
    bool tab_rect(int index,RECT* rect) const {
        if(index<0 || size_t(index)>=manager_.widths.size()) return false;
        const int x=manager_.left(index); *rect={x,0,x+manager_.widths[index],tab_pixels_}; return true;
    }
    int tab_hit(POINT pt) const { return pt.y>=0 && pt.y<tab_pixels_?manager_.hit(pt.x):-1; }
    bool can_scroll_left() const { return manager_.offset>0; }
    bool can_scroll_right() const { return manager_.offset+manager_.viewport<manager_.total(); }
    void update_arrows() const {
        for(auto button:{tab_left_,tab_right_}) if(button) {
            const bool enabled=button==tab_left_?can_scroll_left():can_scroll_right();
            if((IsWindowEnabled(button)!=FALSE)!=enabled) EnableWindow(button,enabled);
            InvalidateRect(button,nullptr,FALSE);
        }
    }
    void scroll_tabs(int delta) {
        if(!show_tabs_) return;
        manager_.offset+=delta*scale(120); manager_.clamp();
        InvalidateRect(tabs_,nullptr,FALSE); update_arrows();
    }
    void on_tabs_wheel(short delta) {
        if (!show_tabs_ || !tabs_) return;
        tab_wheel_accumulator_ += delta;
        int steps = tab_wheel_accumulator_ / WHEEL_DELTA;
        tab_wheel_accumulator_ %= WHEEL_DELTA;
        if (steps != 0) {
            scroll_tabs(-steps);
        }
    }
    void layout() {
        RECT r{}; GetClientRect(hwnd_,&r);
        const int width=std::max(0,int(r.right)), height=std::max(0,int(r.bottom));
        // Leave room for the sheet flares (draw_sheet_tab's 3px radius) at both
        // ends, so the first and last tabs keep complete, symmetric corners.
        manager_.padding=scale(3);
        const int status_height=show_status_?std::min(height,tab_pixels_):0;
        const int strip=show_tabs_?std::min(height-status_height,tab_pixels_):0;
        const int manager_y=manager_bottom_?height-status_height-strip:0;
        const int search_y=manager_bottom_?0:strip;
        const int action_w=std::max(scale(34),tab_pixels_);
        const int add_w=std::min(width,action_w), gap=scale(4), arrow_w=scale(22);
        const bool overflow=manager_.total()+gap+add_w>width;
        const int reveal_w=overflow?std::min(action_w,std::max(0,width-add_w)):0;
        const int arrows=overflow?std::min(arrow_w,std::max(0,(width-add_w-reveal_w-3*gap)/2)):0;
        const int tabs_x=arrows?arrows+gap:0;
        const int available=std::max(0,width-add_w-reveal_w-gap-tabs_x-(arrows?arrows+gap:0));
        manager_.viewport=std::min(manager_.total(),available); manager_.clamp();
        // All children have disjoint rectangles, even when the splitter is narrower than +.
        const int add_x=overflow?width-add_w-reveal_w:std::min(width-add_w,tabs_x+manager_.viewport+gap);
        auto position=[](HWND child,int x,int y,int w,int h,bool visible) {
            SetWindowPos(child,nullptr,x,y,std::max(0,w),std::max(0,h),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        };
        const int tabs_height=std::min(strip,tab_pixels_);
        position(tabs_,tabs_x,manager_y,manager_.viewport,tabs_height,show_tabs_);
        position(add_,add_x,manager_y,add_w,tabs_height,show_tabs_);
        position(reveal_active_,add_x+add_w,manager_y,reveal_w,tabs_height,show_tabs_ && overflow && reveal_w>0);
        position(tab_left_,0,manager_y,arrows,tabs_height,show_tabs_ && arrows>0);
        position(tab_right_,std::max(0,add_x-gap-arrows),manager_y,arrows,tabs_height,show_tabs_ && arrows>0);
        const auto status_layout=modern_playlist::manager_status_layout(width,scale(48));
        const int status_y=height-status_height;
        position(status_,0,status_y,status_layout.count_width,status_height,show_status_ && status_height>0);
        position(sort_az_,status_layout.count_width,status_y,status_layout.button_width,status_height,show_status_ && status_height>0);
        position(sort_za_,status_layout.count_width+status_layout.button_width,status_y,status_layout.button_width,status_height,show_status_ && status_height>0);
        update_arrows();
        // At zero width (hidden tabs / Quick Setup), GetWindowRect can retain
        // the requested dropdown height. The selection field keeps its native
        // vertical inset; mirror it below the field to measure only the closed face.
        COMBOBOXINFO selector{sizeof(selector)};
        const int control_height=GetComboBoxInfo(search_field_,&selector)
            ? std::max(text_pixels_,int(selector.rcItem.top+selector.rcItem.bottom))
            : text_pixels_+scale(6);
        const int row_height=std::max(text_pixels_+scale(10),control_height+2*scale(2));
        const int content_bottom=height-status_height-(manager_bottom_?strip:0);
        const bool search_visible=search_settings_.visible && content_bottom-search_y>=row_height;
        const bool joined=show_tabs_ && !manager_bottom_ && !search_visible && notice_text_.empty() && show_header_;
        if(joined!=tabs_join_header_) {
            tabs_join_header_=joined;
            for(HWND child:{tabs_,add_,reveal_active_,tab_left_,tab_right_}) if(child) InvalidateRect(child,nullptr,FALSE);
        }
        const int spacing=std::min(scale(6),width/12);
        const int usable=std::max(0,width-6*spacing);
        const int button_width=std::min(control_height,usable/8);
        const int fields_space=usable-2*button_width;
        const int field_width=std::min(std::max(scale(128),search_field_pixels_),fields_space/3);
        const int scope_width=std::min(std::max(scale(150),search_scope_pixels_),fields_space/3);
        const int edit_width=fields_space-field_width-scope_width;
        const int edit_x=spacing, field_x=edit_x+edit_width+spacing;
        const int scope_x=field_x+field_width+spacing;
        const int ungrouped_x=scope_x+scope_width+spacing, grouped_x=ungrouped_x+button_width+spacing;
        const int control_y=search_y+(row_height-control_height)/2;
        search_row_rect_=search_visible?RECT{0,search_y,width,search_y+row_height}:RECT{};
        search_edit_face_=search_visible?RECT{edit_x,control_y,edit_x+edit_width,control_y+control_height}:RECT{};
        // Paint the full edit face in the parent, retaining a centered native
        // text/caret/IME area inside it. All five visible faces have equal height.
        const int action_width=std::min(control_height,edit_width);
        position(search_action_,edit_x,control_y,action_width,control_height,search_visible && action_width>0);
        position(search_,edit_x+action_width,control_y+(control_height-text_pixels_)/2,edit_width-action_width,text_pixels_,search_visible && edit_width>action_width);
        position(search_field_,field_x,control_y,field_width,row_height+(text_pixels_+scale(6))*modern_playlist::search_field_count,search_visible && field_width>0);
        position(search_scope_,scope_x,control_y,scope_width,row_height+scale(120),search_visible && scope_width>0);
        position(ungrouped_view_,ungrouped_x,control_y,button_width,control_height,search_visible && button_width>0);
        position(grouped_view_,grouped_x,control_y,button_width,control_height,search_visible && button_width>0);
        SendMessageW(search_field_,CB_SETDROPPEDWIDTH,field_width,0);
        SendMessageW(search_scope_,CB_SETDROPPEDWIDTH,scope_width,0);
        int y=search_y+(search_visible?row_height:0);
        const int notice_height=std::min(text_pixels_+scale(7),std::max(0,content_bottom-y));
        position(notice_,0,y,width,notice_height,!notice_text_.empty() && notice_height>0);
        if(!notice_text_.empty()) y+=notice_height;
        position(list_,0,y,width,content_bottom-y,true);
        position_rename(); // Follows the strip's size, scrolling and visibility.
        fit_columns(); InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void notice(const std::wstring& text) {
        if (notice_text_==text) return;
        notice_text_=text; SetWindowTextW(notice_,text.c_str()); layout();
    }
    void toggle_tabs() {
        cancel_tab_drag(); end_rename(true);
        show_tabs_=!show_tabs_;
        if(!show_tabs_ && (GetFocus()==tabs_ || GetFocus()==add_ || GetFocus()==tab_left_ || GetFocus()==tab_right_ || GetFocus()==reveal_active_)) SetFocus(list_);
        layout();
    }
    void toggle_status() {
        show_status_=!show_status_;
        if(!show_status_ && (GetFocus()==sort_az_ || GetFocus()==sort_za_)) SetFocus(list_);
        layout(); invalidate_all();
    }
    void toggle_header() { show_header_=!show_header_; theme(); layout(); invalidate_all(); }
    COLORREF hover_background() const noexcept {
        const auto& pal=current_palette();
        return RGB((3*GetRValue(pal.header)+GetRValue(pal.selection))/4,
                   (3*GetGValue(pal.header)+GetGValue(pal.selection))/4,
                   (3*GetBValue(pal.header)+GetBValue(pal.selection))/4);
    }
    // The active sheet normally continues the rows (or search row) below it.
    // With the manager on top and no search row it rests on the column header
    // instead, so swap the sheet and strip colors: the active tab continues the
    // header rather than floating on it as a row-colored island.
    COLORREF tab_strip_color() const noexcept {
        const auto& pal=current_palette();
        return tabs_join_header_?pal.row:blend(pal.row,pal.text,8);
    }
    COLORREF active_tab_color() const noexcept {
        const auto& pal=current_palette();
        return tabs_join_header_?pal.header:pal.row;
    }
    // Button icons (+, ◎, the strip arrows, view toggles, sort arrows and
    // search/clear) blend the text color into their face, so they do not stand
    // out in light themes. Disabled icons fade further.
    COLORREF icon_color(COLORREF face,bool enabled=true) const noexcept {
        return blend(face,current_palette().text,enabled?65:30);
    }
    // Glyph strokes match the scrollbar arrows (SM_CXVSCROLL/8, rounded down):
    // 2px at 100% and 125%, 3px at 150%, 4px at 200% DPI/zoom.
    int icon_stroke() { return std::max(1,scale(17)/8); }
    bool is_manager_button(HWND control) const noexcept {
        return control && (control==add_ || control==reveal_active_ || control==tab_left_ ||
            control==tab_right_ || control==sort_az_ || control==sort_za_);
    }
    void draw_manager_button(const DRAWITEMSTRUCT& draw) {
        update_hover();
        const auto& pal=current_palette();
        const bool enabled=IsWindowEnabled(draw.hwndItem)!=FALSE;
        const bool pressed=enabled && (draw.itemState&ODS_SELECTED)!=0;
        const bool hovered=enabled && hovered_manager_button_==draw.hwndItem;
        const bool sort=draw.hwndItem==sort_az_ || draw.hwndItem==sort_za_;
        const auto base=sort?pal.surface:tab_strip_color();
        const int saved=SaveDC(draw.hDC);
        const RECT rect=draw.rcItem;
        IntersectClipRect(draw.hDC,rect.left,rect.top,rect.right,rect.bottom);
        fill(draw.hDC,rect,base);
        const bool artwork=paint_artwork_surface(draw.hDC,draw.hwndItem,rect);
        if(artwork && !sort) modern_playlist::tint_artwork_gdi(draw.hDC,rect,pal.text,20);
        if(pressed || hovered) {
            if(artwork) modern_playlist::tint_artwork_gdi(draw.hDC,rect,pal.selection,80);
            else fill(draw.hDC,rect,pressed?pal.selection:hover_background());
        }
        const bool arrow=draw.hwndItem==tab_left_ || draw.hwndItem==tab_right_;
        // A button is disabled at its end of the strip, or with nothing to sort.
        // Dim it clearly, so its missing hover feedback reads as unavailable.
        const auto ink=pressed?pal.selected_text:icon_color(base,enabled);
        const int width=rect.right-rect.left, height=rect.bottom-rect.top;
        const int cx=(rect.left+rect.right)/2, cy=(rect.top+rect.bottom)/2;
        if(arrow) {
            const int radius=std::min(scale(4),std::max(0,(std::min(width,height)-scale(4))/2));
            if(radius>0) modern_playlist::draw_smooth_chevron(draw.hDC,cx,cy,radius,
                icon_stroke(),draw.hwndItem==tab_right_,ink,true);
        } else {
            // + / ◎ and the ↑ / ↓ sort arrows share one size and bar weight.
            int size=std::min(std::max(scale(16),tab_pixels_/2),std::min(width,height)-2*scale(5));
            // The view toggles' bar weight, from the same nominal 16px icon. Equal
            // size/stroke parity keeps the bars on whole pixels and centered.
            const int stroke=modern_playlist::glyph_stroke(std::min(size,scale(16)));
            if((size-stroke)%2) --size;
            if(size>0) {
                const auto pixels=sort?modern_playlist::sort_arrow_icon(draw.hwndItem==sort_za_,unsigned(size),ink,unsigned(stroke)):
                    modern_playlist::manager_action_icon(draw.hwndItem==reveal_active_,unsigned(size),ink,unsigned(stroke));
                modern_playlist::draw_artwork_gdi(draw.hDC,*pixels,cx-size/2,cy-size/2,size,size);
            }
        }
        if(enabled && (draw.itemState&ODS_FOCUS) && !(draw.itemState&ODS_NOFOCUSRECT) && mouse_focused_manager_!=draw.hwndItem) {
            RECT cue{rect.left+scale(6),rect.bottom-scale(2),rect.right-scale(6),rect.bottom-scale(1)};
            if(cue.right>cue.left && cue.top>=rect.top) fill(draw.hDC,cue,blend(base,pal.text,45));
        }
        RestoreDC(draw.hDC,saved);
    }
    void update_hover() {
        if (destroying_ || !hwnd_) return;
        POINT pt{};
        GetCursorPos(&pt);
        HWND target = WindowFromPoint(pt);
        int tab=-1;
        if (target==tabs_ && IsWindowEnabled(tabs_)) {
            TCHITTESTINFO hit{}; hit.pt=pt;
            ScreenToClient(tabs_,&hit.pt);
            tab=tab_hit(hit.pt);
        }
        if (tab!=hovered_tab_) {
            hovered_tab_=tab;
            if (tabs_) InvalidateRect(tabs_,nullptr,FALSE);
        }
        const HWND button=is_manager_button(target) && IsWindowEnabled(target)?target:nullptr;
        if(button!=hovered_manager_button_) {
            const HWND previous=hovered_manager_button_;
            hovered_manager_button_=button;
            if(previous) InvalidateRect(previous,nullptr,FALSE);
            if(button) InvalidateRect(button,nullptr,FALSE);
        }
    }
    // Until the panel and its children first paint, their area shows the host
    // window's blank surface, which is white. In a dark theme that flashes while
    // startup work delays the first WM_PAINT. Windows erases a newly shown panel
    // synchronously, before any paint, so the first visible erase fills the whole
    // panel, beneath the children too, with the background color. Later erases
    // paint nothing, so ordinary repaints do not flicker.
    void fill_startup_background() {
        RECT r{}; GetClientRect(hwnd_,&r);
        if(IsRectEmpty(&r) || !IsWindowVisible(hwnd_)) return;
        startup_filled_=true;
        if(HDC dc=GetDCEx(hwnd_,nullptr,DCX_CACHE|DCX_CLIPSIBLINGS)) {
            const auto& pal=current_palette(); fill(dc,r,pal.row);
            if(!IsRectEmpty(&search_row_rect_)) fill(dc,search_row_rect_,pal.search_bg);
            ReleaseDC(hwnd_,dc);
        }
        // A child that already painted repaints over the fill.
        EnumChildWindows(hwnd_,[](HWND child,LPARAM) -> BOOL { InvalidateRect(child,nullptr,FALSE); return TRUE; },0);
    }
    void paint_frame() {
        const auto& pal=current_palette(); PAINTSTRUCT ps{}; const HDC paint=BeginPaint(hwnd_,&ps);
        // Compose off screen and copy once. Painted on screen, the plain fills
        // showed until the background image covered them, and composing a new
        // image took long enough for the strip and search row to flicker.
        const RECT dirty=ps.rcPaint; const int width=dirty.right-dirty.left, height=dirty.bottom-dirty.top;
        HDC buffer=width>0 && height>0?CreateCompatibleDC(paint):nullptr;
        HBITMAP bitmap=buffer?CreateCompatibleBitmap(paint,width,height):nullptr;
        HGDIOBJ old_bitmap=nullptr;
        HDC dc=paint;
        if(buffer && bitmap) { old_bitmap=SelectObject(buffer,bitmap); SetViewportOrgEx(buffer,-dirty.left,-dirty.top,nullptr); dc=buffer; }
        RECT r{}; GetClientRect(hwnd_,&r); fill(dc,r,pal.surface);
        if(show_tabs_ && tabs_) {
            RECT strip{}; GetWindowRect(tabs_,&strip);
            MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
            strip.left=0; strip.right=r.right;
            fill(dc,strip,tab_strip_color());
        }
        if(!IsRectEmpty(&search_row_rect_)) fill(dc,search_row_rect_,pal.search_bg);
        if(paint_artwork_surface(dc,hwnd_,r) && show_tabs_ && tabs_) {
            RECT strip{}; GetWindowRect(tabs_,&strip);
            MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
            strip.left=0; strip.right=r.right;
            modern_playlist::tint_artwork_gdi(dc,strip,pal.text,20);
        }
        if(!IsRectEmpty(&search_edit_face_)) paint_search_face(dc,hwnd_,search_edit_face_);
        if(dc==buffer) { BitBlt(paint,dirty.left,dirty.top,width,height,buffer,dirty.left,dirty.top,SRCCOPY); SelectObject(buffer,old_bitmap); }
        if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
        EndPaint(hwnd_,&ps);
    }
    void paint_search_face(HDC dc,HWND control,RECT rect) {
        const auto& pal=current_palette(); fill(dc,rect,pal.search_bg);
        if(paint_artwork_surface(dc,control,rect)) modern_playlist::tint_artwork_gdi(dc,rect,pal.search_bg,72);
    }
    void draw_search_selector(HDC dc,HWND control,RECT rect) {
        const int saved=SaveDC(dc); IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
        paint_search_face(dc,control,rect);
        const auto& pal=current_palette();
        // Translucent like the view toggles (12% or 7% of the text color).
        if(control==hovered_search_selector_ || control==open_search_selector_)
            modern_playlist::tint_artwork_gdi(dc,rect,pal.text,control==open_search_selector_?31:18);
        const bool field=control==search_field_;
        const auto text=field?modern_playlist::search_fields[search_settings_.field].label:modern_playlist::search_scopes[search_settings_.scope];
        SelectObject(dc,default_font_); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,current_palette().text);
        RECT label=rect; label.left+=scale(6); label.right-=scale(18);
        DrawTextW(dc,text,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        const int x=rect.right-scale(10),y=(rect.top+rect.bottom)/2,r=std::max(2,scale(4));
        // Same weight as the scrollbar arrows and icon_stroke().
        modern_playlist::draw_smooth_chevron(dc,x,y,r,std::max(1,scale(17)/8),true,current_palette().muted);
        // Keyboard navigation retains a subtle cue; mouse clicks never leave a
        // dashed focus rectangle behind after the popup closes.
        if(GetFocus()==control && !(SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS) && !search_selector_mouse_) {
            RECT cue{rect.left+scale(6),rect.bottom-scale(2),rect.right-scale(6),rect.bottom-scale(1)};
            fill(dc,cue,current_palette().muted);
        }
        RestoreDC(dc,saved);
    }
    void paint_search_popup_frame(HWND control) {
        HDC dc=GetWindowDC(control); if(!dc) return;
        const int saved=SaveDC(dc);
        RECT window{},client{}; GetWindowRect(control,&window); GetClientRect(control,&client);
        MapWindowPoints(control,nullptr,reinterpret_cast<POINT*>(&client),2);
        OffsetRect(&client,-window.left,-window.top); OffsetRect(&window,-window.left,-window.top);
        ExcludeClipRect(dc,client.left,client.top,client.right,client.bottom);
        fill(dc,window,blend(search_popup_color(),current_palette().text,16));
        RestoreDC(dc,saved); ReleaseDC(control,dc);
    }
    void paint_search_selector(HWND control) {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(control,&ps); RECT rect{}; GetClientRect(control,&rect);
        draw_search_selector(dc,control,rect); EndPaint(control,&ps);
    }
    COLORREF search_popup_color() const {
        const auto& pal=current_palette();
        return blend(pal.search_bg,pal.text,12); // Same fill as the open selector.
    }
    void position_search_popup(HWND popup,WINDOWPOS& pos) {
        if((pos.flags&SWP_HIDEWINDOW) || (!(pos.flags&SWP_SHOWWINDOW) && !IsWindowVisible(popup))) return;
        const HWND selector=popup==search_field_list_?search_field_:search_scope_;
        RECT face{},previous{};
        if(!GetWindowRect(selector,&face) || !GetWindowRect(popup,&previous)) return;
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromRect(&face,MONITOR_DEFAULTTONEAREST),&monitor)) return;
        const auto& work=monitor.rcWork;
        const int gap=scale(4);
        const int below=std::max(0L,work.bottom-face.bottom-gap), above=std::max(0L,face.top-work.top-gap);
        const int requested=(pos.flags&SWP_NOSIZE)?previous.bottom-previous.top:pos.cy;
        const int proposed_y=(pos.flags&SWP_NOMOVE)?previous.top:pos.y;
        bool upwards=proposed_y<face.top;
        if(requested>(upwards?above:below) && (upwards?below:above)>(upwards?above:below)) upwards=!upwards;
        const int height=std::min(requested,upwards?above:below);
        const int width=std::min(face.right-face.left,work.right-work.left);
        if(width<=0 || height<=0) return;
        pos.x=std::clamp(face.left,work.left,work.right-width);
        pos.y=upwards?face.top-gap-height:face.bottom+gap;
        pos.cx=width; pos.cy=height; pos.flags&=~(SWP_NOMOVE|SWP_NOSIZE);
    }
    void update_grouping_controls() {
        const bool grouped=grouping_.active();
        // The native button erases its label area on every WM_SETTEXT, even for
        // the same text, and every refresh() (e.g. a rating edit) reaches here.
        // The owner-drawn icon then stays blank until the refresh returns and
        // WM_PAINT runs, so only write the accessible name when it changes.
        auto label=[](HWND button,const wchar_t* text) {
            if(window_text(button)==text) return;
            SetWindowTextW(button,text); InvalidateRect(button,nullptr,FALSE);
        };
        if(ungrouped_view_) label(ungrouped_view_,grouped?L"不分组视图":L"不分组视图（活动）");
        if(grouped_view_) {
            EnableWindow(grouped_view_,std::any_of(grouping_.patterns.begin(),grouping_.patterns.end(),
                [](const auto& pattern){return pattern.show_headers;}));
            label(grouped_view_,grouped?L"分组视图（活动）":L"分组视图");
        }
    }
    void set_grouped_view(bool grouped) {
        if(pending_) refresh(false);
        if(grouped?grouping_.active():!grouping_.enabled) return;
        if(grouped && !grouping_.patterns[grouping_.pattern].show_headers) {
            const auto found=std::find_if(grouping_.patterns.begin(),grouping_.patterns.end(),
                [](const auto& pattern){return pattern.show_headers;});
            if(found==grouping_.patterns.end()) return;
            grouping_.pattern=unsigned(found-grouping_.patterns.begin());
            collapsed_.clear(); compile_columns();
        }
        grouping_.enabled=grouped;
        // This explicit view choice lasts until the next playlist-filter
        // evaluation. Changing the view alone must not reorder playlist items.
        apply_filter_next_=false; search_reveal_=pfc::infinite_size;
        refresh(false);
    }
    void draw_grouping_button(const DRAWITEMSTRUCT& draw) {
        const int saved=SaveDC(draw.hDC);
        const auto& rect=draw.rcItem; const auto& pal=current_palette();
        IntersectClipRect(draw.hDC,rect.left,rect.top,rect.right,rect.bottom);
        paint_search_face(draw.hDC,draw.hwndItem,rect);
        const bool grouped=draw.hwndItem==grouped_view_, active=grouped==grouping_.active();
        const bool enabled=IsWindowEnabled(draw.hwndItem)!=FALSE;
        const bool pressed=(draw.itemState&ODS_SELECTED)!=0;
        // A translucent tint (18%, 12% or 7% of the text color), so a
        // background image or pseudo transparency shows through the face.
        if(enabled && (active || pressed || hovered_grouping_button_==draw.hwndItem))
            modern_playlist::tint_artwork_gdi(draw.hDC,rect,pal.text,pressed?46:active?31:18);
        const int size=std::max(1,std::min({scale(16),int(rect.right-rect.left)-scale(6),int(rect.bottom-rect.top)-scale(6)}));
        const int x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;
        const auto ink=icon_color(pal.search_bg,enabled);
        const int stroke=modern_playlist::glyph_stroke(size);
        auto bar=[&](int left,int top,int right) {
            const int y0=y+MulDiv(top,size-stroke,14);
            const RECT r{x+MulDiv(left,size,16),y0,x+MulDiv(right,size,16),y0+stroke};
            fill(draw.hDC,r,ink);
        };
        if(grouped) {
            bar(0,0,16); bar(4,4,16); bar(0,10,16); bar(4,14,16);
        } else for(int top:{0,7,14}) { bar(0,top,3); bar(6,top,16); }
        if(enabled && (draw.itemState&ODS_FOCUS) && !(draw.itemState&ODS_NOFOCUSRECT) && mouse_focused_grouping_!=draw.hwndItem) {
            RECT cue{rect.left+scale(4),rect.bottom-scale(2),rect.right-scale(4),rect.bottom-scale(1)};
            if(cue.right>cue.left) fill(draw.hDC,cue,pal.muted);
        }
        RestoreDC(draw.hDC,saved);
    }
    LRESULT draw_header(NMCUSTOMDRAW* draw) {
        const auto& pal = current_palette();
        const bool image_background=artwork_.enabled && artwork_.source!=3;
        const auto divider=image_background?blend(pal.row,pal.text,10):pal.divider;
        if (draw->dwDrawStage==CDDS_PREPAINT) {
            RECT r{}; GetClientRect(header_,&r); fill(draw->hdc,r,pal.header); paint_artwork_surface(draw->hdc,header_,r);
            return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
        }
        if (draw->dwDrawStage==CDDS_POSTPAINT) {
            // Native headers paint a raised trailing face after the last item.
            // Erase it before drawing dividers across the full header.
            HWND header=header_; RECT tail{}; GetClientRect(header,&tail);
            int end=0;
            for (int i=0;i<Header_GetItemCount(header);++i) { RECT item{}; Header_GetItemRect(header,i,&item); end=std::max(end,int(item.right)); }
            tail.left=end; if(tail.left<tail.right) { fill(draw->hdc,tail,pal.header); paint_artwork_surface(draw->hdc,header_,tail); }
            // Native item rectangles can be inset from the client top. Paint
            // the dividers last, after artwork and the trailing face.
            RECT bounds{}; GetClientRect(header_,&bounds);
            const int stroke=1; // 线宽固定为 1 物理像素
            // 竖分隔线比标题文字略高，但不占满整个列标题高度
            TEXTMETRICW text_metrics{};
            {
                const int measured=SaveDC(draw->hdc); SelectObject(draw->hdc,bold_font_);
                GetTextMetricsW(draw->hdc,&text_metrics); RestoreDC(draw->hdc,measured);
            }
            const int header_height_px=bounds.bottom-bounds.top;
            int edge_height=text_metrics.tmHeight+2*scale(2);
            edge_height=std::min(edge_height,header_height_px-2*scale(2));
            edge_height=std::max(edge_height,text_metrics.tmHeight);
            const int edge_top=bounds.top+(header_height_px-edge_height)/2;
            const int edge_bottom=edge_top+edge_height;
            for(int i=0;i<Header_GetItemCount(header_);++i) {
                RECT item{}; Header_GetItemRect(header_,i,&item);
                if(item.right>bounds.left && item.right<bounds.right) {
                    RECT edge{item.right-stroke,edge_top,item.right,edge_bottom}; fill(draw->hdc,edge,divider);
                }
            }
            RECT separator{bounds.left,std::max(bounds.top,bounds.bottom-stroke),bounds.right,bounds.bottom};
            fill(draw->hdc,separator,divider);
            if(artwork_.enabled) {
                RECT top{bounds.left,bounds.top,bounds.right,std::min(bounds.bottom,bounds.top+stroke)};
                fill(draw->hdc,top,divider);
            }
            if(header_ghost_ && header_before_>=0) {
                int x=0; const int count=Header_GetItemCount(header_);
                if(header_before_<count) {
                    RECT item{}; Header_GetItemRect(header_,Header_OrderToIndex(header_,header_before_),&item); x=item.left;
                } else x=end;
                RECT marker{x-scale(1),0,x+scale(2),header_pixels_}; fill(draw->hdc,marker,pal.selection);
            }
            return CDRF_DODEFAULT;
        }
        if (draw->dwDrawStage!=CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
        int logical=static_cast<int>(draw->dwItemSpec);
        if (logical<0 || static_cast<size_t>(logical)>=visible_columns_.size()) return CDRF_SKIPDEFAULT;
        HDC dc=draw->hdc; int saved=SaveDC(dc); RECT r=draw->rc;
        fill(dc,r,pal.header); paint_artwork_surface(dc,header_,r);
        SelectObject(dc,bold_font_); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,pal.text);
        int col=visible_columns_[logical]; auto title=wide(columns_[col].title.c_str());
        r.left+=scale(6); r.right-=scale(6);
        const int alignment=headers_follow_alignment_?columns_[col].align:LVCFMT_CENTER;
        const UINT flags=alignment==LVCFMT_RIGHT?DT_RIGHT:alignment==LVCFMT_LEFT?DT_LEFT:DT_CENTER;
        RECT label=r;
        DrawTextW(dc,title.c_str(),static_cast<int>(title.size()),&label,flags|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (sort_column_==col) {
            const int cx=(r.left+r.right)/2, cy=r.top+scale(3), radius=std::max(2,scale(3));
            draw_chevron(dc,cx,cy,radius,std::max(1,scale(1)),sort_direction_<0,pal.text);
        }
        RestoreDC(dc,saved); return CDRF_SKIPDEFAULT;
    }
    void format_cell(size_t row, const column& col, pfc::string_base& text) {
        if (col.ref=="Cover" || col.ref=="ArtistArt") return;
        if (col.ref=="Index") { text=std::to_string(rows_[row]+1).c_str(); return; }
        if (col.state) {
            if (row>=row_data_.size()) return;
            const auto& state=row_data_[row];
            if (state.playing) text=state.paused?"已暂停":"正在播放";
            if (!state.queue_positions.empty()) {
                if (text.length()) text << "; "; text << "队列：";
                text << utf8(modern_playlist::queue_position_text(state.queue_positions)).c_str();
            }
            return;
        }
        modern_playlist::column_format_hook hook(rows_[row],items_.get_count(),row<row_data_.size() && row_data_[row].playing);
        // The playlist API associates dynamic info with the playing occurrence,
        // including stream title changes and custom playback-related fields.
        if (!pending_ && active_ != pfc::infinite_size) {
            playlist_manager::get()->playlist_item_format_title(active_,rows_[row],
                &hook,text,col.script,nullptr,play_control::display_level_all);
        } else {
            // A queued structural refresh can leave row indices temporarily stale.
            items_[rows_[row]]->format_title(&hook,text,col.script,nullptr);
        }
    }
    void reveal_playing_track() {
        auto pm=playlist_manager::get();
        const auto playing=playing_playlist();
        // The host's active playlist is the selected tab, not necessarily the playing one.
        if (playing!=pfc::infinite_size && pm->get_active_playlist()!=playing) pm->set_active_playlist(playing);
        if (pending_ || active_!=pm->get_active_playlist()) refresh(false);
        manager_.reveal(active_==pfc::infinite_size?-1:int(active_)); InvalidateRect(tabs_,nullptr,FALSE); update_arrows();
        // Check the destination playlist's restored search before revealing its song.
        if (applied_query_.empty() && window_text(search_).empty()) show_now_playing(false);
        SetFocus(list_);
    }
    t_size playing_playlist() const {
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        if (!play_control::get()->is_playing() ||
            !playlist_manager::get()->get_playing_item_location(&playlist,&item))
            return pfc::infinite_size;
        return playlist;
    }
    void draw_speaker(HDC dc,const RECT& rect,COLORREF color) {
        const int size=std::min({scale(16),int(rect.right-rect.left),int(rect.bottom-rect.top)});
        if(size<=0) return;
        if(!speaker_pixels_ || speaker_pixels_->width!=unsigned(size) || speaker_color_!=color) {
            speaker_pixels_=modern_playlist::speaker_icon(unsigned(size),color); speaker_color_=color;
        }
        modern_playlist::draw_artwork_gdi(dc,*speaker_pixels_,(rect.left+rect.right-size)/2,(rect.top+rect.bottom-size)/2,size,size);
    }
    void draw_lock(HDC dc,const RECT& rect,COLORREF color) {
        const int size=std::min({scale(16),int(rect.right-rect.left),int(rect.bottom-rect.top)});
        if(size<=0) return;
        if(!lock_pixels_ || lock_pixels_->width!=unsigned(size) || lock_color_!=color) {
            lock_pixels_=modern_playlist::lock_icon(unsigned(size),color); lock_color_=color;
        }
        modern_playlist::draw_artwork_gdi(dc,*lock_pixels_,(rect.left+rect.right-size)/2,(rect.top+rect.bottom-size)/2,size,size);
    }
    void repaint_playing_tab() {
        if(destroying_ || !tabs_) return;
        if(!pending_ && measured_playing_!=playing_playlist()) {
            measure_tabs(); layout();
            manager_.reveal(active_==pfc::infinite_size?-1:int(active_));
        }
        InvalidateRect(tabs_,nullptr,FALSE); update_arrows();
    }
    void on_playback_new_track(metadb_handle_ptr) override {
        request_wallpaper(); invalidate_all();
        if(parent_watch_) parent_busy(3000); // The host may now show this track's art behind the panel.
        repaint_playing_tab();
        update_state();
        if (list_) modern_playlist::invalidate_playlist_row(list_,-1);
        // The playlist location can become available after this callback returns.
        if (hwnd_) PostMessageW(hwnd_,state_message,0,0);
    }
    void on_playback_stop(play_control::t_stop_reason reason) override { if(reason!=play_control::stop_reason_starting_another) request_wallpaper(); if(parent_watch_) parent_busy(3000); invalidate_all(); repaint_playing_tab(); update_state(); if (list_) modern_playlist::invalidate_playlist_row(list_,-1); }
    void on_playback_pause(bool) override { update_state(); }
    static LRESULT CALLBACK ghost_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
        if (msg==WM_NCCREATE)
            SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self=reinterpret_cast<playlist_view*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if (msg==WM_NCHITTEST) return HTTRANSPARENT;
        if (msg==WM_PAINT && self) {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(wnd,&ps); RECT r{}; GetClientRect(wnd,&r);
            const auto& pal=self->current_palette(); fill(dc,r,pal.header);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,pal.text); SelectObject(dc,self->tabs_font_);
            const auto label=window_text(wnd);
            InflateRect(&r,-self->scale(6),0);
            self->draw_tab_text(dc,label,r,pal.text,true);
            EndPaint(wnd,&ps); return 0;
        }
        return DefWindowProcW(wnd,msg,wp,lp);
    }
    void move_drag_ghost(POINT pointer) {
        if (!drag_ghost_) return;
        RECT tab{}; if (!tab_rect(drag_tab_,&tab)) return;
        POINT screen{pointer.x-drag_grab_.x,pointer.y-drag_grab_.y};
        ClientToScreen(tabs_,&screen);
        SetWindowPos(drag_ghost_,HWND_TOPMOST,screen.x,screen.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    }
    void start_drag_ghost(POINT pointer) {
        if (drag_ghost_ || drag_tab_<0) return;
        RECT tab{}; if (!tab_rect(drag_tab_,&tab)) return;
        const auto& label=tab_names_[drag_tab_];
        WNDCLASSW wc{}; wc.lpfnWndProc=ghost_proc; wc.hInstance=core_api::get_my_instance();
        wc.lpszClassName=L"foo_modernplaylist.dragghost"; wc.style=CS_DROPSHADOW;
        RegisterClassW(&wc);
        drag_ghost_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
            wc.lpszClassName,label.c_str(),WS_POPUP,0,0,tab.right-tab.left,tab.bottom-tab.top,hwnd_,nullptr,wc.hInstance,this);
        if (drag_ghost_) {
            SetLayeredWindowAttributes(drag_ghost_,0,190,LWA_ALPHA);
            move_drag_ghost(pointer);
            ShowWindow(drag_ghost_,SW_SHOWNA);
        }
    }
    void cancel_tab_drag() {
        drag_tab_=-1; drag_tab_moved_=false; drag_before_=-1;
        KillTimer(hwnd_,5); stop_drag_ghost();
        if(GetCapture()==tabs_) ReleaseCapture();
        InvalidateRect(tabs_,nullptr,FALSE);
    }
    void stop_drag_ghost() {
        if (drag_ghost_) { DestroyWindow(drag_ghost_); drag_ghost_=nullptr; }
    }
    // Both hover and active sheets join the playlist, with no shadow.
    void draw_sheet_tab(HDC dc,const RECT& body,COLORREF color,unsigned opacity,bool artwork) {
        modern_playlist::sheet_tab_geometry shape;
        shape.width=body.right-body.left; shape.height=body.bottom-body.top;
        if(shape.width<=0 || shape.height<=0) return;
        shape.radius=scale(3); shape.attached_top=manager_bottom_;
        shape.flare=std::min(shape.radius,shape.height/2); // Same radius at the joined and free corners.
        const auto pixels=modern_playlist::sheet_tab_shape(shape,color,opacity);
        const int left=body.left-shape.margin();
        if(artwork) {
            if(const auto surface=background_surface()) {
                POINT origin{}; MapWindowPoints(tabs_,hwnd_,&origin,1);
                modern_playlist::sheet_tab_artwork(*pixels,*surface,origin.x+left,origin.y+body.top);
            }
        }
        modern_playlist::draw_artwork_gdi(dc,*pixels,left,body.top,int(pixels->width),int(pixels->height));
    }
    void paint_tabs() {
        // Moving the rename box first lets this paint cover the area it left.
        position_rename();
        // Paint only indices from the current playlist generation.
        if(pending_) { PAINTSTRUCT ps{}; auto dc=BeginPaint(tabs_,&ps); RECT r{}; GetClientRect(tabs_,&r); fill(dc,r,tab_strip_color()); EndPaint(tabs_,&ps); return; }
        update_hover();
        const auto& pal = current_palette();
        PAINTSTRUCT ps{}; HDC paint=BeginPaint(tabs_,&ps); RECT bounds{}; GetClientRect(tabs_,&bounds);
        HDC buffer=CreateCompatibleDC(paint); HBITMAP bitmap=CreateCompatibleBitmap(paint,std::max(1L,bounds.right),std::max(1L,bounds.bottom));
        HGDIOBJ old_bitmap=nullptr;
        HDC dc=paint;
        if(buffer && bitmap) { old_bitmap=SelectObject(buffer,bitmap); dc=buffer; }
        int saved=SaveDC(dc); fill(dc,bounds,tab_strip_color()); const bool artwork=paint_artwork_surface(dc,tabs_,bounds);
        if(artwork) modern_playlist::tint_artwork_gdi(dc,bounds,pal.text,20);
        SetBkMode(dc,TRANSPARENT); SelectObject(dc,tabs_font_);
        const auto playing=playing_playlist();
        const int count=int(tab_names_.size());
        // Hover first, active last so adjacent flares never darken the active
        // sheet. Text is painted afterwards and remains above both contours.
        for(int pass=0;pass<2;++pass) for (int i=0;i<count;++i) {
            const bool active=size_t(i)==active_;
            if(active!=(pass==1) || (!active && i!=hovered_tab_)) continue;
            RECT r{}; tab_rect(i,&r); r.bottom=std::min(r.bottom,bounds.bottom);
            if (r.right <= -scale(8) || r.left >= bounds.right+scale(8)) continue;
            const RECT body=tab_content_rect(r);
            if(artwork) draw_sheet_tab(dc,body,pal.row,active?255:128,true);
            else draw_sheet_tab(dc,body,active?active_tab_color():hover_background(),255,false);
        }
        const int stroke=std::max(1,scale(1));
        for (int i=0;i<count;++i) {
            // Separators end where a sheet begins, including after the last tab.
            if(!core_.tab_separators || size_t(i)==active_ || i==hovered_tab_ || size_t(i+1)==active_ || i+1==hovered_tab_) continue;
            RECT r{}; tab_rect(i,&r); r.bottom=std::min(r.bottom,bounds.bottom);
            if (r.right <= 0 || r.right-stroke >= bounds.right) continue;
            const RECT content=tab_content_rect(r);
            const int inset=std::max(1,int(content.bottom-content.top)/4);
            RECT separator{r.right-stroke,content.top+inset,r.right,content.bottom-inset};
            if(separator.bottom>separator.top) fill(dc,separator,pal.divider);
        }
        for (int i=0;i<count;++i) {
            RECT r{}; tab_rect(i,&r); r.bottom=std::min(r.bottom,bounds.bottom);
            if (r.right <= 0 || r.left >= bounds.right) continue;
            const bool active=size_t(i)==active_;
            const RECT content=tab_content_rect(r);
            const int icons=tab_icon_count(i,playing);
            RECT label=tab_label_rect(content,icons);
            const auto& title=tab_names_[i];
            const auto accent=core_.tab_custom_highlight?COLORREF(core_.tab_highlight_color):pal.highlight;
            draw_tab_text(dc,title,label,active && core_.tab_highlight_text?accent:pal.text);
            if((active && core_.tab_underline) || i==drop_tab_) {
                // Underline the name and the icons after it, so a lock,
                // speaker or close button does not leave the accent short.
                const LONG right=icons?tab_icon_rect(content).right:std::min(label.right,LONG(label.left+tab_text_width(dc,title)));
                const LONG underline_y=std::max(content.top,content.bottom-scale(3));
                RECT line{label.left,underline_y,right,std::min(content.bottom,underline_y+icon_stroke())};
                if(line.right>line.left && line.bottom>line.top) fill(dc,line,accent);
            }
            // Icons share the name's vertical centre.
            int slot=0;
            if(static_cast<t_size>(i)==playing) draw_speaker(dc,tab_icon_rect(content,slot++),pal.text);
            else if(tab_has_close(i,playing)) {
                RECT icon=tab_icon_rect(content,slot++); SetTextColor(dc,pal.muted);
                auto old=SelectObject(dc,close_font_); DrawTextW(dc,L"\uE711",1,&icon,DT_SINGLELINE|DT_VCENTER|DT_CENTER); SelectObject(dc,old);
            }
            if(modern_playlist::user_locked(i)) draw_lock(dc,tab_icon_rect(content,slot),pal.text);
        }
        if(rename_edit_ && !IsRectEmpty(&rename_frame_)) {
            // The rename box's field and focus frame, around the edit itself.
            fill(dc,rename_frame_,pal.search_bg);
            SetDCBrushColor(dc,core_.tab_custom_highlight?COLORREF(core_.tab_highlight_color):pal.highlight);
            FrameRect(dc,&rename_frame_,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        if(drag_tab_moved_ && drag_before_>=0) {
            int x=manager_.left(drag_before_); x=std::clamp(x,0,std::max(0,manager_.viewport-scale(2)));
            RECT marker{x,0,x+scale(2),tab_pixels_}; fill(dc,marker,pal.highlight);
        }
        RestoreDC(dc,saved);
        if(dc==buffer) { BitBlt(paint,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY); SelectObject(buffer,old_bitmap); }
        if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
        EndPaint(tabs_,&ps);
        update_arrows();
    }
    void schedule() { search_reveal_=pfc::infinite_size; ++content_epoch_; if (hwnd_ && !destroying_ && !pending_) { pending_ = true; modern_playlist::suspend_playlist_input(list_,true); PostMessageW(hwnd_,refresh_message,0,0); } }
    void refresh(bool reveal_playing=true) {
        if (destroying_ || !list_) return;
        pending_ = false;
        auto pm = playlist_manager::get(); auto next = pm->get_active_playlist();
        queries_.resize(pm->get_playlist_count());
        const bool switched = next != active_;
        active_ = next;
        if(switched) {
            cancel_header_drag();
            sort_column_=-1; sort_direction_=1;
            InvalidateRect(header_,nullptr,FALSE);
            collapsed_.clear(); clear_incremental(); search_reveal_=pfc::infinite_size;
        }
        bool group_sort_needed=false;
        if(grouping_.enabled && grouping_.playlist_filter && (switched || apply_filter_next_) && active_<pm->get_playlist_count()) {
            pfc::string8 name; pm->playlist_get_name(active_,name);
            if(modern_playlist::apply_playlist_filter(grouping_,name.c_str(),group_list_order(grouping_.patterns))) {
                collapsed_.clear(); compile_columns(); group_sort_needed=true;
            }
        }
        apply_filter_next_=false;
        if(columns_artwork_in_header_!=(grouping_.active() && grouping_.artwork_in_header)) { cancel_header_drag(); capture_columns(); make_columns(); }
        rebuilding_ = true;
        // Track metadata and selection updates must not recreate the tab strip.
        // Rebuilding resets its scroll position and exposes intermediate paints.
        if(tabs_dirty_) {
            tab_names_.clear(); tab_text_.clear();
            for(t_size i=0;i<pm->get_playlist_count();++i) { pfc::string8 name; pm->playlist_get_name(i,name); tab_names_.push_back(wide(name.c_str())); }
            tabs_dirty_=false;
        }
        measure_tabs();
        InvalidateRect(tabs_,nullptr,FALSE);
        if (switched && !search_settings_.scope) {
            KillTimer(hwnd_,search_timer);
            SetWindowTextW(search_,active_ < queries_.size() ? queries_[active_].c_str() : L"");
            applied_query_=window_text(search_);
        }
        if (switched) modern_playlist::reset_playlist_scroll(list_);
        rows_.clear(); items_.remove_all();
        std::wstring error;
        if (active_ < pm->get_playlist_count()) {
            pm->playlist_get_all_items(active_,items_);
            if(search_settings_.scope || applied_query_.empty()) {
                for(t_size i=0;i<items_.get_count();++i) rows_.push_back(i);
            } else try {
                const auto matches=search_items(items_,applied_query_);
                for(t_size i=0;i<matches.size();++i) if(matches[i]) rows_.push_back(i);
            } catch(const std::exception& e) { error=L"搜索："+wide(e.what()); }
        }
        update_manager_status();
        filtered_rows_=rows_;
        build_rows();
        SendMessageW(list_,WM_SETREDRAW,FALSE,0);
        ListView_SetItemCountEx(list_,static_cast<int>(rows_.size()),LVSICF_NOSCROLL);
        modern_playlist::set_playlist_groups(list_,groups_);
        ListView_SetItemState(list_,-1,0,LVIS_SELECTED | LVIS_FOCUSED);
        if (active_ < pm->get_playlist_count()) {
            bit_array_bittable selected(items_.get_count()); pm->playlist_get_selection_mask(active_,selected);
            auto focus = pm->playlist_get_focus_item(active_);
            for (size_t i=0;i<rows_.size();++i) ListView_SetItemState(list_,static_cast<int>(i),
                (selected[rows_[i]] ? LVIS_SELECTED : 0) | (focus == rows_[i] ? LVIS_FOCUSED : 0),LVIS_SELECTED | LVIS_FOCUSED);
        }

        SendMessageW(list_,WM_SETREDRAW,TRUE,0); InvalidateRect(list_,nullptr,TRUE);
        highlight_terms_=error.empty()?box_highlights():std::vector<std::wstring>{};
        update_search_visuals();
        update_grouping_controls();
        notice(error);
        layout();
        if(switched) { NotifyWinEvent(EVENT_OBJECT_SELECTION,tabs_,OBJID_CLIENT,active_==pfc::infinite_size?CHILDID_SELF:LONG(active_+1)); manager_.reveal(active_==pfc::infinite_size?-1:int(active_)); InvalidateRect(tabs_,nullptr,FALSE); update_arrows(); }
        EnableWindow(search_,TRUE); rebuilding_ = false;
        modern_playlist::suspend_playlist_input(list_,false);
        update_state();
        if (switched && reveal_playing) show_now_playing(false);
        if(group_sort_needed && grouping_.active()) apply_group_sort();
    }
    void update_search_controls() {
        update_grouping_controls();
        // Combo boxes paint their native face directly while handling these
        // messages, so send them only for real changes (Apply calls this too).
        auto select_index=[](HWND control,unsigned index) {
            if(SendMessageW(control,CB_GETCURSEL,0,0)!=LRESULT(index)) SendMessageW(control,CB_SETCURSEL,index,0);
        };
        select_index(search_field_,search_settings_.field); select_index(search_scope_,search_settings_.scope);
        for(auto control:{search_field_,search_scope_}) {
            if(SendMessageW(control,CB_GETITEMHEIGHT,WPARAM(-1),0)!=text_pixels_) SendMessageW(control,CB_SETITEMHEIGHT,WPARAM(-1),text_pixels_);
            if(SendMessageW(control,CB_GETITEMHEIGHT,0,0)!=text_pixels_+scale(6)) SendMessageW(control,CB_SETITEMHEIGHT,0,text_pixels_+scale(6));
        }
        if(search_themed_) return;
        search_themed_=true;
        // Native input/accessibility, with one painting path for every state.
        for(auto control:{search_field_,search_scope_}) SetWindowTheme(control,L"",L"");
        for(auto popup:{search_field_list_,search_scope_list_}) if(popup) {
            SetWindowTheme(popup,L"",L"");
            SetWindowLongPtrW(popup,GWL_EXSTYLE,GetWindowLongPtrW(popup,GWL_EXSTYLE)&~(WS_EX_CLIENTEDGE|WS_EX_STATICEDGE));
            SetWindowLongPtrW(popup,GWL_STYLE,GetWindowLongPtrW(popup,GWL_STYLE)|WS_BORDER);
            SetWindowPos(popup,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }
    }
    std::vector<std::wstring> box_highlights() const {
        if(search_settings_.field) return applied_query_.empty()?std::vector<std::wstring>{}:std::vector<std::wstring>{applied_query_};
        return modern_playlist::literal_search_terms(applied_query_);
    }
    void update_search_action() {
        if(!search_action_) return;
        // Setting a button's text erases and repaints it at once; skip no-ops.
        const wchar_t* label=GetWindowTextLengthW(search_)?L"清除搜索":L"聚焦搜索";
        if(window_text(search_action_)!=label) SetWindowTextW(search_action_,label);
        InvalidateRect(search_action_,nullptr,FALSE);
    }
    void draw_search_action(const DRAWITEMSTRUCT& draw) {
        const auto& pal=current_palette(); const auto r=draw.rcItem;
        const int saved=SaveDC(draw.hDC);
        IntersectClipRect(draw.hDC,r.left,r.top,r.right,r.bottom);
        paint_search_face(draw.hDC,draw.hwndItem,r);
        if(search_action_hovered_ || (draw.itemState&ODS_SELECTED))
            modern_playlist::tint_artwork_gdi(draw.hDC,r,pal.text,(draw.itemState&ODS_SELECTED)?24:12);
        const int size=std::min({scale(16),int(r.right-r.left)-2*scale(4),int(r.bottom-r.top)-2*scale(4)});
        if(size>0) {
            const auto pixels=modern_playlist::search_action_icon(unsigned(size),icon_color(pal.search_bg),GetWindowTextLengthW(search_)>0);
            modern_playlist::draw_artwork_gdi(draw.hDC,*pixels,(r.left+r.right-size)/2,(r.top+r.bottom-size)/2,size,size);
        }
        if((draw.itemState&ODS_FOCUS) && !(draw.itemState&ODS_NOFOCUSRECT)) {
            RECT cue{r.left+scale(5),r.bottom-std::max(1,scale(2)),r.right-scale(5),r.bottom-std::max(1,scale(1))};
            fill(draw.hDC,cue,pal.highlight);
        }
        RestoreDC(draw.hDC,saved);
    }
    void update_search_visuals(bool found=true) {
        update_search_action();
        modern_playlist::viewport_search state;
        state.terms=highlight_terms_; // Typing locates tracks; only the search box highlights cells.
        state.overlay=incremental_.text; state.found=found; state.color=search_settings_.color; state.field=search_settings_.field;
        modern_playlist::set_playlist_search(list_,state);
    }
    void clear_incremental() {
        KillTimer(hwnd_,incremental_timer); incremental_.clear(); update_search_visuals();
    }
    void toggle_search() {
        search_settings_.visible=!search_settings_.visible;
        if(!search_settings_.visible) {
            SendMessageW(search_field_,CB_SHOWDROPDOWN,FALSE,0);
            SendMessageW(search_scope_,CB_SHOWDROPDOWN,FALSE,0);
            if(grouping_tooltip_) SendMessageW(grouping_tooltip_,TTM_POP,0,0);
            if(GetFocus()==search_ || GetFocus()==search_action_ || GetFocus()==search_field_ || GetFocus()==search_scope_ ||
               GetFocus()==ungrouped_view_ || GetFocus()==grouped_view_) SetFocus(list_);
        }
        layout();
    }
    std::vector<bool> search_items(metadb_handle_list_cref items,const std::wstring& query) {
        std::vector<bool> result(items.get_count(),true);
        if(query.empty()) return result;
        if(!search_settings_.field) {
            auto filter=search_filter_manager::get()->create(utf8(query).c_str());
            std::unique_ptr<bool[]> matches(new bool[items.get_count()]); filter->test_multi(items,matches.get());
            for(size_t i=0;i<result.size();++i) result[i]=matches[i];
        } else {
            titleformat_object::ptr script;
            titleformat_compiler::get()->compile_safe(script,modern_playlist::search_fields[search_settings_.field].pattern);
            for(size_t i=0;i<result.size();++i) {
                pfc::string8 text; items[i]->format_title(nullptr,text,script,nullptr);
                result[i]=!modern_playlist::search_matches(wide(text.c_str()),{query}).empty();
            }
        }
        return result;
    }
    bool locate_track(t_size track) {
        if(pending_ || track>=items_.get_count()) return false;
        search_reveal_=track;
        auto row=std::lower_bound(rows_.begin(),rows_.end(),track);
        if(row==rows_.end() || *row!=track) {
            // A search reveal temporarily overrides both manual and automatic collapse.
            refresh(); row=std::lower_bound(rows_.begin(),rows_.end(),track);
        }
        if(row==rows_.end() || *row!=track) return false;
        const int index=int(row-rows_.begin());
        rebuilding_=true;
        ListView_SetItemState(list_,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_SetItemState(list_,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        rebuilding_=false; select_view(); ListView_EnsureVisible(list_,index,FALSE); return true;
    }
    void incremental_input(wchar_t ch) {
        if(pending_ || dragging_tracks_) return;
        const auto now=GetTickCount64();
        // Space retains its selection action until a typing search is in progress.
        if(ch==L' ' && (incremental_.text.empty() || incremental_.expired(now))) return;
        incremental_.input(ch,now);
        if(incremental_.text.empty()) { clear_incremental(); return; }
        // Wait for the low surrogate instead of searching half a UTF-16 character.
        const bool high=ch>=0xd800 && ch<=0xdbff;
        bool found=false;
        if(!high) for(auto track:filtered_rows_) {
            pfc::string8 value; items_[track]->format_title(nullptr,value,search_settings_.typing_field==modern_playlist::search_field_count?group_script_:typing_search_,nullptr);
            if(modern_playlist::search_prefix(wide(value.c_str()),incremental_.text)) { found=locate_track(track); break; }
        }
        update_search_visuals(found);
        SetTimer(hwnd_,incremental_timer,modern_playlist::incremental_idle_ms,nullptr);
    }
    void apply_search() {
        if(destroying_) return;
        clear_incremental(); search_reveal_=pfc::infinite_size; applied_query_=window_text(search_);
        if(search_settings_.scope) {
            // Empty library queries do not copy the entire library or erase the last results.
            if(applied_query_.empty()) { highlight_terms_.clear(); refresh(); return; }
            try {
                metadb_handle_list library,results; library_manager::get()->get_all_items(library);
                const auto matches=search_items(library,applied_query_);
                for(size_t i=0;i<matches.size();++i) if(matches[i]) results.add_item(library[i]);
                auto pm=playlist_manager::get();
                auto target=pm->find_playlist("媒体库搜索");
                if(target!=pfc::infinite_size && (!playlist_allows(target,playlist_lock::filter_add|playlist_lock::filter_remove) || modern_playlist::special_reserved(target)))
                    throw std::runtime_error("“媒体库搜索”已锁定；其内容保持不变。");
                if(target==pfc::infinite_size) target=pm->create_playlist("媒体库搜索",pfc::infinite_size,pfc::infinite_size);
                if(target==pfc::infinite_size) throw std::runtime_error("无法创建“媒体库搜索”。");
                pm->playlist_undo_backup(target);
                pm->playlist_remove_items(target,bit_array_true());
                pm->playlist_insert_items(target,pfc::infinite_size,results,bit_array_false());
                pm->set_active_playlist(target); refresh();
                notice(std::to_wstring(results.get_count())+L" 个媒体库匹配项");
            } catch(const std::exception& e) { highlight_terms_.clear(); update_search_visuals(); notice(L"搜索："+wide(e.what())); }
            return;
        }
        refresh();
    }
    void append_search_menu(HMENU menu) {
        HMENU search=CreatePopupMenu();
        AppendMenuW(search,MF_STRING|(search_settings_.visible?MF_CHECKED:0),500,L"显示搜索栏\t中键单击");
        AppendMenuW(search,MF_STRING,505,L"高亮颜色...");
        AppendMenuW(search,MF_STRING,506,L"重置高亮颜色");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(search),L"搜索");
    }
    bool search_command(int command) {
        if(command!=500 && command!=505 && command!=506) return false;
        if(command==500) toggle_search();
        if(command==505) {
            static COLORREF custom[16]{};
            CHOOSECOLORW choose{sizeof(choose)}; choose.hwndOwner=hwnd_; choose.rgbResult=search_settings_.color;
            choose.lpCustColors=custom; choose.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(ChooseColorW(&choose)) search_settings_.color=choose.rgbResult;
        }
        if(command==506) search_settings_.color=modern_playlist::search_settings{}.color;
        update_search_visuals(); return true;
    }
    bool pretranslate_message(MSG* message) override {
        // While a tab is renamed, its edit box owns the keyboard. foobar2000 keeps
        // only unmodified typing keys away from its shortcuts, so a shortcut on
        // Ctrl+A, Ctrl+Z, Ctrl+V or an F key would otherwise act on the playlist.
        if(!destroying_ && rename_edit_ && message->hwnd==rename_edit_ && message->message==WM_KEYDOWN) {
            TranslateMessage(message); DispatchMessageW(message); return true;
        }
        if(destroying_ || !hwnd_ || message->message!=WM_KEYDOWN || message->wParam!='F' ||
            !(GetKeyState(VK_CONTROL)&0x8000) || (GetKeyState(VK_SHIFT)&0x8000) || (GetKeyState(VK_MENU)&0x8000)) return false;
        const HWND source=message->hwnd;
        if(!source || (source!=hwnd_ && !IsChild(hwnd_,source) && source!=search_field_list_ && source!=search_scope_list_)) return false;
        if(is_layout_editing()) return false;
        // Do not interrupt captured track/column/tab/scrollbar gestures.
        const auto captured=GetCapture();
        if(header_ghost_ || dragging_tracks_ || drag_tab_>=0 || (captured && (captured==list_ || captured==header_ || captured==tabs_ || captured==scrollbar_))) return true;
        try { focus_search(); } catch(const std::exception& error) { console::error(error.what()); }
        return true;
    }
    void focus_search() {
        if(destroying_ || !search_) return;
        SendMessageW(search_field_,CB_SHOWDROPDOWN,FALSE,0);
        SendMessageW(search_scope_,CB_SHOWDROPDOWN,FALSE,0);
        search_settings_.visible=true; layout(); EnableWindow(search_,TRUE);
        SetFocus(search_); SendMessageW(search_,EM_SETSEL,0,-1);
    }
    void clear_search() {
        if (destroying_ || !search_) return;
        KillTimer(hwnd_, search_timer);
        rebuilding_=true; SetWindowTextW(search_, L""); rebuilding_=false;
        applied_query_.clear(); search_reveal_=pfc::infinite_size; clear_incremental();
        if (!search_settings_.scope && active_ < queries_.size()) queries_[active_].clear();
        InvalidateRect(search_, nullptr, TRUE);
        refresh();
        if (list_) SetFocus(list_);
    }
    void select_view() {
        if (rebuilding_ || pending_ || active_ == pfc::infinite_size) return;
        auto pm = playlist_manager::get();
        bit_array_bittable affected(items_.get_count()), selected(items_.get_count());
        for (size_t i=0;i<rows_.size();++i) { affected.set(rows_[i],true); selected.set(rows_[i],(ListView_GetItemState(list_,static_cast<int>(i),LVIS_SELECTED) & LVIS_SELECTED) != 0); }
        // Filtered-out selection is intentionally untouched.
        rebuilding_ = true;
        pm->playlist_set_selection(active_,affected,selected);
        int focus = ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (focus >= 0 && static_cast<size_t>(focus) < rows_.size()) pm->playlist_set_focus_item(active_,rows_[focus]);
        rebuilding_ = false;
    }
    std::vector<t_size> selected_rows() const {
        std::vector<t_size> result;
        for (int i=ListView_GetNextItem(list_,-1,LVNI_SELECTED); i >= 0; i=ListView_GetNextItem(list_,i,LVNI_SELECTED))
            if (static_cast<size_t>(i) < rows_.size()) result.push_back(rows_[i]);
        return result;
    }
    void play() {
        int row = ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (!pending_ && row >= 0 && static_cast<size_t>(row)<rows_.size()) playlist_manager::get()->playlist_execute_default_action(active_,rows_[row]);
    }
    void remove_tracks() {
        if (pending_ || !accepts_tracks(active_,playlist_lock::filter_remove)) return;
        auto selected = selected_rows(); if (selected.empty()) return;
        bit_array_bittable mask(items_.get_count());
        for (auto i:selected) mask.set(i,true);
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_remove_items(active_,mask);
    }
    void copy_tracks(bool cut) {
        if (pending_ || !playlist_allows(active_,0) || autoplaylist_manager::get()->is_client_present(active_) ||
            (cut && !playlist_allows(active_,playlist_lock::filter_remove))) return;
        metadb_handle_list selected; for (auto i : selected_rows()) selected.add_item(items_[i]);
        if (!selected.get_count()) return;
        auto object = ole_interaction::get()->create_dataobject(selected);
        if (SUCCEEDED(OleSetClipboard(object.get_ptr()))) {
            OleFlushClipboard();
            if (cut) remove_tracks();
        }
    }
    void paste_tracks() {
        if (pending_ || !accepts_tracks(active_)) return;
        pfc::com_ptr_t<IDataObject> object;
        if (FAILED(OleGetClipboard(object.receive_ptr()))) return;
        metadb_handle_list incoming;
        if (FAILED(ole_interaction::get()->parse_dataobject_immediate(object,incoming))) {
            notice(L"粘贴从 foobar2000 复制的曲目。其他文件请使用“添加文件”。"); return;
        }
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_);
        pm->playlist_insert_items(active_,pfc::infinite_size,incoming,bit_array_true());
    }
    void move_tracks(int direction) {
        if (pending_ || rows_.empty() || !playlist_allows(active_,playlist_lock::filter_reorder)) return;
        auto selected = selected_rows();
        std::vector<bool> mask(rows_.size());
        for (size_t i=0;i<rows_.size();++i) mask[i]=std::find(selected.begin(),selected.end(),rows_[i])!=selected.end();
        auto sorted=modern_playlist::nudge_order(mask,direction);
        auto order=modern_playlist::visible_order(rows_,sorted,items_.get_count());
        auto pm=playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size());
    }
    void clear_track_drop() {
        if(list_) SendMessageW(list_,modern_playlist::viewport_drop_clear,0,0);
        if(drop_tab_!=-1) { drop_tab_=-1; if(tabs_) InvalidateRect(tabs_,nullptr,FALSE); }
        if(hwnd_) KillTimer(hwnd_,7);
    }
    void stop_track_drop() noexcept {
        *drag_alive_=false;
        drop_targets_.clear();
        if(hwnd_) KillTimer(hwnd_,7);
    }
    bool accepts_tracks(t_size index,t_uint32 operation=playlist_lock::filter_add) const {
        return playlist_allows(index,operation) &&
            (operation==playlist_lock::filter_reorder || !autoplaylist_manager::get()->is_client_present(index));
    }
    static bool same_items(metadb_handle_list_cref a,metadb_handle_list_cref b) {
        if(a.get_count()!=b.get_count()) return false;
        for(t_size i=0;i<a.get_count();++i) if(a[i]!=b[i]) return false;
        return true;
    }
    void start_track_drag() {
        if(pending_ || destroying_ || dragging_tracks_) return;
        auto selected=selected_rows(); if(selected.empty()) return;
        metadb_handle_list handles;
        for(auto i:selected) handles.add_item(items_[i]);
        auto object=ole_interaction::get()->create_dataobject(handles);
        // Keep the service alive across DoDragDrop's nested loop, including host
        // layout changes that destroy this panel's HWND.
        ui_element_instance::ptr keep_alive=this;
        Microsoft::WRL::ComPtr<modern_playlist::track_drop_source> source;
        source.Attach(new modern_playlist::track_drop_source(drag_alive_));
        drag_object_=object.get_ptr(); drag_indices_=std::move(selected);
        drag_items_=items_; drag_playlist_=playlist_manager_v5::get()->playlist_get_guid(active_);
        drag_content_epoch_=content_epoch_; dragging_tracks_=true;
        ReleaseCapture();
        DWORD effect=0;
        DoDragDrop(object.get_ptr(),source.Get(),DROPEFFECT_COPY|DROPEFFECT_MOVE,&effect);
        // MOVE is handled only by our own same-playlist drop. External targets
        // receive tracks, never permission to delete playlist occurrences here.
        dragging_tracks_=false; drag_object_=nullptr; drag_indices_.clear(); drag_items_.remove_all();
        if(!destroying_) clear_track_drop();
    }
    DWORD receive_track_drop(HWND window,IDataObject* data,DWORD keys,POINTL screen,DWORD allowed,bool commit) {
        if(destroying_ || pending_) { clear_track_drop(); return DROPEFFECT_NONE; }
        auto pm=playlist_manager_v5::get();
        const bool local=dragging_tracks_ && data==drag_object_;
        POINT point{screen.x,screen.y}; ScreenToClient(window,&point);
        t_size target=pfc::infinite_size, before=pfc::infinite_size;
        const bool create=window==add_;
        bool list_target=window==list_;
        if(list_target) {
            modern_playlist::viewport_drop_position hit; hit.point=point; hit.scroll=!commit;
            SendMessageW(list_,modern_playlist::viewport_drop_hit,0,reinterpret_cast<LPARAM>(&hit));
            if(!hit.valid) { clear_track_drop(); return DROPEFFECT_NONE; }
            target=active_;
            if(hit.row>=0 && size_t(hit.row)<rows_.size()) before=rows_[hit.row]+size_t(hit.after);
            else if(hit.group>=0 && size_t(hit.group)<group_members_.size() && !group_members_[hit.group].empty()) {
                const auto& members=group_members_[hit.group]; before=hit.after?members.back()+1:members.front();
            } else before=items_.get_count();
        } else if(window==tabs_) {
            if(!commit) {
                const auto now=GetTickCount64();
                if(now-drop_scroll_time_>=60) {
                    if(point.x<scale(20)) manager_.offset-=scale(16);
                    else if(point.x>=manager_.viewport-scale(20)) manager_.offset+=scale(16);
                    manager_.clamp(); drop_scroll_time_=now; InvalidateRect(tabs_,nullptr,FALSE);
                }
            }
            int tab=tab_hit(point);
            if(tab>=0) target=size_t(tab);
        } else if(!create) return DROPEFFECT_NONE;
        const bool move=local && list_target && target<pm->get_playlist_count() &&
            pm->playlist_get_guid(target)==drag_playlist_ && !(keys&MK_CONTROL);
        const DWORD effect=move?DROPEFFECT_MOVE:DROPEFFECT_COPY;
        if(!(allowed&effect) || (!create && !accepts_tracks(target,move?playlist_lock::filter_reorder:playlist_lock::filter_add)) ||
            (local && (drag_content_epoch_!=content_epoch_ || (commit && !same_items(items_,drag_items_))))) {
            clear_track_drop(); return DROPEFFECT_NONE;
        }
        if(!playlist_incoming_item_filter::get()->process_dropped_files_check(data)) { clear_track_drop(); return DROPEFFECT_NONE; }
        if(!list_target) {
            SendMessageW(list_,modern_playlist::viewport_drop_clear,0,0);
            const int tab=window==tabs_?int(target):-1;
            if(tab!=drop_tab_) { drop_tab_=tab; InvalidateRect(tabs_,nullptr,FALSE); }
        }
        if(!commit) { SetTimer(hwnd_,7,60,nullptr); return effect; }
        if(move) {
            std::vector<bool> selected(items_.get_count()); for(auto i:drag_indices_) selected[i]=true;
            const auto order=modern_playlist::drop_order(selected,before);
            bool changed=false; for(size_t i=0;i<order.size();++i) if(order[i]!=i) { changed=true; break; }
            if(changed) { pm->playlist_undo_backup(target); pm->playlist_reorder_items(target,order.data(),order.size()); }
            return effect;
        }
        // Snapshot the exact target contents for a positioned asynchronous drop.
        // If those contents change while loading files, cancel instead of using
        // stale occurrence indices. Tab drops append to the current contents.
        const GUID id=create?GUID{}:pm->playlist_get_guid(target);
        auto original=std::make_shared<metadb_handle_list>();
        if(list_target) pm->playlist_get_all_items(target,*original);
        auto complete=[id,before,create,list_target,original](metadb_handle_list_cref incoming) {
            try {
                if(!incoming.get_count()) return;
                auto manager=playlist_manager_v5::get();
                auto index=create?pfc::infinite_size:manager->find_playlist_by_guid(id);
                if(!create) {
                    if(index==pfc::infinite_size || autoplaylist_manager::get()->is_client_present(index) ||
                        (manager->playlist_lock_get_filter_mask(index)&playlist_lock::filter_add)) return;
                    if(list_target) {
                        metadb_handle_list current; manager->playlist_get_all_items(index,current);
                        if(!same_items(current,*original)) { console::warning("现代播放列表：由于加载期间目标内容发生变化，拖放已取消。"); return; }
                    }
                } else index=manager->create_playlist_autoname();
                if(index==pfc::infinite_size) return;
                manager->playlist_undo_backup(index);
                manager->playlist_insert_items(index,before,incoming,bit_array_true());
                if(create) manager->set_active_playlist(index);
            } catch(const std::exception& e) { console::error(e.what()); }
        };
        if(local) {
            metadb_handle_list incoming; for(auto i:drag_indices_) incoming.add_item(drag_items_[i]);
            complete(incoming);
        } else {
            playlist_incoming_item_filter_v2::get()->process_dropped_files_async(data,
                playlist_incoming_item_filter_v2::op_flag_no_filter|playlist_incoming_item_filter_v2::op_flag_delay_ui,
                core_api::get_main_window(),process_locations_notify::create(complete));
        }
        return effect;
    }
    void register_track_drops() {
        for(HWND window:{list_,tabs_,add_}) {
            if(!drop_targets_.add(window,[this,window](IDataObject* data,DWORD keys,POINTL point,DWORD allowed,bool commit) {
                ui_element_instance::ptr keep_alive=this;
                return receive_track_drop(window,data,keys,point,allowed,commit);
            },[this] { clear_track_drop(); })) console::warning("现代播放列表：无法注册 OLE 拖放目标。");
        }
    }
    void crop_tracks() {
        if(pending_ || !accepts_tracks(active_,playlist_lock::filter_remove)) return;
        const auto selected=selected_rows(); if(selected.empty()) return;
        bit_array_bittable mask(items_.get_count());
        bool changed=false;
        for(auto row:rows_) if(!std::binary_search(selected.begin(),selected.end(),row)) {
            mask.set(row,true); changed=true;
        }
        if(changed) {
            auto pm=playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_remove_items(active_,mask);
        }
    }
    void transfer_selection(const GUID& destination,bool replace,bool create) {
        if(pending_) return;
        metadb_handle_list tracks; for(auto i:selected_rows()) tracks.add_item(items_[i]);
        if(!tracks.get_count()) return;
        auto pm=playlist_manager_v5::get();
        auto target=create?pfc::infinite_size:pm->find_playlist_by_guid(destination);
        const auto operation=playlist_lock::filter_add|(replace?playlist_lock::filter_remove:0);
        if(!create && !accepts_tracks(target,operation)) return;
        // Snapshot first: creating a playlist synchronously invalidates this view.
        if(create) target=pm->create_playlist_autoname();
        if(target==pfc::infinite_size || !accepts_tracks(target,operation)) return;
        pm->playlist_undo_backup(target);
        if(replace && !pm->playlist_remove_items(target,bit_array_true())) return;
        pm->playlist_insert_items(target,pfc::infinite_size,tracks,bit_array_true());
        if(replace || create) pm->set_active_playlist(target);
    }
    void track_menu(POINT pt) {
        if (pending_) return;
        HMENU menu=CreatePopupMenu(),selection=CreatePopupMenu(),add=CreatePopupMenu(),send=CreatePopupMenu();
        const auto selected=selected_rows();
        const auto epoch=content_epoch_;
        const bool any=!selected.empty();
        auto flags=[](bool allowed) -> UINT { return MF_STRING|(allowed?0:MF_GRAYED); };
        HMENU view=CreatePopupMenu();
        AppendMenuW(view,MF_STRING,13,L"面板设置...");
        AppendMenuW(view,MF_STRING|(core_.extra_line?MF_CHECKED:0),14,L"显示附加行信息");
        AppendMenuW(view,MF_STRING|(show_scrollbar_?MF_CHECKED:0),508,L"显示滚动条");
        AppendMenuW(view,MF_STRING|(show_status_?MF_CHECKED:0),509,L"显示状态栏");
        AppendMenuW(view,MF_STRING|(artwork_.enabled?MF_CHECKED:0),510,L"启用封面背景");
        AppendMenuW(view,MF_STRING|(show_header_?MF_CHECKED:0),9,L"显示列标题\tCtrl+T");
        append_search_menu(view); append_groups_menu(view);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"面板");
        AppendMenuW(menu,MF_STRING,12,L"显示播放队列");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,flags(any),4,L"播放\tEnter");
        AppendMenuW(selection,flags(any && accepts_tracks(active_,playlist_lock::filter_remove)),10,L"收集");
        AppendMenuW(selection,flags(any && accepts_tracks(active_,playlist_lock::filter_remove)),7,L"移除\tDelete");
        AppendMenuW(add,flags(any),20,L"新建播放列表");
        AppendMenuW(send,flags(any),21,L"新建播放列表");
        AppendMenuW(add,MF_SEPARATOR,0,nullptr); AppendMenuW(send,MF_SEPARATOR,0,nullptr);
        auto pm=playlist_manager_v5::get();
        std::vector<GUID> targets;
        // IDs below the native context range, with a bounded, non-overlapping range per action.
        for(t_size i=0;i<pm->get_playlist_count() && targets.size()<4096;++i) {
            targets.push_back(pm->playlist_get_guid(i));
            pfc::string8 name; pm->playlist_get_name(i,name);
            auto label=wide(name.c_str());
            for(size_t at=0;(at=label.find(L'&',at))!=std::wstring::npos;at+=2) label.insert(at,1,L'&');
            AppendMenuW(add,flags(any && accepts_tracks(i)),0x1000+targets.size()-1,label.c_str());
            AppendMenuW(send,flags(any && accepts_tracks(i,playlist_lock::filter_add|playlist_lock::filter_remove)),0x2000+targets.size()-1,label.c_str());
        }
        AppendMenuW(selection,MF_POPUP,reinterpret_cast<UINT_PTR>(add),L"添加到...");
        AppendMenuW(selection,MF_POPUP,reinterpret_cast<UINT_PTR>(send),L"发送到...");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(selection),L"选择...");
        const bool copy=any && playlist_allows(active_,0) && !autoplaylist_manager::get()->is_client_present(active_);
        AppendMenuW(menu,flags(copy),6,L"复制\tCtrl+C");
        AppendMenuW(menu,flags(copy && accepts_tracks(active_,playlist_lock::filter_remove)),11,L"剪切\tCtrl+X");
        AppendMenuW(menu,flags(accepts_tracks(active_)),8,L"粘贴\tCtrl+V");
        contextmenu_manager::ptr context;
        if (any) {
            metadb_handle_list handles; for (auto i:selected) handles.add_item(items_[i]);
            context=contextmenu_manager::g_create();
            context->init_context(handles,contextmenu_manager::flag_show_shortcuts);
            // The native menu is the sole provider of Add to playback queue.
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr); context->win32_build_menu(menu,0x4000,0x4000);
        }
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr); DestroyMenu(menu);
        if (!command || destroying_) return;
        if(search_command(command)) return; // Search does not depend on a stale track selection.
        if (pending_ || content_epoch_ != epoch) return;
        if(command==510) { toggle_background(); return; }
        if(command==509) { toggle_status(); return; }
        if(command==508) { show_scrollbar_=!show_scrollbar_; theme(); fit_columns(); return; }
        if(command==307) { edit_core_settings(2); return; }
        if(group_command(command)) return;
        if(command>=0x1000 && command<0x3000) {
            const bool replace=command>=0x2000;
            const auto index=size_t(command-(replace?0x2000:0x1000));
            if(index<targets.size()) transfer_selection(targets[index],replace,false);
            return;
        }
        switch(command) {
        case 4: play(); break;
        case 6: copy_tracks(false); break;
        case 7: remove_tracks(); break;
        case 8: paste_tracks(); break;
        case 9: toggle_header(); break;
        case 10: crop_tracks(); break;
        case 11: copy_tracks(true); break;
        case 12: modern_playlist::show_playback_queue(); break;
        case 13: edit_core_settings(); break;
        case 14: core_.extra_line=!core_.extra_line; theme(); layout(); break;
        case 20: case 21: transfer_selection(GUID{},command==21,true); break;
        default: if (command>=0x4000 && command<=0x7fff && context.is_valid()) context->execute_by_id(command-0x4000); break;
        }
    }
    void sort(int logical) {
        if (!playlist_allows(active_,playlist_lock::filter_reorder) || pending_ || logical < 0 || static_cast<size_t>(logical)>=visible_columns_.size() || filtered_rows_.empty()) return;
        int col = visible_columns_[logical]; if (columns_[col].state || columns_[col].ref=="Cover" || columns_[col].ref=="ArtistArt" || columns_[col].ref=="Index") return; sort_direction_ = sort_column_ == col ? -sort_direction_ : 1; sort_column_ = col;
        metadb_handle_list visible; for (auto i: filtered_rows_) visible.add_item(items_[i]);
        std::vector<t_size> sorted(filtered_rows_.size());
        metadb_handle_list_helper::sort_by_format_get_order(visible,sorted.data(),columns_[col].sort_script,nullptr,sort_direction_);
        auto order = modern_playlist::visible_order(filtered_rows_,sorted,items_.get_count());
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size());
        InvalidateRect(header_,nullptr,TRUE);
    }
    void new_playlist() {
        auto pm = playlist_manager::get();
        const auto index = pm->create_playlist_autoname();
        if (index != pfc::infinite_size) pm->set_active_playlist(index);
    }
    bool playlist_allows(t_size index,t_uint32 operation) const {
        auto pm=playlist_manager::get();
        return index<pm->get_playlist_count() && !(pm->playlist_lock_get_filter_mask(index)&operation);
    }
    // F2, the tab menu and double-clicking a tab rename in place, in an edit
    // box over the tab's name. Without a visible strip, the dialog is used.
    void rename_playlist(t_size index) {
        if(!playlist_allows(index,playlist_lock::filter_rename) || modern_playlist::special_reserved(index)) return;
        end_rename(true);
        if(pending_) refresh();
        if(!begin_rename(index)) rename_playlist_dialog(index);
    }
    void rename_playlist_dialog(t_size index) {
        auto pm=playlist_manager_v5::get();
        if(!playlist_allows(index,playlist_lock::filter_rename) || modern_playlist::special_reserved(index)) return;
        const GUID id=pm->playlist_get_guid(index);
        pfc::string8 name; pm->playlist_get_name(index,name); dialog_data data; data.value=wide(name.c_str());
        if(DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_TEXT),hwnd_,dialog_proc,reinterpret_cast<LPARAM>(&data))!=IDOK) return;
        commit_rename(id,data.value);
    }
    // Locks are checked again here: they may change while the name is edited.
    void commit_rename(const GUID& id,const std::wstring& value) {
        auto pm=playlist_manager_v5::get(); const auto index=pm->find_playlist_by_guid(id);
        if(!playlist_allows(index,playlist_lock::filter_rename) || modern_playlist::special_reserved(index)) return;
        const auto text=utf8(value); pfc::string8 current; pm->playlist_get_name(index,current);
        if(!text.empty() && text!=current.c_str()) pm->playlist_rename(index,text.c_str(),text.size());
    }
    bool begin_rename(t_size index) {
        if(destroying_ || !tabs_ || !show_tabs_ || !IsWindowVisible(tabs_) || index>=tab_names_.size()) return false;
        RECT strip{}; GetClientRect(tabs_,&strip);
        if(strip.bottom<font_height(tabs_font_)+2) return false; // The strip is clipped too short to type in.
        cancel_tab_drag();
        manager_.reveal(int(index)); update_arrows();
        pfc::string8 name; playlist_manager::get()->playlist_get_name(index,name);
        const HWND edit=CreateWindowExW(0,L"EDIT",wide(name.c_str()).c_str(),WS_CHILD|ES_AUTOHSCROLL,0,0,0,0,
            tabs_,nullptr,core_api::get_my_instance(),nullptr);
        if(!edit) return false;
        rename_focus_=GetFocus(); rename_guid_=playlist_manager_v5::get()->playlist_get_guid(index); rename_edit_=edit;
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(tabs_font_),FALSE);
        SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(scale(2),scale(2)));
        if(themed_dark_==1) SetWindowTheme(edit,L"DarkMode_CFD",nullptr);
        SetWindowSubclass(edit,rename_proc,1,reinterpret_cast<DWORD_PTR>(this));
        position_rename(); if(!rename_edit_) return true; // Its tab vanished; nothing to edit.
        SetFocus(edit); SendMessageW(edit,EM_SETSEL,0,-1);
        InvalidateRect(tabs_,nullptr,FALSE);
        return true;
    }
    // Place the box over its tab's name, widening it to the typed text up to the
    // longest tab width, and the frame around it. The box ends when the strip is
    // hidden or the playlist is gone.
    void position_rename() {
        if(!rename_edit_ || pending_ || destroying_) return;
        const auto index=playlist_manager_v5::get()->find_playlist_by_guid(rename_guid_);
        RECT tab{};
        if(!show_tabs_ || !IsWindowVisible(tabs_) || index>=tab_names_.size() || !tab_rect(int(index),&tab)) { end_rename(true); return; }
        const RECT content=tab_content_rect(tab);
        const RECT label=tab_label_rect(content,tab_icon_count(int(index),playing_playlist()));
        RECT strip{}; GetClientRect(tabs_,&strip);
        const auto text=window_text(rename_edit_); SIZE extent{};
        if(HDC dc=GetDC(rename_edit_)) {
            const auto old=SelectObject(dc,tabs_font_);
            GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&extent);
            SelectObject(dc,old); ReleaseDC(rename_edit_,dc);
        }
        const int margin=scale(2), pad=scale(2), font=font_height(tabs_font_);
        const int left=std::max(1,int(label.left)-margin);
        const int least=int(label.right-label.left)+2*margin;
        const int width=std::min(std::clamp(int(extent.cx)+2*margin+scale(4),least,std::max(least,scale(240))),int(strip.right)-left-1);
        const int height=std::min(font,int(content.bottom-content.top)-2*(pad+1));
        const int top=int(content.top)+(int(content.bottom-content.top)-height)/2;
        RECT frame{left-1,top-pad-1,left+width+1,top+height+pad+1};
        if(width<=0 || height<=0) frame={};
        RECT current{}; GetWindowRect(rename_edit_,&current); MapWindowPoints(nullptr,tabs_,reinterpret_cast<POINT*>(&current),2);
        const RECT wanted{left,top,left+std::max(0,width),top+std::max(0,height)};
        if(!EqualRect(&frame,&rename_frame_)) { rename_frame_=frame; InvalidateRect(tabs_,nullptr,FALSE); }
        if(EqualRect(&current,&wanted) && IsWindowVisible(rename_edit_)) return;
        const bool grew=IsWindowVisible(rename_edit_) && wanted.right-wanted.left>current.right-current.left;
        SetWindowPos(rename_edit_,HWND_TOP,wanted.left,wanted.top,wanted.right-wanted.left,wanted.bottom-wanted.top,
            SWP_NOACTIVATE|(IsRectEmpty(&frame)?SWP_HIDEWINDOW:SWP_SHOWWINDOW));
        if(grew && int(extent.cx)+2*margin<=width) {
            // Typing scrolled the narrower box; show the whole name from its start.
            DWORD first=0,last=0; SendMessageW(rename_edit_,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
            SendMessageW(rename_edit_,EM_SETSEL,0,0); SendMessageW(rename_edit_,EM_SETSEL,first,last);
        }
    }
    void end_rename(bool commit) {
        const HWND edit=rename_edit_; if(!edit) return;
        rename_edit_=nullptr; rename_frame_={};
        commit=commit && !destroying_;
        const auto text=commit?window_text(edit):std::wstring{}; const GUID id=rename_guid_;
        // Enter or Escape returns focus to where renaming started.
        if(GetFocus()==edit && !destroying_) {
            const HWND back=rename_focus_;
            SetFocus(back && back!=edit && IsWindowVisible(back) && (back==hwnd_ || IsChild(hwnd_,back))?back:
                IsWindowVisible(tabs_)?tabs_:list_);
        }
        DestroyWindow(edit);
        if(tabs_) InvalidateRect(tabs_,nullptr,FALSE);
        if(commit) commit_rename(id,text);
    }
    // The native edit supplies typing, caret movement, selection by keyboard,
    // mouse drag and double-click, Ctrl+C/V/X/Z, its context menu and IME.
    // This handles only the keys that finish the edit, plus Ctrl+A and
    // Ctrl+Backspace, which a single-line edit box does not provide.
    static LRESULT CALLBACK rename_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto* self=reinterpret_cast<playlist_view*>(data);
        switch(msg) {
        case WM_NCDESTROY:
            RemoveWindowSubclass(wnd,rename_proc,id);
            if(self->rename_edit_==wnd) { self->rename_edit_=nullptr; self->rename_frame_={}; } // Destroyed with the strip.
            break;
        // Keep Enter, Escape and Tab away from the host's dialog navigation.
        case WM_GETDLGCODE: return DefSubclassProc(wnd,msg,wp,lp)|DLGC_WANTALLKEYS;
        case WM_KEYDOWN: {
            const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0, alt=(GetKeyState(VK_MENU)&0x8000)!=0;
            if(wp==VK_RETURN || wp==VK_TAB) { self->end_rename(true); return 0; }
            if(wp==VK_ESCAPE) { self->end_rename(false); return 0; }
            if(control && !alt && wp=='A') { SendMessageW(wnd,EM_SETSEL,0,-1); return 0; }
            if(control && !alt && wp==VK_BACK) {
                DWORD first=0,last=0; SendMessageW(wnd,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
                if(first==last) {
                    // Delete back to the start of the previous word.
                    const auto text=window_text(wnd); size_t start=std::min<size_t>(first,text.size());
                    while(start && (text[start-1]==L' ' || text[start-1]==L'\t')) --start;
                    while(start && text[start-1]!=L' ' && text[start-1]!=L'\t') --start;
                    SendMessageW(wnd,EM_SETSEL,start,last);
                }
                SendMessageW(wnd,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));
                return 0;
            }
            break;
        }
        // The keys above still produce characters; never type or beep for them.
        case WM_CHAR: if(wp==VK_RETURN || wp==L'\n' || wp==VK_TAB || wp==VK_ESCAPE || wp==1 || wp==127) return 0; break;
        // Clicking elsewhere or switching windows keeps the typed name.
        case WM_KILLFOCUS: if(self->hwnd_) PostMessageW(self->hwnd_,rename_message,reinterpret_cast<WPARAM>(wnd),0); break;
        }
        return DefSubclassProc(wnd,msg,wp,lp);
    }
    void create_autoplaylist(const autoplaylist_data& data,t_size before) {
        auto pm=playlist_manager_v5::get();
        // Validate before creating anything, including predefined queries.
        search_filter_manager::get()->create(data.query.c_str());
        if(before==0 && modern_playlist::library_pinned(0)) before=1;
        const auto index=pm->create_playlist(data.name.c_str(),data.name.size(),before);
        if(index==pfc::infinite_size) return;
        try { autoplaylist_manager::get()->add_client_simple(data.query.c_str(),data.sort.c_str(),index,data.force?autoplaylist_flag_sort:0); }
        catch(...) { pm->remove_playlist(index); throw; }
        pm->set_active_playlist(index);
    }
    void update_manager_status() {
        auto pm=playlist_manager::get();
        const auto active=pm->get_active_playlist();
        const auto text=modern_playlist::manager_status_text(active<pm->get_playlist_count(),
            active<pm->get_playlist_count()?pm->playlist_get_item_count(active):0);
        if(window_text(status_)!=text) SetWindowTextW(status_,text.c_str());
        const bool sortable=pm->get_playlist_count()>(modern_playlist::library_pinned(0)?2U:1U);
        EnableWindow(sort_az_,sortable); EnableWindow(sort_za_,sortable);
    }
    void sort_playlists(bool ascending) {
        cancel_tab_drag();
        auto pm=playlist_manager::get();
        std::vector<std::wstring> names;
        for(t_size i=0;i<pm->get_playlist_count();++i) { pfc::string8 name; pm->playlist_get_name(i,name); names.push_back(wide(name.c_str())); }
        const modern_playlist::explorer_name_compare compare;
        const auto order=modern_playlist::manager_name_order(names.size(),modern_playlist::library_pinned(0),[&](size_t a,size_t b) {
            const int comparison=compare(names[a],names[b]);
            return ascending?comparison<0:comparison>0;
        });
        pm->reorder(order.data(),order.size());
    }
    void tab_menu(POINT pt) {
        end_rename(true);
        if(pending_) refresh();
        POINT local=pt; ScreenToClient(tabs_,&local);
        auto pm=playlist_manager_v5::get();
        int index=show_tabs_?tab_hit(local):-1;
        const GUID target=index>=0?pm->playlist_get_guid(index):GUID{};
        const bool automatic=index>=0 && autoplaylist_manager::get()->is_client_present(index);
        const bool reserved=index>=0 && modern_playlist::special_reserved(index);
        auto flags=[&](t_uint32 mask,bool normal=false) -> UINT {
            return MF_STRING|((index<0 || !playlist_allows(index,mask) || (normal && reserved))?MF_GRAYED:0);
        };
        HMENU menu=CreatePopupMenu(),create=CreatePopupMenu(),presets=CreatePopupMenu();
        AppendMenuW(create,MF_STRING,1,L"新建播放列表\tCtrl+N");
        AppendMenuW(create,MF_STRING,6,L"新建智能列表...");
        const char* names[]={"从未播放","近5天播放","未评级的音轨","评级为 3-5","评级为 4","评级为 5","喜爱的音轨"};
        const char* queries[]={"%play_count% MISSING OR %play_count% IS 0","%last_played% DURING LAST 5 DAYS","%rating% MISSING OR %rating% IS 0","%rating% GREATER 2 AND %rating% LESS 6","%rating% IS 4","%rating% IS 5","%mood% GREATER 0"};
        for(int i=0;i<7;++i) AppendMenuW(presets,MF_STRING,100+i,wide(names[i]).c_str());
        AppendMenuW(create,MF_POPUP,reinterpret_cast<UINT_PTR>(presets),L"预定义智能列表");
        AppendMenuW(create,MF_SEPARATOR,0,nullptr);
        using kind=modern_playlist::special_playlist;
        AppendMenuW(create,MF_STRING|(modern_playlist::special_enabled(kind::library)?MF_CHECKED:0),20,L"媒体库 (固定在最前)");
        AppendMenuW(create,MF_STRING|(modern_playlist::special_enabled(kind::history)?MF_CHECKED:0),21,L"播放记录");
        AppendMenuW(create,MF_STRING|(modern_playlist::special_enabled(kind::queue)?MF_CHECKED:0),22,L"播放队列 (只读)");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(create),index>=0?L"插入...":L"添加...");
        AppendMenuW(menu,MF_STRING,7,L"加载播放列表...");
        if(index>=0) {
            AppendMenuW(menu,MF_STRING,8,L"保存此播放列表...");
            AppendMenuW(menu,MF_STRING,9,L"创建副本");
            AppendMenuW(menu,flags(playlist_lock::filter_rename,true),2,L"重命名\tF2");
            AppendMenuW(menu,MF_STRING|(modern_playlist::can_close_playlist(index)?0:MF_GRAYED),3,L"移除");
            // One lock covers item edits, renaming and removal. Autoplaylists and
            // playlists another owner has locked cannot take it.
            AppendMenuW(menu,MF_STRING|(modern_playlist::user_locked(index)?MF_CHECKED:0)|
                (modern_playlist::can_toggle_user_lock(index)?0:MF_GRAYED),27,L"锁定");
            const bool pin=modern_playlist::library_pinned(index);
            AppendMenuW(menu,MF_STRING|((pin || index==0 || modern_playlist::library_pinned(index-1))?MF_GRAYED:0),4,L"左移");
            AppendMenuW(menu,MF_STRING|((pin || size_t(index+1)>=pm->get_playlist_count())?MF_GRAYED:0),5,L"右移");
            if(automatic) {
                auto client=autoplaylist_manager::get()->query_client(index);
                autoplaylist_client_v2::ptr v2; const bool supported=!client->service_query_t(v2) || v2->show_ui_available();
                AppendMenuW(menu,MF_STRING|((!supported || reserved)?MF_GRAYED:0),10,L"智能列表属性...");
            }
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING|(accepts_tracks(index)?0:MF_GRAYED),11,L"添加文件...");
            AppendMenuW(menu,MF_STRING|(accepts_tracks(index)?0:MF_GRAYED),12,L"添加文件夹...");
        }
        AppendMenuW(menu,MF_STRING,23,L"按名称排序播放列表 (升序)");
        AppendMenuW(menu,MF_STRING,24,L"按名称排序播放列表 (降序)");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,26,L"面板设置...");
        const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr); DestroyMenu(menu);
        if(!command) return;
        if(command==26) { edit_core_settings(3); return; }
        const auto resolved=index>=0?pm->find_playlist_by_guid(target):pfc::infinite_size;
        if(command>=20 && command<=22) { modern_playlist::toggle_special(command==20?kind::library:command==21?kind::history:kind::queue); return; }
        if(command==23 || command==24) { sort_playlists(command==23); return; }
        if(command==7) { standard_commands::main_load_playlist(); return; }
        if(index>=0 && resolved==pfc::infinite_size) return;
        auto before=resolved;
        if(command==1) {
            if(before==0 && modern_playlist::library_pinned(0)) before=1;
            auto created=pm->create_playlist_autoname(before); if(created!=pfc::infinite_size) pm->set_active_playlist(created); return;
        }
        if(command==6 || (command>=100 && command<107)) {
            autoplaylist_data data;
            if(command>=100) { data.name=names[command-100]; data.query=queries[command-100]; if(command==101) data.sort="%last_played%"; }
            else if(DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_AUTOPLAYLIST),hwnd_,autoplaylist_dialog,reinterpret_cast<LPARAM>(&data))!=IDOK) return;
            before=index>=0?pm->find_playlist_by_guid(target):pfc::infinite_size;
            if(index>=0 && before==pfc::infinite_size) return;
            create_autoplaylist(data,before); return;
        }
        if(resolved==pfc::infinite_size) return;
        if(command==2) rename_playlist(resolved);
        if(command==3) modern_playlist::close_playlist(resolved);
        if(command==27) modern_playlist::toggle_user_lock(resolved);
        if((command==4 || command==5) && !modern_playlist::library_pinned(resolved)) {
            const auto to=command==4?(resolved?resolved-1:resolved):std::min(resolved+1,pm->get_playlist_count()-1);
            if(!modern_playlist::library_pinned(to)) { auto order=modern_playlist::move_order(pm->get_playlist_count(),resolved,to); pm->reorder(order.data(),order.size()); }
        }
        if(command==9) {
            metadb_handle_list tracks; pm->playlist_get_all_items(resolved,tracks);
            pfc::string8 name; pm->playlist_get_name(resolved,name); name << " (副本)";
            const auto copy=pm->create_playlist(name.c_str(),name.length(),resolved+1);
            if(copy!=pfc::infinite_size) { pm->playlist_insert_items(copy,0,tracks,bit_array_false()); pm->set_active_playlist(copy); }
        }
        if(command==10 && !modern_playlist::special_reserved(resolved) && autoplaylist_manager::get()->is_client_present(resolved))
            autoplaylist_manager::get()->query_client(resolved)->show_ui(resolved);
        if(command==8 || ((command==11 || command==12) && accepts_tracks(resolved))) {
            // Host commands address the active playlist; activate the actual menu target first.
            pm->set_active_playlist(resolved);
            if(command==8) standard_commands::main_save_playlist();
            if(command==11) standard_commands::main_add_files();
            if(command==12) standard_commands::main_add_directory();
        }
    }
    bool group_pattern_locked() const {
        if(!grouping_.enabled || !grouping_.playlist_filter) return false;
        auto pm=playlist_manager::get(); if(active_>=pm->get_playlist_count()) return false;
        pfc::string8 name; pm->playlist_get_name(active_,name);
        return modern_playlist::assigned_pattern(grouping_.patterns,name.c_str(),group_list_order(grouping_.patterns))<grouping_.patterns.size();
    }
    void append_groups_menu(HMENU menu) {
        HMENU groups=CreatePopupMenu(), patterns=CreatePopupMenu();
        AppendMenuW(groups,MF_STRING|(grouping_.enabled?MF_CHECKED:0),300,L"启用分组");
        AppendMenuW(groups,MF_STRING|(grouping_.playlist_filter?MF_CHECKED:0),301,L"启用播放列表过滤");
        AppendMenuW(groups,MF_STRING|(grouping_.artwork_in_header?MF_CHECKED:0),312,L"在分组标题中显示封面");
        // An assigned pattern grays the choices, not the submenu, so the
        // pattern in use stays visible with its radio mark.
        const bool assigned=group_pattern_locked();
        const auto order=group_list_order(grouping_.patterns);
        UINT current=0;
        for(UINT position=0;position<order.size();++position) {
            const auto i=order[position]; if(i==grouping_.pattern) current=position;
            AppendMenuW(patterns,MF_STRING|(assigned?MF_GRAYED:0),400+i,wide(grouping_.patterns[i].label.c_str()).c_str());
        }
        if(!order.empty()) CheckMenuRadioItem(patterns,0,UINT(order.size()-1),current,MF_BYPOSITION);
        AppendMenuW(groups,MF_POPUP,reinterpret_cast<UINT_PTR>(patterns),L"更改分组模板");
        AppendMenuW(groups,MF_STRING|(grouping_.patterns[grouping_.pattern].show_headers && playlist_allows(active_,playlist_lock::filter_reorder)?0:MF_GRAYED),302,L"应用分组排序");
        AppendMenuW(groups,MF_STRING,303,L"全部折叠"); AppendMenuW(groups,MF_STRING,304,L"全部展开");
        AppendMenuW(groups,MF_STRING|(grouping_.collapse_default?MF_CHECKED:0),305,L"默认折叠分组");
        AppendMenuW(groups,MF_STRING|(grouping_.autocollapse?MF_CHECKED:0),306,L"自动折叠非播放分组");
        // Opens the inline template editor on Panel Settings -> Groups.
        AppendMenuW(groups,MF_SEPARATOR,0,nullptr);
        AppendMenuW(groups,MF_STRING,307,L"更多...");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(groups),L"分组");
    }
    void apply_group_sort() {
        if(pending_ || !grouping_.patterns[grouping_.pattern].show_headers || filtered_rows_.empty() || grouping_.patterns[grouping_.pattern].sort_order.empty()) return;
        auto pm=playlist_manager::get();
        if(pm->playlist_lock_get_filter_mask(active_)&playlist_lock::filter_reorder) { notice(L"此播放列表不允许重新排序。"); return; }
        metadb_handle_list tracks; for(auto i:filtered_rows_) tracks.add_item(items_[i]);
        std::vector<t_size> sorted(filtered_rows_.size());
        metadb_handle_list_helper::sort_by_format_get_order(tracks,sorted.data(),group_sort_,nullptr,1);
        auto order=modern_playlist::visible_order(filtered_rows_,sorted,items_.get_count());
        bool changed=false; for(size_t i=0;i<order.size();++i) if(order[i]!=i) { changed=true; break; }
        sort_column_=-1; InvalidateRect(header_,nullptr,FALSE);
        if(changed) { pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size()); }
    }
    bool group_command(int command) {
        if(!((command>=300 && command<=306) || command==312 || (command>=400 && command<464))) return false;
        if(command==312) {
            grouping_.artwork_in_header=!grouping_.artwork_in_header;
            refresh(false); return true;
        }
        search_reveal_=pfc::infinite_size;
        if(command>=400) {
            if(size_t(command-400)>=grouping_.patterns.size() || group_pattern_locked()) return true;
            grouping_.pattern=command-400; grouping_.enabled=true;
            // Manual selection stays in effect until the next filter evaluation.
            collapsed_.clear(); compile_columns();
            const bool filter=grouping_.playlist_filter; grouping_.playlist_filter=false; refresh(); grouping_.playlist_filter=filter;
            if(grouping_.active()) apply_group_sort();
            return true;
        }
        if(command==300) { grouping_.enabled=!grouping_.enabled; apply_filter_next_=grouping_.enabled; }
        else if(command==301) { grouping_.playlist_filter=!grouping_.playlist_filter; apply_filter_next_=true; }
        else if(command==302) { apply_group_sort(); return true; }
        else if(command==303 || command==304) {
            for(const auto& id:group_ids_) collapsed_[id]=command==303;
        } else if(command==305) { grouping_.collapse_default=!grouping_.collapse_default; collapsed_.clear(); }
        else if(command==306) { grouping_.autocollapse=!grouping_.autocollapse; collapsed_.clear(); }
        else return false;
        refresh(); return true;
    }
    void column_menu(POINT pt) {
        if (pending_) refresh();
        const auto menu_playlist=active_;
        const auto epoch=playlist_epoch_;
        capture_columns();
        // More... opens the Columns page on the header under the pointer.
        int clicked_column=-1;
        if(header_ && IsWindowVisible(header_)) {
            HDHITTESTINFO hit{}; hit.pt=pt; ScreenToClient(header_,&hit.pt);
            const int logical=int(SendMessageW(header_,HDM_HITTEST,0,reinterpret_cast<LPARAM>(&hit)));
            if(logical>=0 && (hit.flags&HHT_ONHEADER) && size_t(logical)<visible_columns_.size()) clicked_column=visible_columns_[logical];
        }
        HMENU menu=CreatePopupMenu(), column_items=CreatePopupMenu(), header_items=CreatePopupMenu();
        // Sort a menu-only index independently of the saved column layout, so
        // the order also survives changes to visibility and header placement.
        std::vector<std::wstring> column_names;
        for(const auto& c:columns_) column_names.push_back(wide(c.title.c_str()));
        std::vector<size_t> menu_order(columns_.size());
        std::iota(menu_order.begin(),menu_order.end(),size_t(0));
        std::stable_sort(menu_order.begin(),menu_order.end(),[&](size_t a,size_t b) {
            return natural_less(column_names[a],column_names[b]);
        });
        const auto enabled_count=std::count_if(columns_.begin(),columns_.end(),[](const auto& c){return c.visible;});
        for (const size_t i:menu_order) {
            const UINT flags=MF_STRING|(columns_[i].visible?MF_CHECKED:0)|
                (columns_[i].visible && enabled_count==1?MF_GRAYED:0);
            AppendMenuW(column_items,flags,100+i,column_names[i].c_str());
        }
        AppendMenuW(column_items,MF_SEPARATOR,0,nullptr);
        AppendMenuW(column_items,MF_STRING,15,L"更多...");
        AppendMenuW(header_items,MF_STRING|(show_header_?MF_CHECKED:0),11,L"显示列标题\tCtrl+T");
        AppendMenuW(header_items,MF_STRING|(headers_follow_alignment_?MF_CHECKED:0),12,L"列标题跟随内容对齐");
        AppendMenuW(header_items,MF_STRING|(show_tabs_?MF_CHECKED:0),5,L"显示播放列表标签\tTab");
        AppendMenuW(header_items,MF_STRING|(manager_bottom_?MF_CHECKED:0),14,L"播放列表管理器置于播放列表下方");
        AppendMenuW(menu,MF_STRING,8,L"面板设置...");
        AppendMenuW(menu,MF_STRING|(core_.extra_line?MF_CHECKED:0),7,L"显示附加行信息");
        AppendMenuW(menu,MF_STRING|(show_scrollbar_?MF_CHECKED:0),508,L"显示滚动条");
        AppendMenuW(menu,MF_STRING|(show_status_?MF_CHECKED:0),509,L"显示状态栏");
        AppendMenuW(menu,MF_STRING|(artwork_.enabled?MF_CHECKED:0),510,L"启用封面背景");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(play_control::get()->is_playing()?0:MF_GRAYED),9,L"显示正在播放项目");
        AppendMenuW(menu,MF_STRING,16,L"刷新封面\tF5");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(header_items),L"标题栏");
        append_search_menu(menu); append_groups_menu(menu);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(column_items),L"列");
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr);
        DestroyMenu(menu);
        if (!command || destroying_) return;
        if(search_command(command)) return; // Search remains available during playlist updates.
        if (epoch!=playlist_epoch_ || menu_playlist!=active_ || pending_) return;
        if(command==510) { toggle_background(); return; }
        if(command==509) { toggle_status(); return; }
        if(command==508) { show_scrollbar_=!show_scrollbar_; theme(); fit_columns(); return; }
        if(command==307) { edit_core_settings(2); return; }
        if(group_command(command)) return;
        if (command==5) { toggle_tabs(); return; }
        if (command==14) {
            cancel_tab_drag(); hovered_tab_=-1;
            SendMessageW(hwnd_,WM_SETREDRAW,FALSE,0);
            manager_bottom_=!manager_bottom_; clear_surface(); layout();
            SendMessageW(hwnd_,WM_SETREDRAW,TRUE,0);
            invalidate_all();
            RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
            return;
        }
        if (command==7) { core_.extra_line=!core_.extra_line; theme(); layout(); return; }
        if (command==16) { refresh_artwork(); return; }
        if (command==8) { edit_core_settings(); return; }
        if (command==15) { edit_core_settings(1,clicked_column); return; }
        if (command==9) { show_now_playing(true); return; }
        if (command==11) { toggle_header(); return; }
        if (command==12) { headers_follow_alignment_=!headers_follow_alignment_; InvalidateRect(header_,nullptr,FALSE); return; }
        if (command>=100 && command<200 && static_cast<size_t>(command-100)<columns_.size()) {
            auto& c=columns_[command-100];
            if (!c.visible || enabled_count>1) {
                c.visible=!c.visible;
                normalize_percents();
            }
        } else return;
        compile_columns(); make_columns(); save_playlist_columns(); refresh();
    }
    // Transfer pixels between neighbours in DISPLAY order as one viewport update.
    void set_header_pair_widths(int left_width,int right_width) {
        const bool fitting=fitting_columns_; fitting_columns_=true;
        modern_playlist::resize_playlist_column_pair(list_,header_resize_left_,header_resize_right_,left_width,right_width);
        fitting_columns_=fitting;
    }
    void begin_header_resize(int logical,int x) {
        cancel_header_drag();
        std::vector<int> order(visible_columns_.size());
        if(!Header_GetOrderArray(header_,int(order.size()),order.data())) return;
        const auto found=std::find(order.begin(),order.end(),logical);
        // The outer edge (including a single visible column) has no neighbour.
        if(found==order.end() || found+1==order.end()) return;
        header_resize_left_=logical; header_resize_right_=*(found+1); header_resize_x_=x;
        header_resize_left_width_=ListView_GetColumnWidth(list_,header_resize_left_);
        header_resize_right_width_=ListView_GetColumnWidth(list_,header_resize_right_);
        SetFocus(header_); SetCapture(header_);
    }
    void move_header_resize(int x) {
        const int left_minimum=column_minimum_width(header_resize_left_), right_minimum=column_minimum_width(header_resize_right_);
        const int delta=std::clamp(x-header_resize_x_,
            std::min(left_minimum,header_resize_left_width_)-header_resize_left_width_,
            header_resize_right_width_-std::min(right_minimum,header_resize_right_width_));
        set_header_pair_widths(header_resize_left_width_+delta,header_resize_right_width_-delta);
    }
    void finish_header_resize(bool commit) {
        if(header_resize_left_<0) return;
        const bool changed=ListView_GetColumnWidth(list_,header_resize_left_)!=header_resize_left_width_;
        if(!commit) set_header_pair_widths(header_resize_left_width_,header_resize_right_width_);
        header_resize_left_=header_resize_right_=-1;
        if(GetCapture()==header_) ReleaseCapture();
        if(!commit || !changed) return;
        for(size_t i=0;i<visible_columns_.size();++i) {
            const int width=ListView_GetColumnWidth(list_,int(i));
            columns_[visible_columns_[i]].width=std::clamp(MulDiv(width,96,scale(96)),32,4000);
        }
        normalize_percents(); save_playlist_columns();
        // Do not fit here: rounding/reapplying ratios would move other dividers.
    }
    void cancel_header_drag() {
        finish_header_resize(false);
        header_candidate_=header_before_=-1;
        if(header_ghost_) { DestroyWindow(header_ghost_); header_ghost_=nullptr; }
        if(header_bitmap_) { DeleteObject(header_bitmap_); header_bitmap_=nullptr; }
        if(header_ && GetCapture()==header_) ReleaseCapture();
        if(header_) InvalidateRect(header_,nullptr,FALSE);
    }
    static LRESULT CALLBACK column_ghost_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
        if(msg==WM_NCCREATE) SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self=reinterpret_cast<playlist_view*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if(msg==WM_NCHITTEST) return HTTRANSPARENT;
        if(msg==WM_ERASEBKGND) return 1;
        if(msg==WM_PAINT && self) {
            PAINTSTRUCT ps{}; auto dc=BeginPaint(wnd,&ps); auto memory=CreateCompatibleDC(dc);
            if(memory && self->header_bitmap_) {
                auto old=SelectObject(memory,self->header_bitmap_);
                BitBlt(dc,0,0,self->header_ghost_size_.cx,self->header_ghost_size_.cy,memory,0,0,SRCCOPY); SelectObject(memory,old);
            }
            if(memory) DeleteDC(memory); EndPaint(wnd,&ps); return 0;
        }
        return DefWindowProcW(wnd,msg,wp,lp);
    }
    void move_header_drag(POINT point) {
        std::vector<int> order(visible_columns_.size()); Header_GetOrderArray(header_,int(order.size()),order.data());
        header_before_=int(order.size());
        for(size_t i=0;i<order.size();++i) {
            RECT rect{}; Header_GetItemRect(header_,order[i],&rect);
            if(point.x<(rect.left+rect.right)/2) { header_before_=int(i); break; }
        }
        POINT screen=point; ClientToScreen(header_,&screen);
        SetWindowPos(header_ghost_,HWND_TOPMOST,screen.x-header_ghost_size_.cx/2,screen.y-header_ghost_size_.cy/2,
            0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(header_,nullptr,FALSE);
    }
    void start_header_drag(POINT point) {
        RECT rect{}; if(header_candidate_<0 || !Header_GetItemRect(header_,header_candidate_,&rect)) return;
        header_ghost_size_={rect.right-rect.left,rect.bottom-rect.top};
        auto dc=GetDC(header_); auto memory=CreateCompatibleDC(dc);
        header_bitmap_=CreateCompatibleBitmap(dc,std::max(1L,header_ghost_size_.cx),std::max(1L,header_ghost_size_.cy));
        if(!memory || !header_bitmap_) { if(memory) DeleteDC(memory); ReleaseDC(header_,dc); cancel_header_drag(); return; }
        auto old=SelectObject(memory,header_bitmap_); RECT bounds{0,0,header_ghost_size_.cx,header_ghost_size_.cy};
        fill(memory,bounds,blend(current_palette().header,RGB(0,0,0),18));
        LOGFONTW lf{}; GetObjectW(bold_font_,sizeof(lf),&lf); lf.lfQuality=ANTIALIASED_QUALITY;
        auto font=CreateFontIndirectW(&lf); auto old_font=SelectObject(memory,font?font:bold_font_);
        SetBkMode(memory,TRANSPARENT); SetTextColor(memory,current_palette().text);
        const auto& col=columns_[visible_columns_[header_candidate_]]; auto title=wide(col.title.c_str());
        bounds.left+=scale(6); bounds.right-=scale(6);
        const int alignment=headers_follow_alignment_?col.align:LVCFMT_CENTER;
        DrawTextW(memory,title.c_str(),int(title.size()),&bounds,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|
            (alignment==LVCFMT_LEFT?DT_LEFT:alignment==LVCFMT_RIGHT?DT_RIGHT:DT_CENTER));
        SelectObject(memory,old_font); if(font) DeleteObject(font);
        SelectObject(memory,old); DeleteDC(memory); ReleaseDC(header_,dc);
        WNDCLASSW wc{}; wc.lpfnWndProc=column_ghost_proc; wc.hInstance=core_api::get_my_instance(); wc.lpszClassName=L"foo_modernplaylist.column_ghost"; RegisterClassW(&wc);
        header_ghost_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,wc.lpszClassName,L"",WS_POPUP,
            0,0,header_ghost_size_.cx,header_ghost_size_.cy,hwnd_,nullptr,wc.hInstance,this);
        if(!header_ghost_) { cancel_header_drag(); return; }
        SetLayeredWindowAttributes(header_ghost_,0,225,LWA_ALPHA); SetCapture(header_); move_header_drag(point);
    }
    void finish_header_drag(POINT point) {
        std::vector<int> order(visible_columns_.size()); Header_GetOrderArray(header_,int(order.size()),order.data());
        const auto found=std::find(order.begin(),order.end(),header_candidate_);
        const int before=header_before_; RECT bounds{}; GetClientRect(header_,&bounds);
        const bool valid=found!=order.end() && before>=0 && PtInRect(&bounds,point);
        if(valid) {
            const int from=int(found-order.begin()),item=*found;
            order.erase(found); order.insert(order.begin()+std::clamp(before-(before>from?1:0),0,int(order.size())),item);
        }
        cancel_header_drag();
        if(valid) { ListView_SetColumnOrderArray(list_,int(order.size()),order.data()); save_playlist_columns(); fit_columns(); }
    }
    static LRESULT CALLBACK child_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclass_id, DWORD_PTR data) {
        auto* self=reinterpret_cast<playlist_view*>(data);
        // Never query a child control to identify it here: SendMessage would
        // re-enter this subclass (LVM_GETHEADER caused the exit stack overflow).
        if (msg==WM_NCDESTROY) {
            RemoveWindowSubclass(wnd,child_proc,subclass_id);
            self->forget_child(wnd);
            return DefSubclassProc(wnd,msg,wp,lp);
        }
        if (self->destroying_) return DefSubclassProc(wnd,msg,wp,lp);
        try {
            if ((msg==WM_CONTEXTMENU || msg==WM_RBUTTONDOWN || msg==WM_RBUTTONUP || msg==WM_RBUTTONDBLCLK) && self->is_layout_editing()) {
                // Own the right-click gesture before native controls or the
                // viewport can show a menu, change selection, or take focus.
                if (msg==WM_RBUTTONDOWN || msg==WM_RBUTTONDBLCLK) return 0;
                if (msg==WM_RBUTTONUP) {
                    POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    ClientToScreen(wnd,&point); lp=MAKELPARAM(point.x,point.y);
                }
                return SendMessageW(self->hwnd_,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(self->hwnd_),lp);
            }
            if(wnd==self->status_ && msg==WM_LBUTTONDBLCLK) { self->reveal_playing_track(); return 0; }
            // The status row has no context menu outside Default UI layout editing.
            if((wnd==self->status_ || wnd==self->sort_az_ || wnd==self->sort_za_) &&
                (msg==WM_CONTEXTMENU || msg==WM_RBUTTONDOWN || msg==WM_RBUTTONUP || msg==WM_RBUTTONDBLCLK)) return 0;
            if(wnd==self->search_action_) {
                if(msg==WM_ERASEBKGND) return 1;
                if(msg==WM_MOUSEMOVE) {
                    self->search_action_hovered_=true;
                    TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,wnd,0}; TrackMouseEvent(&track);
                }
                if(msg==WM_MOUSELEAVE || msg==WM_CANCELMODE || msg==WM_SHOWWINDOW) self->search_action_hovered_=false;
                if(msg==WM_MOUSEMOVE || msg==WM_MOUSELEAVE || msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==WM_CANCELMODE)
                    InvalidateRect(wnd,nullptr,FALSE);
            }
            if(self->is_manager_button(wnd)) {
                // Keep native button capture/activation, but distinguish mouse
                // focus from keyboard focus so a click cannot leave a focus box.
                if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK) self->mouse_focused_manager_=wnd;
                if((msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN || msg==WM_KILLFOCUS) && self->mouse_focused_manager_==wnd)
                    self->mouse_focused_manager_=nullptr;
                if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK || msg==WM_LBUTTONUP ||
                    msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN || msg==WM_KEYUP || msg==WM_SETFOCUS || msg==WM_KILLFOCUS ||
                    msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || msg==WM_ENABLE || msg==WM_UPDATEUISTATE)
                    InvalidateRect(wnd,nullptr,FALSE);
            }
            if(wnd==self->search_field_list_ || wnd==self->search_scope_list_) {
                if(msg==WM_WINDOWPOSCHANGING) self->position_search_popup(wnd,*reinterpret_cast<WINDOWPOS*>(lp));
                if(msg==WM_NCPAINT) { self->paint_search_popup_frame(wnd); return 0; }
                // Combo dropdowns run a native modal loop, outside SDK filters.
                if(msg==WM_KEYDOWN) { MSG key{}; key.hwnd=wnd; key.message=msg; key.wParam=wp; key.lParam=lp;
                    if(self->pretranslate_message(&key)) return 0;
                }
            }
            // These controls need only edit-mode routing and popup frame painting.
            // Preserve their native input behavior (especially the combo lists).
            if (wnd==self->notice_ || wnd==self->scrollbar_ || wnd==self->search_field_list_ || wnd==self->search_scope_list_)
                return DefSubclassProc(wnd,msg,wp,lp);
            if(wnd==self->ungrouped_view_ || wnd==self->grouped_view_) {
                if(msg==WM_ERASEBKGND) return 1;
                if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK) self->mouse_focused_grouping_=wnd;
                if((msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN || msg==WM_KILLFOCUS) && self->mouse_focused_grouping_==wnd)
                    self->mouse_focused_grouping_=nullptr;
                if(msg==WM_MOUSEMOVE) self->hovered_grouping_button_=wnd;
                if(msg==WM_MOUSELEAVE && self->hovered_grouping_button_==wnd) self->hovered_grouping_button_=nullptr;
                if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK || msg==WM_LBUTTONUP ||
                    msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN || msg==WM_KEYUP || msg==WM_SETFOCUS || msg==WM_KILLFOCUS ||
                    msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || msg==WM_ENABLE || msg==WM_UPDATEUISTATE ||
                    msg==WM_MOUSEMOVE || msg==WM_MOUSELEAVE) InvalidateRect(wnd,nullptr,FALSE);
            }
            if(self->header_ghost_ && (msg==WM_KILLFOCUS || (msg==WM_SHOWWINDOW && !wp))) self->cancel_header_drag();
            if(wnd==self->header_) {
                if(self->header_resize_left_>=0) {
                    if(msg==WM_MOUSEMOVE) { self->move_header_resize(GET_X_LPARAM(lp)); return 0; }
                    if(msg==WM_LBUTTONUP) {
                        self->move_header_resize(GET_X_LPARAM(lp)); self->finish_header_resize(true); return 0;
                    }
                    if(msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || msg==WM_KILLFOCUS ||
                        (msg==WM_SHOWWINDOW && !wp) || (msg==WM_KEYDOWN && wp==VK_ESCAPE)) {
                        self->finish_header_resize(false); return 0;
                    }
                }
                if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONDBLCLK) {
                    HDHITTESTINFO hit{}; hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    const int logical=int(SendMessageW(wnd,HDM_HITTEST,0,reinterpret_cast<LPARAM>(&hit)));
                    if(hit.flags&(HHT_ONDIVIDER|HHT_ONDIVOPEN)) {
                        self->begin_header_resize(logical,hit.pt.x); return 0;
                    }
                    self->header_candidate_=(hit.flags&HHT_ONHEADER)?logical:-1; self->header_start_=hit.pt;
                }
                if(msg==WM_MOUSEMOVE && (wp&MK_LBUTTON) && self->header_candidate_>=0) {
                    POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    if(!self->header_ghost_ && (std::abs(point.x-self->header_start_.x)>=GetSystemMetrics(SM_CXDRAG) ||
                        std::abs(point.y-self->header_start_.y)>=GetSystemMetrics(SM_CYDRAG))) {
                        const int candidate=self->header_candidate_;
                        DefSubclassProc(wnd,WM_CANCELMODE,0,0); self->header_candidate_=candidate;
                        self->start_header_drag(point);
                    }
                    if(self->header_ghost_) { self->move_header_drag(point); return 0; }
                }
                if(msg==WM_LBUTTONUP) {
                    if(self->header_ghost_) { self->finish_header_drag({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}); return 0; }
                    self->header_candidate_=-1;
                }
                if(msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || (msg==WM_KEYDOWN && wp==VK_ESCAPE)) self->cancel_header_drag();
            }
            if (msg==HDM_LAYOUT && wnd==self->header_) {
                LRESULT result=DefSubclassProc(wnd,msg,wp,lp);
                auto* layout=reinterpret_cast<HDLAYOUT*>(lp);
                const int height=self->show_header_?self->header_pixels_:0;
                layout->pwpos->cy=height; layout->prc->top=layout->pwpos->y+height;
                return result;
            }
            if(wnd==self->tabs_) {
                // The shared MSAA adapter queries this read-only list-message contract.
                if(msg==WM_GETOBJECT && static_cast<LONG>(lp)==OBJID_CLIENT) {
                    if(!self->manager_accessible_) self->manager_accessible_.Attach(new modern_playlist::viewport_accessibility(wnd,true));
                    return LresultFromObject(IID_IAccessible,wp,self->manager_accessible_.Get());
                }
                if(msg==LVM_GETITEMCOUNT) return self->pending_?0:self->tab_names_.size();
                if(msg==LVM_GETITEMTEXTW) {
                    auto* item=reinterpret_cast<LVITEMW*>(lp);
                    if(item && item->pszText && item->cchTextMax>0 && wp<self->tab_names_.size()) {
                        lstrcpynW(item->pszText,self->tab_names_[wp].c_str(),item->cchTextMax); return lstrlenW(item->pszText);
                    }
                    return 0;
                }
                if(msg==LVM_GETITEMRECT) return self->tab_rect(int(wp),reinterpret_cast<RECT*>(lp));
                if(msg==LVM_HITTEST) { auto* hit=reinterpret_cast<LVHITTESTINFO*>(lp); return hit->iItem=self->tab_hit(hit->pt); }
                if(msg==LVM_GETITEMSTATE) return wp==self->active_?(LVIS_SELECTED|LVIS_FOCUSED)&lp:0;
                if(msg==LVM_GETNEXTITEM) return self->active_<self->tab_names_.size() && int(wp)<int(self->active_)?LRESULT(self->active_):-1;
                if(msg==modern_playlist::viewport_access_action) {
                    if(self->pending_ || wp>=self->tab_names_.size()) return FALSE;
                    SetFocus(wnd); playlist_manager::get()->set_active_playlist(wp); return TRUE;
                }
            }
            if(msg==WM_CONTEXTMENU && (wnd==self->tabs_ || wnd==self->add_ || wnd==self->tab_left_ || wnd==self->tab_right_ || wnd==self->reveal_active_))
                return SendMessageW(self->hwnd_,msg,reinterpret_cast<WPARAM>(wnd),lp);
            if (wnd==self->list_ && msg==WM_SIZE && !self->fitting_columns_ && !self->rebuilding_)
                PostMessageW(self->hwnd_,fit_columns_message,0,0);
            if (wnd==self->list_ && msg==WM_NOTIFY) {
                auto* header=reinterpret_cast<NMHDR*>(lp);
                if (header->hwndFrom==self->header_) {
                    if (header->code==NM_CUSTOMDRAW) return self->draw_header(reinterpret_cast<NMCUSTOMDRAW*>(lp));
                    if (header->code==HDN_ITEMCHANGINGW || header->code==HDN_ITEMCHANGINGA) {
                        auto* change=reinterpret_cast<NMHEADERW*>(lp);
                        if (!self->fitting_columns_ && !self->rebuilding_ && change->pitem &&
                            (change->pitem->mask&HDI_WIDTH) && change->pitem->cxy<self->column_minimum_width(change->iItem)) return TRUE;
                    }
                    // Divider gestures are handled above as adjacent pairs.
                    if(header->code==HDN_BEGINTRACKW || header->code==HDN_BEGINTRACKA) return TRUE;
                    if (header->code==HDN_ENDDRAG) {
                        PostMessageW(self->hwnd_,header_order_message,0,0);
                    }
                }
            }
            // Owner drawing covers the complete face, including its hover fill.
            if(msg==WM_ERASEBKGND && (self->is_manager_button(wnd) || wnd==self->status_)) return 1;
            if (wnd==self->tabs_ && msg==WM_PAINT) { self->paint_tabs(); return 0; }
            if (wnd==self->tabs_ && msg==WM_ERASEBKGND) return 1;
            if (msg==WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)) {
                self->zoom(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
            }
            const bool plain_tab=wp==VK_TAB && !(GetKeyState(VK_CONTROL)&0x8000) &&
                !(GetKeyState(VK_SHIFT)&0x8000) && !(GetKeyState(VK_MENU)&0x8000) &&
                wnd!=self->search_ && wnd!=self->search_field_ && wnd!=self->search_scope_ &&
                wnd!=self->ungrouped_view_ && wnd!=self->grouped_view_;
            if(msg==WM_GETDLGCODE && plain_tab) return DefSubclassProc(wnd,msg,wp,lp)|DLGC_WANTTAB;
            if (msg==WM_KEYDOWN) {
                if(self->header_ghost_) { if(wp==VK_ESCAPE) self->cancel_header_drag(); return 0; }
                if(wnd==self->list_ && SendMessageW(wnd,modern_playlist::viewport_scrollbar_capture,wp,0)) return 0;
                // Captured cell edits (and track drags) own their keyboard gesture.
                if(wnd==self->list_ && GetCapture()==wnd) {
                    if(wp==VK_ESCAPE) SendMessageW(wnd,WM_CANCELMODE,0,0);
                    return 0;
                }
                const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0, alt=(GetKeyState(VK_MENU)&0x8000)!=0;
                const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
                const bool ctrl=control && !shift && !alt;
                if(plain_tab) { if(!(lp&(LPARAM(1)<<30))) self->toggle_tabs(); return 0; }
                if(!control && !shift && !alt && wp==VK_F5) { self->refresh_artwork(); return 0; }
                if(!control && !shift && !alt && wp==VK_F2 && wnd!=self->search_ && wnd!=self->search_field_ && wnd!=self->search_scope_ &&
                    wnd!=self->ungrouped_view_ && wnd!=self->grouped_view_) {
                    self->rename_playlist(playlist_manager::get()->get_active_playlist()); return 0;
                }
                if (ctrl && wp=='F') { self->focus_search(); return 0; }
                if (ctrl && wp=='T') { self->toggle_header(); return 0; }
                if (ctrl && wp=='N') { self->new_playlist(); return 0; }
                if (wp==VK_ESCAPE && self->drag_tab_>=0) { self->cancel_tab_drag(); return 0; }
                if (wp==VK_ESCAPE && wnd==self->list_ && !self->incremental_.text.empty()) { self->clear_incremental(); return 0; }
                if (wp==VK_ESCAPE) {
                    if (wnd==self->search_ || (wnd==self->list_ && GetWindowTextLengthW(self->search_) > 0)) {
                        self->clear_search(); return 0;
                    }
                }
                if (wnd==self->list_ && !self->pending_) {
                    if(wp==VK_SPACE && !ctrl && !self->incremental_.text.empty() && !self->incremental_.expired(GetTickCount64())) return 0;
                    if (!control && !shift && !alt && wp==VK_RETURN) { self->play(); return 0; }
                    if (!control && !shift && !alt && wp==VK_DELETE) { self->remove_tracks(); return 0; }
                    if (ctrl && wp=='A') { ListView_SetItemState(wnd,-1,LVIS_SELECTED,LVIS_SELECTED); return 0; }
                    if (ctrl && wp=='Z') { playlist_manager::get()->activeplaylist_undo_restore(); return 0; }
                    if (ctrl && wp=='Y') { playlist_manager::get()->activeplaylist_redo_restore(); return 0; }
                    if (ctrl && (wp=='C' || wp=='X')) { self->copy_tracks(wp=='X'); return 0; }
                    if (ctrl && wp=='V') { self->paste_tracks(); return 0; }
                }
            }
            if(msg==WM_MBUTTONUP && (wnd==self->list_ || wnd==self->search_)) { self->toggle_search(); return 0; }
            if (wnd==self->list_) {
                if(msg==WM_KILLFOCUS) self->clear_incremental();
                if((msg==WM_CHAR || msg==WM_SYSKEYDOWN) && SendMessageW(wnd,modern_playlist::viewport_scrollbar_capture,0,0)) return 0;
                if(msg==WM_CHAR && wp==VK_TAB) return 0;
                if(msg==WM_CHAR && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)) {
                    if(wp==VK_BACK || (wp>=32 && wp!=127)) { self->incremental_input(wchar_t(wp)); return 0; }
                }
                if (msg==WM_SYSKEYDOWN && (wp==VK_UP || wp==VK_DOWN)) { self->move_tracks(wp==VK_UP?-1:1); return 0; }
            }
            if (wnd==self->tabs_) {
                if(msg==WM_GETDLGCODE) return DLGC_WANTARROWS|DLGC_WANTCHARS;
                // The rename box is the strip's child: its colors and growth.
                if(msg==WM_CTLCOLOREDIT && self->rename_edit_ && reinterpret_cast<HWND>(lp)==self->rename_edit_) {
                    const auto& pal=self->current_palette();
                    SetTextColor(reinterpret_cast<HDC>(wp),pal.text); SetBkColor(reinterpret_cast<HDC>(wp),pal.search_bg);
                    return reinterpret_cast<LRESULT>(self->edit_background_);
                }
                if(msg==WM_COMMAND && self->rename_edit_ && reinterpret_cast<HWND>(lp)==self->rename_edit_) {
                    if(HIWORD(wp)==EN_CHANGE) self->position_rename();
                    return 0;
                }
                // The first click activated the tab; a double-click renames it.
                // A click that closed or dragged a tab never leads to a rename.
                if(msg==WM_LBUTTONDBLCLK) {
                    self->end_rename(true);
                    if(self->pending_) self->refresh();
                    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const int hit=self->tab_hit(pt); RECT tab{};
                    if(hit>=0 && self->tab_rect(hit,&tab) && playlist_manager_v5::get()->playlist_get_guid(hit)==self->clicked_tab_) {
                        auto icon=self->tab_icon_rect(tab); InflateRect(&icon,self->scale(2),0);
                        if(!(self->tab_has_close(hit,self->playing_playlist()) && PtInRect(&icon,pt))) self->rename_playlist(t_size(hit));
                    }
                    self->clicked_tab_={};
                    return 0;
                }
                if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) InvalidateRect(wnd,nullptr,FALSE);
                if(msg==WM_KEYDOWN && (wp==VK_LEFT || wp==VK_RIGHT || wp==VK_HOME || wp==VK_END)) {
                    auto pm=playlist_manager::get(); int count=int(pm->get_playlist_count());
                    if(count) { int next=wp==VK_HOME?0:wp==VK_END?count-1:int(pm->get_active_playlist())+(wp==VK_LEFT?-1:1); pm->set_active_playlist(std::clamp(next,0,count-1)); }
                    return 0;
                }
                if(msg==WM_LBUTTONDOWN) {
                    // Finish a rename first, so the click hits the refreshed tabs.
                    self->end_rename(true);
                    if(self->pending_) self->refresh();
                    SetFocus(wnd); POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    self->clicked_tab_={};
                    self->drag_tab_=self->tab_hit(pt); self->drag_epoch_=self->playlist_epoch_;
                    self->drag_tab_start_=pt; self->drag_tab_moved_=false;
                    if(self->drag_tab_>=0) self->drag_grab_={pt.x-self->manager_.left(self->drag_tab_),pt.y};
                    if(self->drag_tab_>=0) SetCapture(wnd);
                    return 0;
                }
                if(msg==WM_MOUSEMOVE && self->drag_tab_>=0 && (wp&MK_LBUTTON)) {
                    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    if(self->drag_epoch_!=self->playlist_epoch_) { self->cancel_tab_drag(); return 0; }
                    if(!self->drag_tab_moved_ && !modern_playlist::library_pinned(self->drag_tab_) &&
                        (std::abs(pt.x-self->drag_tab_start_.x)>=self->scale(4) || std::abs(pt.y-self->drag_tab_start_.y)>=self->scale(4))) {
                        self->drag_tab_moved_=true; self->start_drag_ghost(pt);
                        SetTimer(self->hwnd_,5,120,nullptr);
                    }
                    if(self->drag_tab_moved_) {
                        self->drag_before_=self->manager_.insertion(pt.x);
                        if(modern_playlist::library_pinned(0)) self->drag_before_=std::max(1,self->drag_before_);
                        self->move_drag_ghost(pt); InvalidateRect(wnd,nullptr,FALSE);
                    }
                    return 0;
                }
                if(msg==WM_LBUTTONUP && self->drag_tab_>=0) {
                    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const int hit=self->tab_hit(pt),from=self->drag_tab_;
                    const bool moved=self->drag_tab_moved_,valid=self->drag_epoch_==self->playlist_epoch_;
                    const int before=self->manager_.insertion(pt.x);
                    self->cancel_tab_drag();
                    if(!valid) return 0;
                    auto pm=playlist_manager::get();
                    if(moved && pt.y>=0 && pt.y<self->tab_pixels_ && pt.x>=0 && pt.x<self->manager_.viewport) {
                        int to=modern_playlist::manager_drop_destination(from,before,int(pm->get_playlist_count()),modern_playlist::library_pinned(0)?0:-1);
                        if(to>=0 && to!=from) { auto order=modern_playlist::move_order(pm->get_playlist_count(),from,to); pm->reorder(order.data(),order.size()); }
                    } else if(!moved && hit==from) {
                        RECT tab{}; self->tab_rect(hit,&tab);
                        // The close target spans the tab's height and its padding.
                        auto icon=self->tab_icon_rect(tab); InflateRect(&icon,self->scale(2),0);
                        if(self->tab_has_close(hit,self->playing_playlist()) && PtInRect(&icon,pt))
                            modern_playlist::close_playlist(hit);
                        else { self->clicked_tab_=playlist_manager_v5::get()->playlist_get_guid(hit); pm->set_active_playlist(hit); }
                    }
                    return 0;
                }
                if(msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || (msg==WM_KEYDOWN && wp==VK_ESCAPE)) { self->cancel_tab_drag(); return 0; }
                if(msg==WM_MOUSEWHEEL || msg==WM_MOUSEHWHEEL) { self->on_tabs_wheel(GET_WHEEL_DELTA_WPARAM(wp)*(msg==WM_MOUSEWHEEL?1:-1)); return 0; }
            }
            if (msg == WM_MOUSEMOVE) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd, 0};
                TrackMouseEvent(&tme);
                self->update_hover();
            } else if (msg == WM_MOUSELEAVE) {
                self->update_hover();
            }
            if(wnd==self->search_field_ || wnd==self->search_scope_) {
                if(msg==WM_PAINT) { self->paint_search_selector(wnd); return 0; }
                if(msg==WM_PRINTCLIENT) { RECT rect{}; GetClientRect(wnd,&rect); self->draw_search_selector(reinterpret_cast<HDC>(wp),wnd,rect); return 0; }
                if(msg==WM_ERASEBKGND) return 1;
                if(msg==WM_LBUTTONDOWN) self->search_selector_mouse_=true;
                if(msg==WM_KEYDOWN) self->search_selector_mouse_=false;
                if(msg==WM_MOUSEMOVE) self->hovered_search_selector_=wnd;
                if(msg==WM_MOUSELEAVE && self->hovered_search_selector_==wnd) self->hovered_search_selector_=nullptr;
                if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==CB_SETCURSEL || msg==CB_SHOWDROPDOWN ||
                    msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP || msg==WM_KEYDOWN || msg==WM_KEYUP ||
                    msg==WM_MOUSEMOVE || msg==WM_MOUSELEAVE || msg==WM_UPDATEUISTATE) {
                    // Combo boxes can paint directly during input, outside WM_PAINT.
                    // Restore the complete face after that native processing finishes.
                    const auto result=DefSubclassProc(wnd,msg,wp,lp);
                    if(!self->destroying_ && IsWindow(wnd)) RedrawWindow(wnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_NOERASE);
                    return result;
                }
            }
            if (wnd == self->search_) {
                if (msg == WM_GETDLGCODE) {
                    LRESULT res = DefSubclassProc(wnd, msg, wp, lp);
                    if (wp == VK_ESCAPE || (lp && reinterpret_cast<MSG*>(lp)->message == WM_KEYDOWN && reinterpret_cast<MSG*>(lp)->wParam == VK_ESCAPE)) {
                        return res | DLGC_WANTALLKEYS;
                    }
                    return res;
                }
                if (msg == WM_CHAR && wp == VK_ESCAPE) return 0;
                if (msg == WM_PAINT) {
                    // Let the native edit paint every focused state, including
                    // uncommitted IME composition when its text length is still zero.
                    if (GetFocus()!=wnd && GetWindowTextLengthW(wnd) == 0) {
                        PAINTSTRUCT ps{};
                        HDC dc = BeginPaint(wnd, &ps);
                        if (dc) {
                            int saved = SaveDC(dc);
                            const auto& pal = self->current_palette();
                            RECT client{};
                            GetClientRect(wnd, &client);
                            self->paint_search_face(dc,wnd,client);
                            SelectObject(dc, self->default_font_);
                            SetTextColor(dc, pal.muted);
                            SetBkMode(dc, TRANSPARENT);
                            RECT text_rect = client;
                            text_rect.left += self->scale(2);
                            text_rect.right -= self->scale(2);
                            DrawTextW(dc, search_placeholder, -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                            RestoreDC(dc, saved);
                            EndPaint(wnd, &ps);
                        }
                        return 0;
                    }
                    return DefSubclassProc(wnd, msg, wp, lp);
                }
                if (msg == WM_ERASEBKGND) {
                    if (GetFocus()!=wnd && GetWindowTextLengthW(wnd) == 0) return 1;
                }
                if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == EM_SETSEL) {
                    LRESULT lr = DefSubclassProc(wnd, msg, wp, lp);
                    if (GetWindowTextLengthW(wnd) == 0) {
                        InvalidateRect(wnd, nullptr, FALSE);
                    }
                    return lr;
                }
                if (msg == WM_SETTEXT) {
                    LRESULT lr = DefSubclassProc(wnd, msg, wp, lp);
                    InvalidateRect(wnd, nullptr, TRUE);
                    return lr;
                }
            }
        } catch (const std::exception& e) { console::error(e.what()); }
        return DefSubclassProc(wnd,msg,wp,lp);
    }
    static LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self=reinterpret_cast<playlist_view*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if (msg==WM_NCCREATE) {
            self=static_cast<playlist_view*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd_=wnd; SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(wnd,msg,wp,lp);
        try { return self->message(msg,wp,lp); }
        catch (const std::exception& e) { self->rebuilding_=false; console::error(e.what()); return 0; }
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg==WM_DESTROY) { begin_destroy(); return 0; }
        if (msg==WM_NCDESTROY) {
            const auto wnd=hwnd_;
            hwnd_=tabs_=search_=list_=header_=notice_=add_=tab_left_=tab_right_=search_field_=search_scope_=search_action_=ungrouped_view_=grouped_view_=grouping_tooltip_=status_=sort_az_=sort_za_=reveal_active_=scrollbar_=search_field_list_=search_scope_list_=nullptr;
            search_themed_=false; themed_dark_=-1;
            SetWindowLongPtrW(wnd,GWLP_USERDATA,0);
            return DefWindowProcW(wnd,msg,wp,lp);
        }
        if (destroying_) return DefWindowProcW(hwnd_,msg,wp,lp);
        switch(msg) {
        case WM_CREATE: {
            auto instance=core_api::get_my_instance();
            // Clip children: the strip's buffered paint must not cover the rename box.
            tabs_=CreateWindowExW(0,L"STATIC",L"播放列表管理器",WS_CHILD|WS_TABSTOP|WS_CLIPCHILDREN|SS_NOTIFY,0,0,0,0,hwnd_,nullptr,instance,nullptr);
            add_=CreateWindowExW(0,L"BUTTON",L"+",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(10),instance,nullptr);
            reveal_active_=CreateWindowExW(0,L"BUTTON",L"显示正在播放的播放列表",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(20),instance,nullptr);
            tab_left_=CreateWindowExW(0,L"BUTTON",L"向左滚动播放列表",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(12),instance,nullptr);
            tab_right_=CreateWindowExW(0,L"BUTTON",L"向右滚动播放列表",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(13),instance,nullptr);
            status_=CreateWindowExW(0,L"STATIC",L"无活动播放列表",WS_CHILD|SS_OWNERDRAW|SS_NOTIFY,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(17),instance,nullptr);
            sort_az_=CreateWindowExW(0,L"BUTTON",L"按名称排序播放列表 (升序)",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(18),instance,nullptr);
            sort_za_=CreateWindowExW(0,L"BUTTON",L"按名称排序播放列表 (降序)",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(19),instance,nullptr);
            search_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(11),instance,nullptr);
            SendMessageW(search_,EM_SETLIMITTEXT,16384,0);
            search_action_=CreateWindowExW(0,L"BUTTON",L"聚焦搜索",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(23),instance,nullptr);
            search_field_=CreateWindowExW(0,L"COMBOBOX",L"搜索字段",WS_CHILD|WS_CLIPSIBLINGS|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(15),instance,nullptr);
            search_scope_=CreateWindowExW(0,L"COMBOBOX",L"搜索范围",WS_CHILD|WS_CLIPSIBLINGS|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(16),instance,nullptr);
            ungrouped_view_=CreateWindowExW(0,L"BUTTON",L"不分组视图",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(21),instance,nullptr);
            grouped_view_=CreateWindowExW(0,L"BUTTON",L"分组视图",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(22),instance,nullptr);
            grouping_tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd_,nullptr,instance,nullptr);
            if(grouping_tooltip_) for(auto button:{ungrouped_view_,grouped_view_}) if(button) {
                TOOLINFOW tool{sizeof(tool)}; tool.hwnd=hwnd_; tool.uId=reinterpret_cast<UINT_PTR>(button);
                tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
                tool.lpszText=const_cast<wchar_t*>(button==ungrouped_view_?L"以未分组方式显示":L"以分组方式显示");
                SendMessageW(grouping_tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
            }
            for(const auto& field:modern_playlist::search_fields) SendMessageW(search_field_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(field.label));
            for(auto label:modern_playlist::search_scopes) SendMessageW(search_scope_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            list_=modern_playlist::create_playlist_viewport(hwnd_,instance);
            if (!list_) throw std::runtime_error("无法创建播放列表视图");
            notice_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT,0,0,0,0,hwnd_,nullptr,instance,nullptr);
            // Resolve nested controls once, before installing subclasses.
            header_=ListView_GetHeader(list_);
            scrollbar_=reinterpret_cast<HWND>(SendMessageW(list_,modern_playlist::viewport_scrollbar_window,0,0));
            COMBOBOXINFO combo{sizeof(combo)};
            if (GetComboBoxInfo(search_field_,&combo)) search_field_list_=combo.hwndList;
            if (GetComboBoxInfo(search_scope_,&combo)) search_scope_list_=combo.hwndList;
            for (HWND child:{tabs_,search_,list_,add_,header_,tab_left_,tab_right_,search_field_,search_scope_,search_action_,ungrouped_view_,grouped_view_,status_,sort_az_,sort_za_,reveal_active_,notice_,scrollbar_,search_field_list_,search_scope_list_})
                if (child) SetWindowSubclass(child,child_proc,1,reinterpret_cast<DWORD_PTR>(this));
            // The virtual viewport owns its HWND; do not let host dark-list helpers replace it.
            // Establish the tab viewport before inserting/selecting tabs.
            update_dpi(); make_columns(); theme(); layout(); refresh(); register_track_drops(); return 0;
        }
        case WM_ERASEBKGND: if(!startup_filled_) fill_startup_background(); return 1;
        case WM_PAINT: paint_frame(); return 0;
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(PtInRect(&search_edit_face_,point)) {
                // The edit's vertical padding belongs to its full clickable face.
                SetFocus(search_); MapWindowPoints(hwnd_,search_,&point,1);
                point.y=std::clamp(point.y,0L,LONG(std::max(0,text_pixels_-1)));
                SendMessageW(search_,msg,wp,MAKELPARAM(point.x,point.y)); return 0;
            }
            break;
        }
        case WM_SETCURSOR:
            if(LOWORD(lp)==HTCLIENT) {
                // The search/clear icon sits on the edit face but is a button.
                if(reinterpret_cast<HWND>(wp)==search_action_) { SetCursor(LoadCursor(nullptr,IDC_HAND)); return TRUE; }
                POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd_,&point);
                if(PtInRect(&search_edit_face_,point)) { SetCursor(LoadCursor(nullptr,IDC_IBEAM)); return TRUE; }
            }
            break;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            update_hover();
            return 0;
        }
        case WM_MOUSELEAVE: {
            update_hover();
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wp) == WA_INACTIVE && hover_ != hover_area::none) {
                hover_ = hover_area::none;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const auto& pal = current_palette();
            SetTextColor(reinterpret_cast<HDC>(wp),pal.text);
            if(artwork_.region==0) if(auto pixels=background_surface()) {
                const bool search_control=reinterpret_cast<HWND>(lp)==search_;
                auto& brush=search_control?search_surface_brush_:surface_brush_;
                if(!brush) {
                    auto info=modern_playlist::artwork_bitmap_info(*pixels); void* bits=nullptr;
                    auto bitmap=CreateDIBSection(reinterpret_cast<HDC>(wp),&info,DIB_RGB_COLORS,&bits,nullptr,0);
                    if(bitmap && bits) {
                        auto composed=*pixels;
                        if(search_control) {
                            modern_playlist::cover_pixels tint{1,1,{GetBValue(pal.search_bg),GetGValue(pal.search_bg),GetRValue(pal.search_bg),255}};
                            modern_playlist::composite_image(composed,tint,72,0);
                        }
                        memcpy(bits,composed.bgra.data(),composed.bgra.size()); brush=CreatePatternBrush(bitmap);
                    }
                    if(bitmap) DeleteObject(bitmap);
                }
                if(brush) { POINT origin{}; MapWindowPoints(reinterpret_cast<HWND>(lp),hwnd_,&origin,1);
                    SetBrushOrgEx(reinterpret_cast<HDC>(wp),-origin.x,-origin.y,nullptr); SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT); return reinterpret_cast<LRESULT>(brush); }
            }
            SetBkColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)==search_?pal.search_bg:pal.surface);
            return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==search_?edit_background_:background_);
        }
        case WM_CTLCOLORLISTBOX:
            if(reinterpret_cast<HWND>(lp)==search_field_list_ || reinterpret_cast<HWND>(lp)==search_scope_list_) {
                SetBkColor(reinterpret_cast<HDC>(wp),search_popup_color());
                SetTextColor(reinterpret_cast<HDC>(wp),current_palette().text);
                SetDCBrushColor(reinterpret_cast<HDC>(wp),search_popup_color());
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            break;
        case WM_MEASUREITEM: {
            auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if(measure->CtlID==15 || measure->CtlID==16) { measure->itemHeight=text_pixels_+scale(6); return TRUE; }
            break;
        }
        case WM_DRAWITEM: {
            auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if(draw->hwndItem==search_action_) { draw_search_action(*draw); return TRUE; }
            if(draw->hwndItem==ungrouped_view_ || draw->hwndItem==grouped_view_) {
                draw_grouping_button(*draw); return TRUE;
            }
            if(draw->hwndItem==search_field_ || draw->hwndItem==search_scope_) {
                if(draw->itemState&ODS_COMBOBOXEDIT) {
                    RECT rect{}; GetClientRect(draw->hwndItem,&rect); draw_search_selector(draw->hDC,draw->hwndItem,rect); return TRUE;
                }
                const auto& pal=current_palette(); const int saved=SaveDC(draw->hDC);
                const bool selected=(draw->itemState&ODS_SELECTED)!=0;
                const auto base=search_popup_color();
                fill(draw->hDC,draw->rcItem,selected?blend(base,pal.text,12):base);
                SetTextColor(draw->hDC,pal.text); SetBkMode(draw->hDC,TRANSPARENT); SelectObject(draw->hDC,default_font_);
                if(draw->itemID!=UINT(-1)) {
                    const auto length=SendMessageW(draw->hwndItem,CB_GETLBTEXTLEN,draw->itemID,0);
                    if(length>=0) {
                        std::wstring text(size_t(length)+1,L'\0'); SendMessageW(draw->hwndItem,CB_GETLBTEXT,draw->itemID,reinterpret_cast<LPARAM>(text.data()));
                        RECT label=draw->rcItem; label.left+=scale(6);
                        DrawTextW(draw->hDC,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                    }
                }
                RestoreDC(draw->hDC,saved); return TRUE;
            }
            if(draw->hwndItem==status_) {
                const auto& pal=current_palette();
                const int saved=SaveDC(draw->hDC);
                fill(draw->hDC,draw->rcItem,pal.surface);
                paint_artwork_surface(draw->hDC,draw->hwndItem,draw->rcItem);
                SetBkMode(draw->hDC,TRANSPARENT); SelectObject(draw->hDC,tabs_font_); SetTextColor(draw->hDC,pal.text);
                const auto text=window_text(status_);
                RECT rect=draw->rcItem; rect.left+=scale(6); rect.right=std::max(rect.left,rect.right-scale(6));
                DrawTextW(draw->hDC,text.c_str(),int(text.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                RestoreDC(draw->hDC,saved); return TRUE;
            }
            if(is_manager_button(draw->hwndItem)) {
                draw_manager_button(*draw); return TRUE;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL) { zoom(GET_WHEEL_DELTA_WPARAM(wp)); return 0; }
            if (show_tabs_) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                RECT strip{}; GetWindowRect(tabs_,&strip); MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
                if (pt.y >= strip.top && pt.y < strip.bottom) {
                    on_tabs_wheel(GET_WHEEL_DELTA_WPARAM(wp));
                    return 0;
                }
            }
            break;
        }
        case WM_MOUSEHWHEEL: {
            if (show_tabs_) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                RECT strip{}; GetWindowRect(tabs_,&strip); MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
                if (pt.y >= strip.top && pt.y < strip.bottom) {
                    on_tabs_wheel(-GET_WHEEL_DELTA_WPARAM(wp));
                    return 0;
                }
            }
            break;
        }
        case WM_DPICHANGED:
        case 0x02E3: // WM_DPICHANGED_AFTERPARENT (newer than the SDK minimum target)
            capture_columns(); update_dpi(); theme(); make_columns(); layout(); invalidate_all(); return 0;
        case WM_SIZE: cancel_header_drag(); cancel_tab_drag(); own_geometry_changed(); layout(); invalidate_all(); return 0;
        case WM_MOVE: own_geometry_changed(); invalidate_all(); return 0;
        case parent_repaint_message: parent_repaint_posted_=false; invalidate_all(); return 0;
        case WM_SETFOCUS: SetFocus(list_); return 0;
        case WM_MBUTTONUP: toggle_search(); return 0;
        case WM_COMMAND:
            if(LOWORD(wp)==23 && HIWORD(wp)==BN_CLICKED) {
                if(GetWindowTextLengthW(search_)) clear_search();
                focus_search(); return 0;
            }
            if((LOWORD(wp)==21 || LOWORD(wp)==22) && HIWORD(wp)==BN_CLICKED) {
                set_grouped_view(LOWORD(wp)==22); return 0;
            }
            if((LOWORD(wp)==15 || LOWORD(wp)==16) && (HIWORD(wp)==CBN_DROPDOWN || HIWORD(wp)==CBN_CLOSEUP)) {
                const HWND control=reinterpret_cast<HWND>(lp);
                open_search_selector_=HIWORD(wp)==CBN_DROPDOWN?control:nullptr;
                if(HIWORD(wp)==CBN_CLOSEUP) {
                    hovered_search_selector_=nullptr;
                    if(search_selector_mouse_ && GetFocus()==control) SetFocus(search_);
                }
                RedrawWindow(control,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_NOERASE); return 0;
            }
            if((LOWORD(wp)==15 || LOWORD(wp)==16) && HIWORD(wp)==CBN_SELCHANGE) {
                KillTimer(hwnd_,search_timer);
                search_settings_.field=unsigned(SendMessageW(search_field_,CB_GETCURSEL,0,0));
                search_settings_.scope=unsigned(SendMessageW(search_scope_,CB_GETCURSEL,0,0));
                if(!search_settings_.scope && active_<queries_.size()) queries_[active_]=window_text(search_);
                apply_search(); return 0;
            }
            if ((LOWORD(wp)==18 || LOWORD(wp)==19) && HIWORD(wp)==BN_CLICKED) { sort_playlists(LOWORD(wp)==18); return 0; }
            if (LOWORD(wp)==IDCANCEL) { clear_search(); return 0; }
            if (LOWORD(wp)==10 && HIWORD(wp)==BN_CLICKED) { new_playlist(); SendMessageW(add_,BM_SETSTATE,FALSE,0); SetFocus(tabs_); InvalidateRect(add_,nullptr,FALSE); }
            if (LOWORD(wp)==20 && HIWORD(wp)==BN_CLICKED) { reveal_playing_track(); return 0; }
            if (LOWORD(wp)==12 && HIWORD(wp)==BN_CLICKED) scroll_tabs(-1);
            if (LOWORD(wp)==13 && HIWORD(wp)==BN_CLICKED) scroll_tabs(1);
            if (LOWORD(wp)==11 && HIWORD(wp)==EN_CHANGE && !rebuilding_) {
                update_search_action();
                int len = GetWindowTextLengthW(search_);
                if (len <= 1) {
                    InvalidateRect(search_, nullptr, TRUE);
                }
                if (!search_settings_.scope && active_<queries_.size()) queries_[active_]=window_text(search_);
                SetTimer(hwnd_,search_timer,modern_playlist::search_delay_ms,nullptr);
            }
            return 0;
        case state_message: SetTimer(hwnd_,state_timer,1,nullptr); return 0;
        case rename_message: if(rename_edit_ && reinterpret_cast<HWND>(wp)==rename_edit_ && GetFocus()!=rename_edit_) end_rename(true); return 0;
        case WM_TIMER:
            if(wp==7) { drop_targets_.tick(); return 0; } if(wp==incremental_timer) {
            if(incremental_.expired(GetTickCount64())) clear_incremental(); return 0;
        } if(wp==5) {
            if(drag_epoch_!=playlist_epoch_ || !drag_tab_moved_) { cancel_tab_drag(); return 0; }
            POINT pt{}; GetCursorPos(&pt); ScreenToClient(tabs_,&pt);
            if(pt.x<scale(20)) scroll_tabs(-1); else if(pt.x>=manager_.viewport-scale(20)) scroll_tabs(1);
            drag_before_=manager_.insertion(pt.x); if(modern_playlist::library_pinned(0)) drag_before_=std::max(1,drag_before_);
            InvalidateRect(tabs_,nullptr,FALSE); return 0;
        } if(wp==4) { finish_cover(); return 0; } if(wp==wallpaper_timer) { finish_wallpaper(); return 0; } if(wp==parent_timer) { check_parent_background(); return 0; } if (wp==state_timer) { KillTimer(hwnd_,state_timer); update_state(); return 0; } if (wp==search_timer) { KillTimer(hwnd_,search_timer); apply_search(); } return 0;
        case fit_columns_message: fit_columns(); return 0;
        case header_order_message: save_playlist_columns(); fit_columns(); return 0;
        case refresh_message: if (pending_) refresh(); return 0;
        case WM_NOTIFY: {
            auto* h=reinterpret_cast<NMHDR*>(lp);
            if (h->hwndFrom==list_) {
                if(h->code==modern_playlist::viewport_group_select) {
                    const int g=reinterpret_cast<modern_playlist::viewport_group_request*>(lp)->group;
                    if(!pending_ && g>=0 && size_t(g)<group_members_.size() && !group_members_[g].empty()) {
                        // Use playlist occurrences, including collapsed members.
                        // Preserve selection outside the current search results.
                        const auto members=group_members_[g];
                        bit_array_bittable affected(items_.get_count()), selected(items_.get_count());
                        for(auto item:filtered_rows_) affected.set(item,true);
                        for(auto item:members) selected.set(item,true);
                        auto pm=playlist_manager::get();
                        // A collapsed group's first track is focused but hidden;
                        // see on_item_ensure_visible().
                        group_click_item_=size_t(g)<groups_.size() && groups_[g].collapsed?members.front():pfc::infinite_size;
                        group_click_time_=GetTickCount64();
                        pm->playlist_set_selection(active_,affected,selected);
                        pm->playlist_set_focus_item(active_,members.front());
                    }
                    return 0;
                }
                if(h->code==modern_playlist::viewport_group_toggle) {
                    const auto& request=*reinterpret_cast<modern_playlist::viewport_group_request*>(lp);
                    const int g=request.group;
                    if(!pending_ && g>=0 && size_t(g)<groups_.size()) {
                        search_reveal_=pfc::infinite_size; group_click_item_=pfc::infinite_size;
                        collapsed_[group_ids_[g]]=request.collapse;
                        if(groups_[g].collapsed!=request.collapse) refresh(false);
                    }
                    return 0;
                }
                if(h->code==modern_playlist::viewport_background) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_background_request*>(lp);
                    request.pixels=background_surface(); MapWindowPoints(list_,hwnd_,&request.origin,1); return 0;
                }
                if(h->code==modern_playlist::viewport_group_cover) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_group_request*>(lp);
                    if(!pending_ && request.group>=0 && size_t(request.group)<group_members_.size())
                        request.pixels=group_cover(group_members_[request.group].front(),request.load,request.artist);
                    return 0;
                }
                if(h->code==modern_playlist::viewport_row_cover) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_cover_request*>(lp);
                    if(!pending_ && request.row>=0 && size_t(request.row)<rows_.size())
                        request.pixels=group_cover(rows_[request.row],request.load,request.artist);
                    return 0;
                }
                if (h->code==modern_playlist::viewport_row_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_row_request*>(lp);
                    if (request.row>=0 && static_cast<size_t>(request.row)<row_data_.size()) {
                        const auto& row=row_data_[request.row]; request.global_index=row.track_index; request.group_index=row.track_index_in_group;
                        request.playing=row.playing; request.paused=row.paused;
                        request.queue=modern_playlist::queue_position_text(row.queue_positions);
                    }
                    return 0;
                }
                if(h->code==modern_playlist::viewport_show_playing) { reveal_playing_track(); return 0; }
                if (h->code==modern_playlist::viewport_special_edit) {
                    const auto& request=*reinterpret_cast<modern_playlist::viewport_special_request*>(lp);
                    if(!pending_ && request.row>=0 && size_t(request.row)<rows_.size() && request.column>=0 && size_t(request.column)<visible_columns_.size()) {
                        const auto& ref=columns_[visible_columns_[request.column]].ref;
                        const auto kind=ref=="Mood"?modern_playlist::special_column::mood:ref=="Rating"?modern_playlist::special_column::rating:modern_playlist::special_column::none;
                        if(kind!=modern_playlist::special_column::none && request.value>=0 && request.value<=(kind==modern_playlist::special_column::mood?1:5)) {
                            try { modern_playlist::write_special_column(hwnd_,items_[rows_[request.row]],kind,request.value); }
                            catch(const std::exception& error) { popup_message::g_show(error.what(),"现代播放列表：元数据更新失败"); }
                        }
                    }
                    return 0;
                }
                if (h->code==modern_playlist::viewport_cell_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_cell_request*>(lp);
                    if (request.row>=0 && static_cast<size_t>(request.row)<rows_.size() && request.column>=0 && static_cast<size_t>(request.column)<visible_columns_.size()) {
                        const auto& col=columns_[visible_columns_[request.column]]; request.state_column=col.state;
                        request.cover=col.ref=="Cover" || col.ref=="ArtistArt"; request.artist=col.ref=="ArtistArt";
                        request.primary_fields=col.primary_fields; request.secondary_fields=col.secondary_fields;
                        request.special=col.ref=="Mood"?modern_playlist::special_column::mood:col.ref=="Rating"?modern_playlist::special_column::rating:modern_playlist::special_column::none;
                        if (core_.extra_line && !col.state && !col.secondary_pattern.empty()) {
                            column secondary=col; secondary.script=col.secondary_script; pfc::string8 text;
                            format_cell(request.row,secondary,text); request.secondary=wide(text.c_str());
                        }
                    }
                    return 0;
                }
                if (h->code==modern_playlist::viewport_tooltip_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_tooltip_request*>(lp);
                    if (!pending_ && core_.tooltips && request.row>=0 && static_cast<size_t>(request.row)<rows_.size()) {
                        auto pm=playlist_manager::get(); t_size track=rows_[request.row];
                        if(core_.selected_tooltips) {
                            bit_array_bittable selection(items_.get_count()); pm->playlist_get_selection_mask(active_,selection);
                            track=selection.find_first(true,0,items_.get_count());
                            if(track>=items_.get_count()) return 0;
                        }
                        pfc::string8 text; pm->playlist_item_format_title(active_,track,nullptr,text,tooltip_script_,nullptr,play_control::display_level_all);
                        request.text=wide(text.c_str());
                    }
                    return 0;
                }

                if (h->code==LVN_GETDISPINFOW) {
                    auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp); auto& item=info->item;
                    if ((item.mask & LVIF_TEXT) && item.iItem>=0 && static_cast<size_t>(item.iItem)<rows_.size() && item.iSubItem>=0 && static_cast<size_t>(item.iSubItem)<visible_columns_.size()) {
                        pfc::string8 text; format_cell(static_cast<size_t>(item.iItem),columns_[visible_columns_[item.iSubItem]],text);
                        cell_=wide(text.c_str()); item.pszText=cell_.data();
                    } return 0;
                }
                if(h->code==modern_playlist::viewport_width_changed) { PostMessageW(hwnd_,fit_columns_message,0,0); return 0; }
                if (h->code==LVN_ITEMCHANGED || h->code==LVN_ODSTATECHANGED) {
                    select_view();
                    // With nothing playing, the background follows the focused track.
                    // The current image stays until finish_wallpaper() repaints with
                    // its replacement; recomposing and repainting the whole panel on
                    // every selection change made it flicker.
                    if(artwork_.enabled && !play_control::get()->is_playing()) request_wallpaper();
                    return 0;
                }
                if (h->code==NM_DBLCLK) { if (reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem>=0) default_action(); return 0; }
                if (h->code==LVN_BEGINDRAG && !pending_) { start_track_drag(); return 0; }
                if (h->code==LVN_COLUMNCLICK) { sort(reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem); return 0; }
            }
            break;
        }
        case WM_CONTEXTMENU: {
            if (is_layout_editing()) {
                // Default UI owns this menu. Identify the whole UI element,
                // preserving screen coordinates and the keyboard (-1,-1) marker.
                // A host menu command may remove or replace this panel.
                ui_element_instance::ptr keep_alive=this;
                return DefWindowProcW(hwnd_,msg,reinterpret_cast<WPARAM>(hwnd_),lp);
            }
            HWND source=reinterpret_cast<HWND>(wp); POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(source==status_ || source==sort_az_ || source==sort_za_) return 0;
            if (pt.x==-1 && pt.y==-1) {
                pt={10,10};
                if(source==tabs_) { manager_.reveal(active_==pfc::infinite_size?-1:int(active_)); RECT tab{};
                    if(tab_rect(int(active_),&tab)) pt={std::max(0L,tab.left)+scale(8),scale(8)};
                }
                if (source==list_) {
                    RECT row{}; int focus=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
                    if (focus>=0 && ListView_GetItemRect(list_,focus,&row,LVIR_BOUNDS)) pt={row.left+8,row.top+8};
                    else { RECT header{}; GetWindowRect(header_,&header); pt.y=header.bottom-header.top+8; }
                }
                ClientToScreen(source,&pt);
            }
            RECT header{}; GetWindowRect(header_,&header);
            if ((source==list_ || source==header_) && PtInRect(&header,pt)) column_menu(pt);
            else if (source==tabs_ || source==add_ || source==tab_left_ || source==tab_right_ || source==reveal_active_) tab_menu(pt);
            else if(source==hwnd_ && show_tabs_) {
                RECT strip{}; GetWindowRect(tabs_,&strip);
                if(pt.y>=strip.top && pt.y<strip.bottom) tab_menu(pt); else break;
            }
            else if (source==list_ && !pending_) {
                track_menu(pt);
            } else break;
            return 0;
        }
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }
    static std::shared_ptr<modern_playlist::cover_pixels> load_artwork(metadb_handle_ptr handle,
            album_art_manager_v2::ptr manager,const std::wstring& path,bool artist,unsigned maximum,unsigned blur,abort_callback& abort) {
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        struct com_scope { HRESULT result; ~com_scope(){if(SUCCEEDED(result)) CoUninitialize();} } scope{initialized};
        try {
            Microsoft::WRL::ComPtr<IStream> stream;
            if(path.empty()) {
                if(handle.is_empty()) return {};
                metadb_handle_list tracks; tracks.add_item(handle);
                const GUID id=artist?album_art_ids::artist:album_art_ids::cover_front;
                pfc::list_t<GUID> ids; ids.add_item(id);
                auto extractor=manager->open(tracks,ids,abort); auto data=extractor->query(id,abort);
                if(data->get_size()>32*1024*1024) return {};
                stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(data->get_ptr()),static_cast<UINT>(data->get_size())));
                if(!stream) return {};
            }
            std::wstring native_path=path;
            if(!path.empty()) {
                pfc::string8 native;
                if(filesystem::g_get_native_path(utf8(path).c_str(),native,abort)) native_path=wide(native.c_str());
            }
            auto pixels=modern_playlist::decode_artwork(stream.Get(),native_path,maximum,abort);
            if(pixels) modern_playlist::box_blur(*pixels,blur);
            abort.check(); return pixels;
        } catch(...) { return {}; }
    }
    std::shared_ptr<modern_playlist::cover_pixels> group_cover(t_size track,bool load,bool artist=false) {
        if(track>=items_.get_count()) return {};
        const auto handle=items_[track];
        const std::string key=std::string(artist?"artist:":"cover:")+handle->get_path()+"#"+std::to_string(handle->get_subsong_index());
        std::shared_ptr<modern_playlist::cover_pixels> cached;
        if(covers_.lookup(key,cached) && !covers_.stale(key)) return cached;
        if(load && !cover_job_.valid() && !destroying_) {
            auto manager=album_art_manager_v2::get(); auto* abort=&cover_abort_;
            cover_job_key_=key; cover_job_epoch_=artwork_epoch_;
            cover_job_=std::async(std::launch::async,[handle,manager,abort,artist] { return load_artwork(handle,manager,{},artist,256,0,*abort); });
            SetTimer(hwnd_,4,16,nullptr);
        }
        // A stale image stays visible until finish_cover() replaces it; otherwise
        // the viewport draws pending/missing artwork using its current style.
        return cached;
    }
    void finish_cover() {
        if(!cover_job_.valid()) { KillTimer(hwnd_,4); return; }
        if(cover_job_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
        auto pixels=cover_job_.get(); KillTimer(hwnd_,4);
        if(cover_job_epoch_==artwork_epoch_) {
            // A null entry remembers a failed lookup without caching theme-colored pixels.
            covers_.store(cover_job_key_,std::move(pixels));
        }
        // Artwork completion must not discard text layouts or cancel cell edits.
        InvalidateRect(list_,nullptr,FALSE);
    }
    void request_wallpaper() {
        if(!artwork_.enabled || artwork_.source==3 || destroying_) return;
        metadb_handle_ptr handle; play_control::get()->get_now_playing(handle);
        if(handle.is_empty()) {
            const auto pm=playlist_manager::get(); const auto active=pm->get_active_playlist();
            const auto focus=active<pm->get_playlist_count()?pm->playlist_get_focus_item(active):pfc::infinite_size;
            if(active<pm->get_playlist_count() && focus<pm->playlist_get_item_count(active)) pm->playlist_get_item_handle(handle,active,focus);
        }
        std::wstring path;
        if(artwork_.source==1) {
            pfc::string8 formatted;
            if(handle.is_valid()) handle->format_title(nullptr,formatted,wallpaper_script_,nullptr);
            else wallpaper_script_->run(nullptr,formatted,nullptr);
            path=wide(formatted.c_str());
            if(path.empty()) {
                // An empty TF result means no image, not the previous track's image.
                if(!wallpaper_key_.empty() || !wallpaper_known_ || wallpaper_) {
                    const bool shown=wallpaper_!=nullptr;
                    wallpaper_key_.clear(); wallpaper_known_=true; wallpaper_.reset(); clear_surface();
                    if(shown) invalidate_all(); // Every part of the panel showed the image.
                }
                return;
            }
        }
        const bool artist=artwork_.source==4;
        const std::string identity=artwork_.source==1?utf8(path):handle.is_valid()?std::string(handle->get_path())+"#"+std::to_string(handle->get_subsong_index()):"<none>";
        const std::string key=std::to_string(artwork_.source)+":"+identity;
        if(key!=wallpaper_key_) { wallpaper_key_=key; wallpaper_known_=false; } // Keep the previous image until its replacement is ready.
        if(wallpaper_known_ || wallpaper_job_.valid()) return;
        const unsigned blur=artwork_.blur; auto manager=album_art_manager_v2::get(); auto* abort=&cover_abort_;
        wallpaper_job_key_=key; wallpaper_job_epoch_=artwork_epoch_;
        wallpaper_job_=std::async(std::launch::async,[handle,manager,path,artist,blur,abort] { return load_artwork(handle,manager,path,artist,1920,blur,*abort); });
        SetTimer(hwnd_,wallpaper_timer,30,nullptr);
    }
    void finish_wallpaper() {
        if(!wallpaper_job_.valid()) { KillTimer(hwnd_,wallpaper_timer); return; }
        if(wallpaper_job_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
        auto pixels=wallpaper_job_.get(); KillTimer(hwnd_,wallpaper_timer);
        if(wallpaper_job_epoch_==artwork_epoch_ && wallpaper_job_key_==wallpaper_key_) {
            const bool identical=wallpaper_ && pixels && wallpaper_->width==pixels->width && wallpaper_->height==pixels->height && wallpaper_->bgra==pixels->bgra;
            if(!identical) { wallpaper_=std::move(pixels); clear_surface(); }
            wallpaper_known_=true;
        }
        invalidate_all();
    }
    // Capture the parent behind the whole panel. Returns false when the pixels
    // match the last capture, which is then kept without blurring it again.
    bool capture_parent(unsigned width,unsigned height) {
        auto& raw=parent_scratch_; raw.width=width; raw.height=height; raw.bgra.resize(size_t(width)*height*4);
        const auto color=current_palette().row;
        for(size_t i=0;i<raw.bgra.size();i+=4) { raw.bgra[i]=GetBValue(color); raw.bgra[i+1]=GetGValue(color); raw.bgra[i+2]=GetRValue(color); raw.bgra[i+3]=255; }
        composing_surface_=true;
        modern_playlist::capture_parent_background(hwnd_,raw);
        composing_surface_=false;
        parent_capture_key_=modern_playlist::parent_background_key(hwnd_);
        if(parent_capture_ && raw.width==parent_raw_.width && raw.height==parent_raw_.height && raw.bgra==parent_raw_.bgra) return false;
        std::swap(parent_raw_,parent_scratch_);
        auto blurred=std::make_shared<modern_playlist::cover_pixels>(parent_raw_);
        modern_playlist::box_blur(*blurred,artwork_.blur);
        parent_capture_=std::move(blurred); return true;
    }
    // The host does not say when it repaints behind the panel. While pseudo
    // transparency is used, a timer checks every 100 ms whether this panel or
    // an ancestor moved, and compares the parent's pixels with the last
    // capture: every 250 ms for a few seconds after a change, a new track or
    // startup, otherwise every second, and less often if the parent paints
    // slowly. Only a changed capture is blurred and repainted.
    void watch_parent_background() {
        if(parent_watch_ || !hwnd_ || destroying_) return;
        parent_watch_=true; parent_busy(3000);
        SetTimer(hwnd_,parent_timer,100,nullptr);
    }
    // Check sooner for a while, e.g. while the host loads a new track's art.
    void parent_busy(ULONGLONG duration) {
        const auto now=GetTickCount64();
        parent_busy_until_=std::max(parent_busy_until_,now+duration); parent_check_due_=std::min(parent_check_due_,now);
    }
    void check_parent_background() {
        if(!artwork_.enabled || artwork_.source!=3 || !artwork_.opacity || destroying_) {
            KillTimer(hwnd_,parent_timer); parent_watch_=false; return;
        }
        if(!IsWindowVisible(hwnd_) || IsIconic(GetAncestor(hwnd_,GA_ROOT))) return;
        RECT r{}; GetClientRect(hwnd_,&r);
        if(r.right<=0 || r.bottom<=0 || uint64_t(r.right)*r.bottom>16000000) return;
        const auto now=GetTickCount64();
        const bool moved=!modern_playlist::same_parent_background(modern_playlist::parent_background_key(hwnd_),parent_capture_key_);
        if(now<(moved?parent_move_due_:parent_check_due_)) return;
        const auto started=std::chrono::steady_clock::now();
        const bool changed=capture_parent(unsigned(r.right),unsigned(r.bottom));
        const auto cost=ULONGLONG(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
        if(changed) { parent_busy(2000); clear_surface(); invalidate_all(); }
        // Keep capturing to a small share of the UI thread's time.
        parent_move_due_=now+cost*3;
        parent_check_due_=now+std::max<ULONGLONG>(now<parent_busy_until_?250:1000,cost*20);
    }
    // This panel moved or resized and repaints in full anyway, so the next
    // paint recaptures without the extra repaint an ancestor's move needs.
    void own_geometry_changed() {
        if(artwork_.source==3) { forget_parent_capture(); parent_capture_key_=modern_playlist::parent_background_key(hwnd_); }
        clear_surface();
    }
    void post_parent_repaint() {
        if(!parent_repaint_posted_ && hwnd_) parent_repaint_posted_=PostMessageW(hwnd_,parent_repaint_message,0,0)!=FALSE;
    }
    std::shared_ptr<modern_playlist::cover_pixels> background_surface() {
        if(!artwork_.enabled || !artwork_.opacity || composing_surface_) return {};
        request_wallpaper();
        RECT r{}; GetClientRect(hwnd_,&r);
        if(r.right<=0 || r.bottom<=0 || uint64_t(r.right)*r.bottom>16000000) return {};
        // Every painted part asks for the surface, so capturing per request made
        // each repaint capture and blur the whole panel many times. Recapture
        // only when the parent may show different pixels behind this panel.
        if(artwork_.source==3) {
            watch_parent_background();
            const auto key=modern_playlist::parent_background_key(hwnd_);
            if(!modern_playlist::same_parent_background(key,parent_capture_key_)) {
                // An ancestor moved. This paint may cover only part of the panel,
                // so repaint all of it, or the rest keeps the old background.
                parent_capture_key_=key; forget_parent_capture(); clear_surface(); post_parent_repaint();
            }
        }
        if(surface_ && surface_->width==unsigned(r.right) && surface_->height==unsigned(r.bottom)) return surface_;
        auto pixels=std::make_shared<modern_playlist::cover_pixels>(); pixels->width=r.right; pixels->height=r.bottom; pixels->bgra.resize(size_t(r.right)*r.bottom*4);
        const auto color=current_palette().row;
        for(size_t i=0;i<pixels->bgra.size();i+=4) { pixels->bgra[i]=GetBValue(color); pixels->bgra[i+1]=GetGValue(color); pixels->bgra[i+2]=GetRValue(color); pixels->bgra[i+3]=255; }
        if(artwork_.source==3) {
            if(!parent_capture_ || parent_capture_->width!=pixels->width || parent_capture_->height!=pixels->height)
                capture_parent(pixels->width,pixels->height);
            modern_playlist::composite_image(*pixels,*parent_capture_,artwork_.opacity,0);
        } else if(wallpaper_) {
            modern_playlist::composite_image(*pixels,*wallpaper_,artwork_.opacity,artwork_.mode);
            // Dim the finished image with the normal panel background color.
            // Compose once so GDI controls and the Direct2D viewport share it.
            const modern_playlist::cover_pixels overlay{1,1,{GetBValue(color),GetGValue(color),GetRValue(color),255}};
            modern_playlist::composite_image(*pixels,overlay,artwork_.dimming,0);
        }
        surface_=std::move(pixels); return surface_;
    }
    bool paint_artwork_surface(HDC dc,HWND child,const RECT& rect) {
        // Playlist also covers the entire status row, including both sort buttons.
        // Search, column headers and playlist tabs belong to Whole panel only.
        if(artwork_.region!=0 && child!=status_ && child!=sort_az_ && child!=sort_za_) return false;
        auto pixels=background_surface(); if(!pixels) return false;
        POINT origin{}; MapWindowPoints(child,hwnd_,&origin,1);
        const int saved=SaveDC(dc); IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
        modern_playlist::draw_surface_gdi(dc,*pixels,-origin.x,-origin.y);
        RestoreDC(dc,saved); return true;
    }
    void build_rows() {
        clear_surface();
        rows_.clear(); row_data_.clear(); groups_.clear(); group_members_.clear(); group_ids_.clear();
        modern_playlist::group_position position{}; std::string previous;
        std::vector<modern_playlist::group_position> positions(items_.get_count());
        std::vector<std::vector<t_size>> members; std::vector<std::string> ids;
        size_t visible=0;
        for(size_t i=0;i<items_.get_count();++i) {
            pfc::string8 key; items_[i]->format_title(nullptr,key,group_script_,nullptr);
            position=modern_playlist::next_group_position(previous,key.c_str(),position,i==0);
            if(i==0 || key.c_str()!=previous) { members.emplace_back(); ids.push_back(std::to_string(i)+":"+key.c_str()); }
            previous=key.c_str(); positions[i]=position;
            if(visible<filtered_rows_.size() && filtered_rows_[visible]==i) { members.back().push_back(i); ++visible; }
        }
        t_size playing_playlist=pfc::infinite_size, playing_item=pfc::infinite_size;
        auto pm=playlist_manager::get();
        const auto next_auto=play_control::get()->is_playing() && pm->get_playing_item_location(&playing_playlist,&playing_item) && playing_playlist==active_?playing_item:pfc::infinite_size;
        // Manual expansion/collapse overrides last until the playing occurrence
        // changes; they never turn off the saved auto-collapse preference.
        if(grouping_.autocollapse && next_auto!=auto_item_) collapsed_.clear();
        auto_item_=next_auto;
        bool cover=false,artist_art=false;
        for(const auto& col:columns_) if(col.visible) {
            if(col.ref=="Cover") cover=true;
            else if(col.ref=="ArtistArt") artist_art=true;
        }
        for(size_t g=0;g<members.size();++g) {
            if(members[g].empty()) continue;
            const auto& tracks=members[g];
            bool collapsed=grouping_.collapse_default;
            if(grouping_.autocollapse) collapsed=std::find(tracks.begin(),tracks.end(),auto_item_)==tracks.end();
            const auto saved=collapsed_.find(ids[g]); if(saved!=collapsed_.end()) collapsed=saved->second;
            if(std::find(tracks.begin(),tracks.end(),search_reveal_)!=tracks.end()) collapsed=false;
            if(grouping_.active()) {
                modern_playlist::viewport_group group;
                group.collapsed=collapsed; group.cover=cover; group.artist_art=artist_art; group.band.first=rows_.size();
                group.artwork_in_header=grouping_.artwork_in_header;
                for(size_t col=0;col<visible_columns_.size();++col) {
                    const auto& ref=columns_[visible_columns_[col]].ref;
                    if(ref=="Cover") group.cover_column=int(col);
                    else if(ref=="ArtistArt") group.artist_column=int(col);
                }
                const auto& pattern=grouping_.patterns[grouping_.pattern];
                const std::string* sources[]={&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2};
                for(int n=0;n<4;++n) group.fields[n]=modern_playlist::search_format_fields(*sources[n]);
                group.band.count=collapsed?0:tracks.size();
                // The viewport adds only the padding needed by artwork columns,
                // using their current displayed widths.
                std::wstring* labels[]={&group.l1,&group.r1,&group.l2,&group.r2};
                for(int n=0;n<4;++n) { pfc::string8 text; items_[tracks.front()]->format_title(nullptr,text,group_labels_[n],nullptr); *labels[n]=wide(text.c_str()); }
                double seconds=0; for(auto track:tracks) seconds+=items_[track]->get_length();
                const auto total=static_cast<unsigned long long>(std::max(0.0,seconds));
                group.r2+=(group.r2.empty()?L"":L" | ")+std::to_wstring(tracks.size())+L" 首 | "+
                    std::to_wstring(total/60)+L":"+(total%60<10?L"0":L"")+std::to_wstring(total%60);
                groups_.push_back(std::move(group)); group_members_.push_back(tracks); group_ids_.push_back(ids[g]);
                if(collapsed) continue;
            }
            size_t in_group=0;
            for(auto i:tracks) {
                modern_playlist::playlist_row<metadb_handle_ptr> row;
                row.row_index=rows_.size(); row.track_index=i; row.metadb=items_[i];
                row.group_index=positions[i].group; row.track_index_in_group=in_group++;
                const char* path=items_[i]->get_path();
                row.tracktype=strstr(path,"://") && _strnicmp(path,"file://",7)?modern_playlist::track_kind::stream:modern_playlist::track_kind::file;
                rows_.push_back(i); row_data_.push_back(std::move(row));
            }
        }
    }
    void update_state() {
        repaint_playing_tab();
        if (destroying_ || !list_ || pending_ || row_data_.size()!=rows_.size()) return;
        auto pm=playlist_manager::get(); pfc::list_t<t_playback_queue_item> queue; pm->queue_get_contents(queue);
        std::vector<const void*> handles; handles.reserve(items_.get_count());
        for(size_t i=0;i<items_.get_count();++i) handles.push_back(items_[i].get_ptr());
        std::vector<modern_playlist::queue_entry> entries; entries.reserve(queue.get_count());
        for(size_t i=0;i<queue.get_count();++i) entries.push_back({queue[i].m_playlist,queue[i].m_item,queue[i].m_handle.get_ptr()});
        const auto positions=modern_playlist::queue_positions(active_,handles,entries,modern_playlist::is_queue_playlist(active_));
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        const bool playing=play_control::get()->is_playing() && pm->get_playing_item_location(&playlist,&item) && playlist==active_;
        const t_size next_auto=playing?item:pfc::infinite_size;
        if(grouping_.active() && grouping_.autocollapse && next_auto!=auto_item_) { refresh(); return; }
        int playing_group=-1;
        for(size_t g=0;g<group_members_.size();++g)
            if(std::find(group_members_[g].begin(),group_members_[g].end(),next_auto)!=group_members_[g].end()) {
                playing_group=static_cast<int>(g); break;
            }
        modern_playlist::set_playlist_playing_group(list_,playing_group);
        const bool paused=play_control::get()->is_paused(); int playing_row=-1;
        const std::vector<size_t> empty;
        for (size_t i=0;i<row_data_.size();++i) {
            auto& row=row_data_[i]; const auto found=positions.find(row.track_index);
            const auto& indices=found==positions.end()?empty:found->second;
            const bool now=playing && row.track_index==item;
            if (row.queue_positions!=indices || row.playing!=now || row.paused!=(now && paused)) {
                row.queue_positions=indices; row.playing=now; row.paused=now && paused;
                modern_playlist::invalidate_playlist_row(list_,static_cast<int>(i));
            }
            if(now) playing_row=static_cast<int>(i);
        }
        if (std::none_of(visible_columns_.begin(),visible_columns_.end(),[&](int col){return columns_[col].state;})) playing_row=-1;
        modern_playlist::set_playlist_playback(list_,playing_row,paused);
    }
    void show_now_playing(bool activate) {
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        auto pm=playlist_manager::get();
        if (!play_control::get()->is_playing() || !pm->get_playing_item_location(&playlist,&item)) return;
        if (activate) {
            if (pm->get_active_playlist()!=playlist) pm->set_active_playlist(playlist);
            // Activation queues a refresh; finish it before using playlist row indices.
            if (pending_ || active_!=playlist) refresh();
            if (active_!=playlist || item>=items_.get_count()) return;
            // Explicit navigation must also reveal a song hidden by a saved search.
            if (!std::binary_search(filtered_rows_.begin(),filtered_rows_.end(),item)) clear_search();
        }
        if (playlist!=active_ || pending_) return;
        for(size_t g=0;g<groups_.size();++g) if(groups_[g].collapsed &&
            std::find(group_members_[g].begin(),group_members_[g].end(),item)!=group_members_[g].end()) {
            collapsed_[group_ids_[g]]=false; refresh(); break;
        }
        const auto row=std::lower_bound(rows_.begin(),rows_.end(),item);
        if (row!=rows_.end() && *row==item) ListView_EnsureVisible(list_,static_cast<int>(row-rows_.begin()),FALSE);
    }
    void default_action() {
        if (!core_.enqueue_on_double_click) { play(); return; }
        const int row=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (!pending_ && row>=0 && static_cast<size_t>(row)<rows_.size()) playlist_manager::get()->queue_add_item_playlist(active_,rows_[row]);
    }
    // Opens Panel Settings on a page; the Columns page can preselect a column.
    void edit_core_settings(int page=0,int column=-1) {
        ui_element_instance::ptr keep_alive=this;
        save_playlist_columns();
        panel_settings_data edited;
        edited.state=current_state();
        edited.initial_page=page; edited.initial_column=column;
        edited.focus_border=current_palette().highlight;
        const int focused=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if(!pending_ && focused>=0 && size_t(focused)<rows_.size()) {
            edited.preview.index=rows_[focused]; edited.preview.total=items_.get_count();
            edited.preview.track=items_[rows_[focused]];
            edited.preview.playing=size_t(focused)<row_data_.size() && row_data_[focused].playing;
        }
        edited.serialize=[](const panel_state& state) {
            const auto config=write_state(state);
            return std::string(static_cast<const char*>(config->get_data()),config->get_data_size());
        };
        edited.parse=[](const std::string& record) {
            return read_state(ui_element_config::g_create(element_id,record.data(),record.size()));
        };
        edited.apply=[this](panel_settings_data& settings) {
            if(destroying_ || !hwnd_) return;
            const auto& state=settings.state;
            const auto& artwork=state.artwork;
            const bool reload=artwork_.enabled!=artwork.enabled || artwork_.source!=artwork.source || artwork_.path!=artwork.path || artwork_.blur!=artwork.blur;
            cancel_header_drag(); cancel_tab_drag();
            core_=state.core; artwork_=artwork; manager_bottom_=state.manager_bottom;
            search_settings_.typing_field=state.search.typing_field; clear_incremental();
            if(settings.panel_changed) {
                // Reset and Import also replace settings that have no page.
                show_tabs_=state.show_tabs; show_header_=state.show_header;
                headers_follow_alignment_=state.headers_follow_alignment;
                show_scrollbar_=state.show_scrollbar; show_status_=state.show_status; zoom_percent_=state.zoom_percent;
                search_settings_=state.search; clear_incremental();
            }
            const bool columns=settings.columns_changed || settings.panel_changed;
            if(columns) { columns_=state.columns; sort_column_=-1; }
            if(settings.groups_changed) {
                grouping_=state.groups; collapsed_.clear(); apply_filter_next_=true;
                search_reveal_=pfc::infinite_size;
            } else if(settings.appearance_changed) {
                // Header height and fonts need neither re-filtering nor sorting.
                grouping_.header_rows=state.groups.header_rows; grouping_.fonts=state.groups.fonts;
            }
            compile_columns();
            if(reload) refresh_artwork(false,false);
            if(columns) make_columns();
            theme();
            refresh(false);
            manager_.reveal(active_==pfc::infinite_size?-1:int(active_));
            update_arrows(); invalidate_all();
            if(settings.groups_changed && grouping_.active()) apply_group_sort();
            // Keep the staged baseline aligned with the layout produced by Apply.
            settings.state.columns=columns_; settings.state.groups=grouping_;
        };
        DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PANEL_SETTINGS),hwnd_,panel_settings_dialog,reinterpret_cast<LPARAM>(&edited));
    }
    void sync_host_selection(t_size playlist, const bit_array* affected = nullptr, const bit_array* state = nullptr) {
        if (destroying_ || rebuilding_ || pending_ || playlist!=active_) return;
        ++content_epoch_;
        auto pm=playlist_manager::get();
        const auto focused=pm->playlist_get_focus_item(active_);
        const auto position=std::lower_bound(rows_.begin(),rows_.end(),focused);
        const int focus=position!=rows_.end() && *position==focused ? static_cast<int>(position-rows_.begin()) : -1;
        rebuilding_=true;
        if (affected && state) for (size_t row=0;row<rows_.size();++row) if ((*affected)[rows_[row]]) {
            ListView_SetItemState(list_,static_cast<int>(row),(*state)[rows_[row]]?LVIS_SELECTED:0,LVIS_SELECTED);
        }
        const int old=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (old!=focus) {
            if (old>=0) ListView_SetItemState(list_,old,0,LVIS_FOCUSED);
            if (focus>=0) ListView_SetItemState(list_,focus,LVIS_FOCUSED,LVIS_FOCUSED);
        }
        rebuilding_=false;
    }
    void invalidate_metadata(t_size playlist,const bit_array& mask) {
        if (destroying_ || !list_ || pending_ || playlist!=active_) return;
        modern_playlist::invalidate_playlist_rows(list_,[&](int row) { return mask[rows_[row]]; });
    }
    void on_items_added(t_size playlist,t_size,metadb_handle_list_cref,const bit_array&) override { if (playlist==active_) schedule(); }
    void on_items_reordered(t_size playlist,const t_size*,t_size) override { if (playlist==active_) schedule(); }
    void on_items_removed(t_size playlist,const bit_array&,t_size,t_size) override { if (playlist==active_) schedule(); }
    void on_items_selection_change(t_size playlist,const bit_array& affected,const bit_array& state) override { sync_host_selection(playlist,&affected,&state); }
    void on_item_focus_change(t_size playlist,t_size,t_size) override { sync_host_selection(playlist); }
    void on_item_ensure_visible(t_size playlist,t_size item) override {
        if(destroying_ || !list_) return;
        // A click on a collapsed group's header focuses its first track. A
        // request to show that track, in answer to the click, must not expand
        // the group: its header is already in view, and a double-click on it
        // would otherwise expand the group and then collapse it again.
        if(playlist==active_ && item==group_click_item_ && GetTickCount64()-group_click_time_<=GetDoubleClickTime()) return;
        auto pm=playlist_manager::get();
        if(playlist>=pm->get_playlist_count() || playlist!=pm->get_active_playlist() ||
            item>=pm->playlist_get_item_count(playlist)) return;
        // The host sends this separately from focus/selection (including when
        // Cursor follows playback is enabled). Focus alone must not force scrolling.
        // Construction registers a host callback, which is forbidden during
        // callback dispatch. Reuse the tracker created during panel construction.
        auto* position=&ensure_visible_position_;
        position->m_playlist=playlist; position->m_item=item;
        metadb_handle_ptr track; pm->playlist_get_item_handle(track,playlist,item);
        const auto request=++ensure_visible_request_;
        ui_element_instance::ptr keep_alive=this;
        // Playlist callbacks may only read host state. Finish any pending refresh
        // outside the callback, tracking the exact occurrence across intervening edits.
        fb2k::inMainThread([this,keep_alive,position,track,request] {
            if(destroying_ || !list_ || request!=ensure_visible_request_) return;
            try {
                auto pm=playlist_manager::get();
                auto current=[&] {
                    if(destroying_ || !list_ || request!=ensure_visible_request_ ||
                        position->m_playlist>=pm->get_playlist_count() ||
                        position->m_playlist!=pm->get_active_playlist() ||
                        position->m_item>=pm->playlist_get_item_count(position->m_playlist)) return false;
                    metadb_handle_ptr item; pm->playlist_get_item_handle(item,position->m_playlist,position->m_item);
                    return item==track;
                };
                // A playlist switch may apply a grouping sort and queue one
                // further rebuild; finish both before mapping the tracked index.
                for(int pass=0;pass<2 && (pending_ || active_!=position->m_playlist);++pass) {
                    if(!current()) return;
                    refresh(false);
                }
                if(pending_ || active_!=position->m_playlist || !current()) return;
                // Preserve search filtering; only expand a group containing the
                // requested visible occurrence, without changing host selection.
                for(size_t g=0;g<groups_.size();++g) if(groups_[g].collapsed &&
                    std::find(group_members_[g].begin(),group_members_[g].end(),position->m_item)!=group_members_[g].end()) {
                    collapsed_[group_ids_[g]]=false; refresh(false); break;
                }
                if(pending_ || active_!=position->m_playlist || !current()) return;
                const auto row=std::lower_bound(rows_.begin(),rows_.end(),position->m_item);
                if(row!=rows_.end() && *row==position->m_item)
                    ListView_EnsureVisible(list_,static_cast<int>(row-rows_.begin()),FALSE);
            } catch(const std::exception& e) { console::error(e.what()); }
        });
    }
    void on_items_modified(t_size playlist,const bit_array& mask) override {
        if (destroying_ || pending_ || playlist!=active_) return;
        for(t_size i=0;i<items_.get_count();++i) if(mask[i]) {
            const auto handle=items_[i];
            const auto key=std::string(handle->get_path())+"#"+std::to_string(handle->get_subsong_index());
            covers_.mark_stale("cover:"+key); covers_.mark_stale("artist:"+key);
        }
        refresh_artwork(true,false);
        // Tag changes can alter filter membership or contiguous album-group parity.
        if (grouping_.active() || !applied_query_.empty()) { schedule(); }
        else { build_rows(); update_state(); modern_playlist::invalidate_playlist_row(list_,-1); }
    }
    void on_items_modified_fromplayback(t_size playlist,const bit_array& mask,play_control::t_display_level) override {
        invalidate_metadata(playlist,mask);
    }
    void on_items_replaced(t_size playlist,const bit_array&,const pfc::list_base_const_t<t_on_items_replaced_entry>&) override { if (playlist==active_) schedule(); }
    void on_playlist_locked(t_size,bool) override { schedule(); InvalidateRect(tabs_,nullptr,FALSE); }
    void on_playlist_activate(t_size,t_size) override { schedule(); }
    void on_playlist_created(t_size index,const char*,t_size) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        if (index<=queries_.size()) queries_.insert(queries_.begin()+index,L""); active_=pfc::infinite_size; schedule();
    }
    void on_playlists_reorder(const t_size* order,t_size count) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        auto old=queries_; queries_.resize(count);
        // Reordering changes indices, not the displayed playlist's identity.
        // Remap the existing view so refresh() preserves scroll, search, groups
        // and column-sort state instead of treating this as playlist activation.
        const auto previous=active_;
        active_=pfc::infinite_size;
        for(t_size i=0;i<count;++i) {
            queries_[i]=order[i]<old.size()?old[order[i]]:L"";
            if(order[i]==previous) active_=i;
        }
        schedule();
    }
    void on_playlists_removed(const bit_array& mask,t_size old_count,t_size) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        std::vector<std::wstring> remaining;
        for(t_size i=0;i<old_count;++i) if(!mask[i]) remaining.push_back(i<queries_.size()?queries_[i]:L"");
        queries_=std::move(remaining); active_=pfc::infinite_size; schedule();
    }
    void on_playlist_renamed(t_size,const char*,t_size) override { ++playlist_epoch_; tabs_dirty_=true; apply_filter_next_=true; schedule(); }
};

class playlist_element : public ui_element {
public:
    GUID get_guid() override { return element_id; }
    GUID get_subclass() override { return ui_element_subclass_playlist_renderers; }
    void get_name(pfc::string_base& out) override { out="现代播放列表"; }
    bool get_description(pfc::string_base& out) override { out="播放列表标签、搜索与可自定义的标题格式列。"; return true; }
    ui_element_instance::ptr instantiate(HWND parent,ui_element_config::ptr cfg,ui_element_instance_callback_ptr cb) override {
        return new service_impl_t<playlist_view>(parent,cfg,cb);
    }
    ui_element_config::ptr get_default_configuration() override { return ui_element_config::g_create_empty(element_id); }
    ui_element_children_enumerator::ptr enumerate_children(ui_element_config::ptr) override { return nullptr; }
};
service_factory_single_t<playlist_element> element_factory;
} // namespace

namespace modern_playlist {
ui_element_instance::ptr create_columns_view(HWND parent, ui_element_config::ptr config) {
    return new service_impl_t<playlist_view>(parent, config, nullptr, false);
}
}
