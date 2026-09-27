#include "playlist_viewport.h"
#include "artwork_win.h"
#include "icon_shapes.h"
#include "scroll_model.h"
#include "scrollbar_win.h"
#include "playlist_core.h"
#include "viewport_accessibility.h"
#include <commctrl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace modern_playlist {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT_PTR drag_timer = 44;
constexpr UINT_PTR frame_timer = 41;
constexpr UINT_PTR dirty_timer = 42, playback_timer = 43;
constexpr UINT playback_interval_ms = 1000;
constexpr UINT search_message = WM_APP + 210;
constexpr UINT groups_message = WM_APP + 209;
constexpr UINT playing_group_message = WM_APP + 217;
constexpr UINT playback_message = WM_APP + 207;
constexpr UINT style_message = WM_APP + 201;
constexpr UINT invalidate_row_message = WM_APP + 202;
constexpr UINT reset_scroll_message = WM_APP + 203;
constexpr UINT suspend_input_message = WM_APP + 204;
constexpr UINT invalidate_rows_message = WM_APP + 206;

class viewport {
    HWND window_ = nullptr, header_ = nullptr, tooltip_ = nullptr;
    int hover_row_ = -1, playing_row_ = -1, playing_group_ = -1;
    bool paused_ = false, play_phase_ = false, play_timer_running_ = false;
    ULONGLONG play_started_ = 0;
    std::wstring tooltip_text_;
    bool tooltip_visible_=false, tooltip_updating_=false;
    HFONT extra_font_ = nullptr, group_font_ = nullptr;
    int icon_pitch_ = 18;
    std::map<uint64_t,std::shared_ptr<cover_pixels>> special_icons_;
    ComPtr<viewport_accessibility> accessible_;
    HFONT font_ = nullptr; // Borrowed from the panel, replaced before it is deleted.
    viewport_style style_;
    viewport_background_request wallpaper_;
    struct cached_bitmap { std::shared_ptr<cover_pixels> pixels; ComPtr<ID2D1Bitmap> bitmap; size_t used=0; };
    std::map<const cover_pixels*,cached_bitmap> bitmaps_;
    size_t bitmap_clock_=0, bitmap_bytes_=0;
    cached_bitmap background_bitmap_;
    std::shared_ptr<cover_pixels> default_cover_pixels_;
    void draw_default_cover(float x,float y,int width,int height,HDC dc) {
        const auto r=image_placement(1,1,width,height,2);
        if(r.w<=0 || r.h<=0) return;
        const auto design=make_default_cover_style(unsigned(r.w),style_.row,style_.text);
        x=std::round(x+float(r.x)); y=std::round(y+float(r.y));
        if(dc) {
            if(!default_cover_pixels_ || default_cover_pixels_->width!=design.size)
                default_cover_pixels_=raster_default_cover(design);
            draw_surface_gdi(dc,*default_cover_pixels_,int(x),int(y));
            return;
        }
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        brush_->SetColor(color(design.base));
        target_->FillRectangle(D2D1::RectF(x,y,x+design.size,y+design.size),brush_.Get());
        const auto center=D2D1::Point2F(x+design.size/2.f,y+design.size/2.f);
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        brush_->SetColor(color(design.ring));
        target_->FillEllipse(D2D1::Ellipse(center,design.outer_diameter/2.f,design.outer_diameter/2.f),brush_.Get());
        brush_->SetColor(color(design.base));
        target_->FillEllipse(D2D1::Ellipse(center,design.inner_diameter/2.f,design.inner_diameter/2.f),brush_.Get());
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    }
    void draw_image(const std::shared_ptr<cover_pixels>& pixels,float x,float y,float w,float h,HDC dc) {
        if(!pixels || w<=0 || h<=0) return;
        if(dc) { draw_artwork_gdi(dc,*pixels,int(x),int(y),int(w),int(h)); return; }
        if(pixels==wallpaper_.pixels) {
            if(background_bitmap_.pixels!=pixels || !background_bitmap_.bitmap) {
                background_bitmap_={}; background_bitmap_.pixels=pixels;
                target_->CreateBitmap(D2D1::SizeU(pixels->width,pixels->height),pixels->bgra.data(),pixels->width*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),background_bitmap_.bitmap.GetAddressOf());
            }
            if(background_bitmap_.bitmap) target_->DrawBitmap(background_bitmap_.bitmap.Get(),D2D1::RectF(x,y,x+w,y+h));
            return;
        }
        auto found=bitmaps_.find(pixels.get());
        if(found==bitmaps_.end()) {
            // Evict only the least recently drawn bitmap, keeping the rest warm.
            while(!bitmaps_.empty() && (bitmaps_.size()>=512 || bitmap_bytes_+pixels->bgra.size()>64U*1024*1024)) {
                const auto oldest=std::min_element(bitmaps_.begin(),bitmaps_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                bitmap_bytes_-=oldest->second.pixels->bgra.size(); bitmaps_.erase(oldest);
            }
            cached_bitmap entry; entry.pixels=pixels;
            if(FAILED(target_->CreateBitmap(D2D1::SizeU(pixels->width,pixels->height),pixels->bgra.data(),pixels->width*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),entry.bitmap.GetAddressOf()))) return;
            found=bitmaps_.emplace(pixels.get(),std::move(entry)).first;
            bitmap_bytes_+=pixels->bgra.size();
        }
        found->second.used=++bitmap_clock_;
        target_->DrawBitmap(found->second.bitmap.Get(),D2D1::RectF(x,y,x+w,y+h));
    }
    void draw_cover(std::shared_ptr<cover_pixels> pixels,bool artist,float x,float y,int width,int height,HDC dc) {
        const auto square=image_placement(1,1,width,height,2);
        if(square.w<=0 || square.h<=0) return;
        x+=float(square.x); y+=float(square.y);
        const int size=int(square.w);
        if(!pixels && !artist) { draw_default_cover(x,y,size,size,dc); return; }
        if(!pixels) { static const auto no_artist=artist_art_placeholder(); pixels=no_artist; }
        const auto r=image_placement(pixels->width,pixels->height,size,size,style_.artwork.aspect?2:0);
        draw_image(pixels,x+float(r.x),y+float(r.y),float(r.w),float(r.h),dc);
    }
    bool draw_wallpaper(HDC dc,const RECT& clip,bool group=false) {
        if(!wallpaper_.pixels || (style_.artwork.region==2 && !group)) return false;
        const auto& p=wallpaper_.pixels;
        if(dc) {
            const int saved=SaveDC(dc); IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);
            draw_surface_gdi(dc,*p,-wallpaper_.origin.x,-wallpaper_.origin.y); RestoreDC(dc,saved);
        } else {
            target_->PushAxisAlignedClip(D2D1::RectF(float(clip.left),float(clip.top),float(clip.right),float(clip.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
            draw_image(p,float(-wallpaper_.origin.x),float(-wallpaper_.origin.y),float(p->width),float(p->height),nullptr); target_->PopAxisAlignedClip();
        }
        return true;
    }
    void tint(HDC dc,RECT r,COLORREF c,unsigned alpha) {
        if(dc) tint_artwork_gdi(dc,r,c,alpha);
        else { auto value=color(c); value.a=alpha/255.f; brush_->SetColor(value); target_->FillRectangle(D2D1::RectF(float(r.left),float(r.top),float(r.right),float(r.bottom)),brush_.Get()); }
    }

    viewport_search search_;
    scroll_model scroll_;
    scrollbar_control scrollbar_;
    int scrollbar_width_=0;
    std::vector<unsigned char> selected_;
    std::vector<viewport_group> groups_;
    group_geometry group_layout_;
    size_t slot(int row) const { return row>=0 && size_t(row)<group_layout_.track_slots.size()?group_layout_.track_slots[row]:0; }
    size_t visual_count() const { return group_layout_.slots.size(); }
    int focus_ = -1, anchor_ = -1, horizontal_ = 0;
    int wheel_remainder_ = 0, horizontal_remainder_ = 0;
    bool redraw_ = true, laying_out_ = false, suspended_ = false;
    bool mouse_down_ = false, drag_sent_ = false, defer_single_ = false;
    bool touching_ = false;
    int edit_row_=-1, edit_column_=-1, edit_value_=0, edit_initial_=0;
    bool edit_moved_=false;
    ULONGLONG mouse_down_time_=0, drop_scroll_time_=0;
    int drop_slot_=-1;
    POINT mouse_start_{}, touch_previous_{};
    ULONGLONG last_frame_ = 0, touch_start_ = 0;
    RECT dirty_{};
    bool dirty_pending_ = false;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> text_factory_;
    ComPtr<IDWriteTextFormat> text_format_, extra_text_format_, group_text_format_;
    struct cached_cell {
        std::wstring text, secondary;
        bool state = false;
        special_column special=special_column::none;
        bool cover=false, artist=false;
        ComPtr<IDWriteTextLayout> filled_icon, empty_icon;
        ComPtr<IDWriteTextLayout> layout, secondary_layout;
        int width = -1, queue_text_width = -1;
    };
    std::map<int, std::vector<cached_cell>> cache_;

    static COLORREF mix(COLORREF background,COLORREF foreground,unsigned alpha) {
        alpha=std::min(255U,alpha);
        return RGB((GetRValue(background)*(255-alpha)+GetRValue(foreground)*alpha)/255,
                   (GetGValue(background)*(255-alpha)+GetGValue(foreground)*alpha)/255,
                   (GetBValue(background)*(255-alpha)+GetBValue(foreground)*alpha)/255);
    }
    viewport_row_request row_info(int row) {
        viewport_row_request info; info.row=row; info.global_index=info.group_index=row;
        notify(&info.hdr,viewport_row_info); return info;
    }
    COLORREF row_background(int row,const viewport_row_request& info) const {
        const auto background=style_.alternating && alternate_row(info.global_index,info.group_index,!groups_.empty()) ? style_.alternate : style_.row;
        return selected_[row] ? mix(background,style_.selection,style_.selection_alpha) : background;
    }
    COLORREF row_text(int row,COLORREF background,const viewport_row_request& info) const {
        if(info.playing) return style_.focus;
        if (!selected_[row] || !style_.selection_alpha) return style_.text;
        if (style_.selection_alpha==255) return style_.selected_text;
        return 299*GetRValue(background)+587*GetGValue(background)+114*GetBValue(background)>=128000 ? RGB(0,0,0) : RGB(255,255,255);
    }
    void hide_tooltip() {
        hover_row_=-1; tooltip_visible_=false;
        if (tooltip_) { TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; SendMessageW(tooltip_,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&tool)); }
    }
    static LRESULT CALLBACK tooltip_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto* self=reinterpret_cast<viewport*>(data);
        if (msg==WM_NCDESTROY) RemoveWindowSubclass(wnd,tooltip_proc,id);
        if (msg==WM_WINDOWPOSCHANGING && self->tooltip_updating_ && self->tooltip_visible_) {
            // UPDATETIPTEXT can run the native show/layout path even for an
            // already visible tracking tooltip. Correct it BEFORE the move,
            // so no intermediate cursor-relative position reaches the screen.
            auto& pos=*reinterpret_cast<WINDOWPOS*>(lp);
            RECT bounds{};
            if (GetWindowRect(wnd,&bounds)) {
                const LONG old_width=bounds.right-bounds.left, old_height=bounds.bottom-bounds.top;
                LONG width=(pos.flags&SWP_NOSIZE)?old_width:std::max(old_width,LONG(pos.cx));
                LONG height=(pos.flags&SWP_NOSIZE)?old_height:std::max(old_height,LONG(pos.cy));
                LONG x=bounds.left, y=bounds.top;
                MONITORINFO monitor{sizeof(monitor)};
                if (GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor)) {
                    const auto& work=monitor.rcWork;
                    width=std::min(width,work.right-work.left); height=std::min(height,work.bottom-work.top);
                    x=std::clamp(x,work.left,work.right-width); y=std::clamp(y,work.top,work.bottom-height);
                }
                // Keep the largest size for this hover; shorter dynamic values
                // must not make the popup oscillate. Only growth at an edge moves it.
                pos.x=int(x); pos.y=int(y); pos.cx=int(width); pos.cy=int(height);
                pos.flags&=~(SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW|SWP_HIDEWINDOW);
                pos.flags|=SWP_NOZORDER;
                if (x==bounds.left && y==bounds.top) pos.flags|=SWP_NOMOVE;
                if (width==old_width && height==old_height) pos.flags|=SWP_NOSIZE;
                return 0;
            }
        }
        return DefSubclassProc(wnd,msg,wp,lp);
    }
    void track_hover(POINT pt) {
        const int row=hit(pt);
        if (row!=hover_row_) { hide_tooltip(); hover_row_=row; }
        TRACKMOUSEEVENT track{sizeof(track),tooltip_visible_?TME_LEAVE:TME_HOVER|TME_LEAVE,window_,style_.tooltip_delay}; TrackMouseEvent(&track);
    }
    void show_tooltip() {
        if (tooltip_visible_ || !search_.overlay.empty() || !style_.tooltips || suspended_ || mouse_down_ || touching_ || scroll_.moving() || hover_row_<0 || hover_row_>=count()) return;
        viewport_tooltip_request request; request.row=hover_row_; notify(&request.hdr,viewport_tooltip_info);
        if (request.text.empty()) return;
        tooltip_text_=std::move(request.text);
        if (!tooltip_) {
            // Buffer native background/text painting and avoid replaying fade
            // or slide effects when changing playback text invokes its show path.
            tooltip_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_COMPOSITED,TOOLTIPS_CLASSW,nullptr,
                WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP|TTS_NOANIMATE|TTS_NOFADE,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!tooltip_) return;
            if (!SetWindowSubclass(tooltip_,tooltip_proc,1,reinterpret_cast<DWORD_PTR>(this))) {
                DestroyWindow(tooltip_); tooltip_=nullptr; return;
            }
            TOOLINFOW tool{sizeof(tool)}; tool.uFlags=TTF_TRACK|TTF_ABSOLUTE; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
            SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
        }
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
        SendMessageW(tooltip_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
        SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,std::max(240,style_.padding*70));
        SendMessageW(tooltip_,TTM_SETTITLEW,TTI_NONE,reinterpret_cast<LPARAM>(style_.selected_tooltips?L"Selected track":L"Track"));
        SendMessageW(tooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
        POINT pt{}; GetCursorPos(&pt);
        SendMessageW(tooltip_,TTM_TRACKPOSITION,0,MAKELPARAM(pt.x+12,pt.y+20));
        if (IsWindowVisible(window_)) { SendMessageW(tooltip_,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&tool)); tooltip_visible_=true; }
        RECT tip{}; GetWindowRect(tooltip_,&tip); MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int x=std::max(monitor.rcWork.left,std::min(tip.left,monitor.rcWork.right-(tip.right-tip.left)));
            const int y=std::max(monitor.rcWork.top,std::min(tip.top,monitor.rcWork.bottom-(tip.bottom-tip.top)));
            SetWindowPos(tooltip_,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        }
    }
    void refresh_tooltip(int row) {
        if (!tooltip_visible_ || !tooltip_ || (!style_.selected_tooltips && row>=0 && row!=hover_row_) || hover_row_<0 ||
            hover_row_>=count()) return;
        viewport_tooltip_request request; request.row=hover_row_; notify(&request.hdr,viewport_tooltip_info);
        if (request.text.empty()) { hide_tooltip(); return; }
        if (request.text==tooltip_text_) return;
        tooltip_text_=std::move(request.text);
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
        tooltip_updating_=true;
        SendMessageW(tooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
        tooltip_updating_=false;
    }
    void playback_clock() {
        const auto range=scroll_.visible(visual_count(),style_.row_height,page());
        const bool needed=!suspended_ && !paused_ && playing_row_>=0 && slot(playing_row_)>=range.first && slot(playing_row_)<range.second && IsWindowVisible(window_);
        if (needed) {
            const auto now=GetTickCount64();
            if (!play_timer_running_) {
                play_started_=now;
                play_timer_running_=SetTimer(window_,playback_timer,playback_interval_ms,nullptr)!=0;
            }
            // Use elapsed seconds so delayed timer messages cannot shift the phase.
            play_phase_=((now-play_started_)/playback_interval_ms)%2!=0;
        }
        if (!needed && play_timer_running_) { KillTimer(window_,playback_timer); play_timer_running_=false; play_phase_=false; }
    }
    RECT client() const { RECT r{}; GetClientRect(window_, &r); return r; }
    RECT body() const { auto r = client(); r.right=std::max(0L,r.right-scrollbar_width_); r.top = std::min(r.bottom, LONG(style_.header_height)); return r; }
    int page() const { const auto r = body(); return std::max(0L, r.bottom - r.top); }
    int count() const { return static_cast<int>(selected_.size()); }
    static D2D1_COLOR_F color(COLORREF c) { return D2D1::ColorF(GetRValue(c)/255.f, GetGValue(c)/255.f, GetBValue(c)/255.f); }
    void invalidate_body() { if (redraw_) { const auto r = body(); InvalidateRect(window_, &r, FALSE); } }
    RECT row_rect(int row) const {
        auto r = body();
        r.top = static_cast<LONG>(std::clamp(std::floor(style_.header_height + double(slot(row))*style_.row_height - scroll_.displayed),
            double(LONG_MIN), double(LONG_MAX-style_.row_height-1)));
        r.bottom = static_cast<LONG>(std::clamp(std::ceil(style_.header_height + double(slot(row)+1)*style_.row_height - scroll_.displayed),
            double(LONG_MIN), double(LONG_MAX)));
        return r;
    }
    void invalidate_row(int row) {
        if (!redraw_ || row < 0 || row >= count()) return;
        const auto bounds = body(), row_bounds = row_rect(row);
        RECT clipped{};
        if (IntersectRect(&clipped, &bounds, &row_bounds)) InvalidateRect(window_, &clipped, FALSE);
    }
    // Async metadata/cover producers use this path: one invalidation per frame.
    void queue_dirty(int row) {
        if(row<0 || row==edit_row_) cancel_edit();
        refresh_tooltip(row);
        if (row < 0) { cache_.clear(); dirty_ = body(); }
        else {
            cache_.erase(row);
            const auto r = row_rect(row), bounds = body(); RECT clipped{};
            if (!IntersectRect(&clipped, &r, &bounds)) return;
            if (dirty_pending_) { RECT joined{}; UnionRect(&joined, &dirty_, &clipped); dirty_ = joined; }
            else dirty_ = clipped;
        }
        if (!dirty_pending_ && !SetTimer(window_, dirty_timer, 16, nullptr)) {
            if (redraw_) InvalidateRect(window_, &dirty_, FALSE);
            return;
        }
        dirty_pending_ = true;
    }
    LRESULT notify(NMHDR* hdr, UINT code) {
        hdr->hwndFrom = window_; hdr->idFrom = GetDlgCtrlID(window_); hdr->code = code;
        return SendMessageW(GetParent(window_), WM_NOTIFY, hdr->idFrom, reinterpret_cast<LPARAM>(hdr));
    }
    void selection_changed() {
        hide_tooltip();
        if (!redraw_) return;
        NMLISTVIEW change{}; change.iItem = -1; change.uChanged = LVIF_STATE;
        notify(&change.hdr, LVN_ITEMCHANGED);
        if (accessible_) NotifyWinEvent(EVENT_OBJECT_SELECTIONWITHIN, window_, OBJID_CLIENT, CHILDID_SELF);
    }
    void focus(int row) {
        const int old = focus_; focus_ = row;
        invalidate_row(old); invalidate_row(focus_);
        if (accessible_ && redraw_ && row>=0) NotifyWinEvent(EVENT_OBJECT_FOCUS,window_,OBJID_CLIENT,row+1);
    }
    void select(int row, bool control, bool shift) {
        if (row < 0 || row >= count()) return;
        int first_dirty=count(), last_dirty=-1;
        auto set = [&](int i, bool value) {
            if ((selected_[i]!=0)==value) return;
            selected_[i]=value; first_dirty=std::min(first_dirty,i); last_dirty=std::max(last_dirty,i);
        };
        if (shift) {
            if (anchor_ < 0) anchor_ = focus_ >= 0 ? focus_ : row;
            const int first = std::min(anchor_, row), last = std::max(anchor_, row);
            for (int i = 0; i < count(); ++i) set(i,(i>=first && i<=last) || (control && selected_[i]));
        } else {
            for (int i = 0; i < count(); ++i) set(i,i==row ? (control ? !selected_[i] : true) : (control && selected_[i]));
            anchor_ = row;
        }
        if (last_dirty>=first_dirty && redraw_) {
            RECT changed=row_rect(first_dirty), last=row_rect(last_dirty), clipped{}, bounds=body();
            changed.bottom=last.bottom;
            if (IntersectRect(&clipped,&changed,&bounds)) InvalidateRect(window_,&clipped,FALSE);
        }
        focus(row); selection_changed();
    }
    int hit(POINT point) const {
        const auto r = body();
        if (point.x < r.left || point.x >= r.right) return -1;
        const int visual=scroll_.hit(point.y-r.top,style_.row_height,r.bottom-r.top,visual_count());
        return visual>=0?group_layout_.slots[visual].track:-1;
    }
    int content_width() const {
        int width = 0;
        for (int i = 0; i < Header_GetItemCount(header_); ++i) {
            HDITEMW item{}; item.mask = HDI_WIDTH; Header_GetItem(header_, i, &item); width += item.cxy;
        }
        return width;
    }
    void sync_scrollbar() { scrollbar_.position(scroll_.target); }
    void layout() {
        if (laying_out_ || !header_) return;
        laying_out_ = true;
        const int old_width=body().right;
        const int thickness=scrollbar_control::metric(SM_CXVSCROLL,style_.scrollbar_dpi);
        const double content=double(visual_count())*style_.row_height;
        // Never create a native horizontal bar: temporary column overflow while
        // resizing/refitting must not change the client height or flash nonclient UI.
        const auto bounds=client();
        scrollbar_width_=style_.show_scrollbar && content>page() && page()>0?
            std::min(thickness,int(bounds.right)):0;
        horizontal_=std::clamp(horizontal_,0,std::max(0,content_width()-int(body().right)));
        scroll_.extent(content,page());
        const auto r=client();
        SetWindowPos(header_,nullptr,-horizontal_,0,std::max(content_width(),int(bounds.right)+horizontal_),
            style_.header_height,SWP_NOZORDER|SWP_NOACTIVATE);
        scrollbar_.layout(r.right-scrollbar_width_,r.bottom,scrollbar_width_,std::min(int(r.bottom),style_.header_height),
            scrollbar_control::metric(SM_CYVSCROLL,style_.scrollbar_dpi),content,page(),scroll_.target,style_.row_height,scrollbar_width_>0);
        if (target_ && FAILED(target_->Resize(D2D1::SizeU(std::max(1L,r.right), std::max(1L,r.bottom))))) discard_target();
        laying_out_ = false;
        if(old_width!=body().right) { NMHDR changed{}; notify(&changed,viewport_width_changed); }
    }
    void animate() {
        if (scroll_.moving()) hide_tooltip();
        sync_scrollbar();
        if (scroll_.moving() && !last_frame_) {
            last_frame_ = GetTickCount64();
            if (!SetTimer(window_, frame_timer, 16, nullptr)) {
                scroll_.reset(scroll_.target); last_frame_ = 0; invalidate_body();
            }
        }
    }
    void stop_timer() { KillTimer(window_, frame_timer); last_frame_ = 0; }
    void reveal(int row) { if (row >= 0 && row < count()) { scroll_.reveal(slot(row), style_.row_height, page()); animate(); } }
    void vertical(WPARAM wp) {
        scroll_.cancel_inertia();
        switch (LOWORD(wp)) {
        case SB_LINEUP: scroll_.by(-style_.row_height); break;
        case SB_LINEDOWN: scroll_.by(style_.row_height); break;
        case SB_PAGEUP: scroll_.by(-page()); break;
        case SB_PAGEDOWN: scroll_.by(page()); break;
        case SB_TOP: scroll_.to(0); break;
        case SB_BOTTOM: scroll_.to(scroll_.maximum); break;
        }
        animate();
    }
    void horizontal(WPARAM wp) {
        hide_tooltip();
        switch (LOWORD(wp)) {
        case SB_LINELEFT: horizontal_ -= style_.row_height; break;
        case SB_LINERIGHT: horizontal_ += style_.row_height; break;
        case SB_PAGELEFT: horizontal_ -= body().right; break;
        case SB_PAGERIGHT: horizontal_ += body().right; break;
        case SB_LEFT: horizontal_ = 0; break;
        case SB_RIGHT: horizontal_ = content_width(); break;
        }
        layout(); invalidate_body();
    }
    void discard_target() { background_bitmap_={}; bitmaps_.clear(); bitmap_bytes_=0; brush_.Reset(); target_.Reset(); }
    void reset_text() { cancel_edit(); cache_.clear(); text_format_.Reset(); extra_text_format_.Reset(); group_text_format_.Reset(); hide_tooltip(); }
    bool resources() {
        if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
        if (!text_factory_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf())))) return false;
        if (!target_) {
            const auto r = client();
            // Retained contents allow genuine dirty-rectangle paints.
            auto props = D2D1::RenderTargetProperties(); props.dpiX = props.dpiY = 96;
            if (FAILED(factory_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(window_,
                D2D1::SizeU(std::max(1L,r.right),std::max(1L,r.bottom)), D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS), &target_))) return false;
            if (FAILED(target_->CreateSolidColorBrush(color(style_.text), &brush_))) { discard_target(); return false; }
        }
        if (!text_format_) {
            LOGFONTW lf{}; GetObjectW(font_, sizeof(lf), &lf);
            if (FAILED(text_factory_->CreateTextFormat(lf.lfFaceName[0] ? lf.lfFaceName : L"Segoe UI", nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(lf.lfWeight ? lf.lfWeight : FW_NORMAL),
                lf.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                float(std::max(1L,std::abs(lf.lfHeight))), L"", &text_format_))) return false;
            text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (FAILED(text_factory_->CreateTextFormat(lf.lfFaceName[0]?lf.lfFaceName:L"Segoe UI",nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(lf.lfWeight?lf.lfWeight:FW_NORMAL),lf.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,float(std::max(1L,std::abs(lf.lfHeight)))*.9f,L"",&extra_text_format_))) { text_format_.Reset(); return false; }
            extra_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            extra_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (!group_text_format_) {
            LOGFONTW lf{}; GetObjectW(group_font_?group_font_:font_,sizeof(lf),&lf);
            if (FAILED(text_factory_->CreateTextFormat(lf.lfFaceName[0]?lf.lfFaceName:L"Segoe UI",nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(lf.lfWeight?lf.lfWeight:FW_SEMIBOLD),lf.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,float(std::max(1L,std::abs(lf.lfHeight))),L"",&group_text_format_))) return false;
            group_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            group_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return true;
    }
    std::vector<cached_cell>& cells(int row) {
        auto found = cache_.find(row);
        if (found != cache_.end()) return found->second;
        std::vector<cached_cell> result(Header_GetItemCount(header_));
        for (int col = 0; col < static_cast<int>(result.size()); ++col) {
            NMLVDISPINFOW request{}; request.item.mask = LVIF_TEXT;
            request.item.iItem = row; request.item.iSubItem = col;
            notify(&request.hdr, LVN_GETDISPINFOW);
            if (request.item.pszText) result[col].text = request.item.pszText;
            viewport_cell_request extra; extra.row=row; extra.column=col; notify(&extra.hdr,viewport_cell_info);
            result[col].secondary=std::move(extra.secondary); result[col].state=extra.state_column; result[col].special=extra.special;
            result[col].cover=extra.cover; result[col].artist=extra.artist;
        }
        return cache_.emplace(row, std::move(result)).first->second;
    }
    struct column_geometry { RECT rect; int align; };
    void draw_row_cover(const cached_cell& cell,const column_geometry& column,int row,float y,HDC dc) {
        if(!groups_.empty()) return;
        const int width=column.rect.right-column.rect.left;
        const int margin=std::min(style_.cover_margin,std::max(0,(std::min(width,style_.row_height)-1)/2));
        if(width<=0 || column.rect.right<=0 || column.rect.left>=body().right) return;
        viewport_cover_request request; request.row=row; request.artist=cell.artist;
        notify(&request.hdr,viewport_row_cover);
        draw_cover(request.pixels,cell.artist,float(column.rect.left+margin),y+margin,width-2*margin,style_.row_height-2*margin,dc);
    }
    std::vector<column_geometry> geometry() const {
        std::vector<column_geometry> result;
        for (int i = 0; i < Header_GetItemCount(header_); ++i) {
            RECT r{}; Header_GetItemRect(header_, i, &r); OffsetRect(&r, -horizontal_, 0);
            HDITEMW item{}; item.mask = HDI_FORMAT; Header_GetItem(header_, i, &item);
            result.push_back({r, item.fmt & HDF_JUSTIFYMASK});
        }
        return result;
    }
    star_geometry icon_geometry(const column_geometry& g,special_column kind) const {
        const int width=std::max(0L,g.rect.right-g.rect.left-2*style_.padding);
        const int pitch=std::max(icon_pitch_,std::max(1,style_.padding*3));
        if(kind==special_column::rating) return stars(g.rect.left+style_.padding,width,pitch,g.align);
        auto result=stars(g.rect.left+style_.padding,std::min(width,pitch),pitch,0);
        result.left+=g.align==HDF_RIGHT?std::max(0,width-pitch):g.align==HDF_CENTER?std::max(0,width-pitch)/2:0;
        return result;
    }
    void cancel_edit() {
        const int old=edit_row_; edit_row_=edit_column_=-1; edit_moved_=false;
        if(old>=0) { invalidate_row(old); if(GetCapture()==window_) ReleaseCapture(); }
    }
    bool begin_edit(POINT pt) {
        const int row=hit(pt); if(row<0) return false;
        const auto columns=geometry(); auto& values=cells(row);
        for(size_t col=0;col<columns.size();++col) {
            const auto& g=columns[col]; auto& cell=values[col];
            if(pt.x<g.rect.left || pt.x>=g.rect.right || cell.special==special_column::none) continue;
            const int value=icon_geometry(g,cell.special).hit(pt.x);
            // Blank space in a special cell still consumes the click.
            if(!value) return true;
            const auto text=parse_colors(cell.text).text;
            edit_row_=row; edit_column_=int(col); edit_moved_=false;
            edit_initial_=cell.special==special_column::rating?rating_value(text):int(mood_value(text));
            edit_value_=cell.special==special_column::rating?value:1-edit_initial_;
            SetCapture(window_); invalidate_row(row); return true;
        }
        return false;
    }
    void move_edit(POINT pt) {
        if(edit_row_<0) return;
        const auto columns=geometry();
        if(edit_column_<0 || size_t(edit_column_)>=columns.size()) { cancel_edit(); return; }
        auto& cell=cells(edit_row_)[edit_column_];
        if(cell.special!=special_column::rating || hit(pt)!=edit_row_) return;
        const int value=icon_geometry(columns[edit_column_],cell.special).hit(pt.x);
        if(value && value!=edit_value_) { edit_value_=value; edit_moved_=true; invalidate_row(edit_row_); }
    }
    void finish_edit(POINT pt) {
        if(edit_row_<0) return;
        move_edit(pt); if(edit_row_<0) return;
        const auto columns=geometry(); const auto kind=cells(edit_row_)[edit_column_].special;
        const bool commit=hit(pt)==edit_row_ && icon_geometry(columns[edit_column_],kind).hit(pt.x)>0;
        viewport_special_request request; request.row=edit_row_; request.column=edit_column_;
        request.value=kind==special_column::rating && !edit_moved_ && edit_value_==edit_initial_?0:edit_value_;
        cancel_edit(); // Release capture before a host command can pump messages.
        if(commit) notify(&request.hdr,viewport_special_edit);
    }
    void draw_special(cached_cell& cell,const column_geometry& column,int row,float y,COLORREF foreground,HDC dc) {
        const auto parsed=parse_colors(cell.text);
        const bool heart=cell.special==special_column::mood;
        const auto accent=parsed.runs.empty()?(heart?RGB(255,120,170):RGB(255,255,50)):COLORREF(parsed.runs.front().color);
        const auto g=icon_geometry(column,cell.special);
        const int value=edit_row_==row && edit_column_>=0 && &cell==&cells(row)[edit_column_]?edit_value_:
            cell.special==special_column::rating?rating_value(parsed.text):int(mood_value(parsed.text));
        const int icon_size=heart?style_.mood_icon_size:style_.rating_icon_size;
        for(int i=0;i<g.count;++i) {
            const bool filled=i<value, dot=!heart && !filled && style_.rating_dots;
            const auto ink=filled?accent:foreground;
            // Tiny dots need more contrast than the much larger empty stars.
            const unsigned opacity=filled?255U:heart?0x16U:dot?0x60U:0x20U;
            // Drawing size is independent of the full, unchanged click/drag slot.
            const unsigned size=unsigned(std::clamp(std::min({dot?style_.rating_dot_size:icon_size,g.pitch,style_.row_height}),1,256));
            const uint64_t shape=dot?5:heart?1:0; // Cache shapes 2-4 belong to State.
            const uint64_t key=uint64_t(ink)|(uint64_t(size)<<24)|(shape<<40)|(uint64_t(opacity)<<48);
            auto found=special_icons_.find(key);
            if(found==special_icons_.end()) {
                if(special_icons_.size()>=64) special_icons_.clear();
                found=special_icons_.emplace(key,dot?rating_dot_icon(size,ink,opacity):special_icon(heart,size,ink,opacity)).first;
            }
            draw_image(found->second,float(g.left+i*g.pitch+(g.pitch-int(size))/2),
                y+(style_.row_height-int(size))/2,float(size),float(size),dc);
        }
    }
    void prune_cache(int first, int end) {
        const int margin = std::max(8, end-first);
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first < first-margin || it->first >= end+margin) it = cache_.erase(it);
            else ++it;
        }
    }
    void state_label(cached_cell& value,const viewport_row_request& info,bool selected) {
        if (!value.state) return;
        std::wstring label=info.playing ? (info.paused || play_phase_?L"▷":L"▶") : selected && info.queue.empty()?L"✓":L"";
        if (!info.queue.empty()) { if(!label.empty()) label+=L" "; label+=info.queue; }
        if (label!=value.text) { value.text=std::move(label); value.layout.Reset(); value.queue_text_width=-1; }
    }
    void draw_line(const std::wstring& input,ComPtr<IDWriteTextLayout>& layout,IDWriteTextFormat* format,
                   int width,int height,int alignment,float x,float y,COLORREF foreground,bool highlight=true) {
        const bool colored=input.find(wchar_t(3))!=std::wstring::npos;
        const auto parsed=colored?parse_colors(input):colored_text{}; const auto& text=colored?parsed.text:input;
        if (!layout && SUCCEEDED(text_factory_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format,float(width),float(height),&layout))) {
            layout->SetTextAlignment(alignment==HDF_RIGHT?DWRITE_TEXT_ALIGNMENT_TRAILING:alignment==HDF_CENTER?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(text_factory_->CreateEllipsisTrimmingSign(format,&ellipsis))) {
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0}; layout->SetTrimming(&trim,ellipsis.Get());
            }
        }
        if(layout) {
            // Layouts outlive render targets: never retain a target-owned brush in a layout.
            layout->SetDrawingEffect(nullptr,{0,static_cast<UINT32>(text.size())});
            if(highlight) for(const auto& match:search_matches(text,search_.terms)) {
                UINT32 count=0;
                layout->HitTestTextRange(UINT32(match.start),UINT32(match.length),x,y,nullptr,0,&count);
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if(count && SUCCEEDED(layout->HitTestTextRange(UINT32(match.start),UINT32(match.length),x,y,metrics.data(),count,&count))) {
                    brush_->SetColor(color(search_.color));
                    for(const auto& rect:metrics) if(!rect.isTrimmed && rect.isText) {
                        const float left=std::max(x,rect.left),right=std::min(x+width,rect.left+rect.width);
                        const float top=std::max(y,rect.top),bottom=std::min(y+height,rect.top+rect.height);
                        if(right>left && bottom>top) target_->FillRectangle(D2D1::RectF(left,top,right,bottom),brush_.Get());
                    }
                }
            }
        }
        std::vector<ComPtr<ID2D1SolidColorBrush>> run_brushes;
        if(layout) for(const auto& run:parsed.runs) {
            ComPtr<ID2D1SolidColorBrush> ink;
            if(SUCCEEDED(target_->CreateSolidColorBrush(color(COLORREF(run.color)),&ink))) {
                layout->SetDrawingEffect(ink.Get(),{UINT32(run.start),UINT32(run.length)}); run_brushes.push_back(ink);
            }
        }
        ComPtr<ID2D1SolidColorBrush> highlighted;
        if(layout && highlight && !search_.terms.empty() && SUCCEEDED(target_->CreateSolidColorBrush(color(highlight_text()),&highlighted)))
            for(const auto& match:search_matches(text,search_.terms)) layout->SetDrawingEffect(highlighted.Get(),{UINT32(match.start),UINT32(match.length)});
        brush_->SetColor(color(foreground));
        if (layout) target_->DrawTextLayout(D2D1::Point2F(x,y),layout.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
        if(layout) layout->SetDrawingEffect(nullptr,{0,static_cast<UINT32>(text.size())});
    }
    void draw_state(cached_cell& value,const viewport_row_request& info,bool selected,
                    const column_geometry& column,float y,COLORREF foreground,HDC dc) {
        const int left=column.rect.left+style_.padding, right=column.rect.right-style_.padding;
        const int width=right-left; if(width<=0) return;
        const bool symbol=info.playing || (selected && info.queue.empty());
        const int size=std::min(icon_pitch_,style_.row_height),gap=std::max(1,style_.padding/2);
        int queue_width=0;
        if(!info.queue.empty()) {
            // Measure with the renderer that will draw the queue text. GDI's
            // integer advances can be narrower than DirectWrite's fractional
            // advances, making even "01" ellipsize in an otherwise wide cell.
            if(dc) {
                auto old=SelectObject(dc,font_); RECT extent{};
                DrawTextW(dc,info.queue.c_str(),int(info.queue.size()),&extent,DT_LEFT|DT_SINGLELINE|DT_CALCRECT|DT_NOPREFIX);
                SelectObject(dc,old); queue_width=int(extent.right-extent.left);
            } else {
                if(value.queue_text_width<0) {
                    ComPtr<IDWriteTextLayout> measured;
                    if(SUCCEEDED(text_factory_->CreateTextLayout(info.queue.c_str(),static_cast<UINT32>(info.queue.size()),
                        text_format_.Get(),float(width),float(style_.row_height),&measured))) {
                        DWRITE_TEXT_METRICS metrics{};
                        if(SUCCEEDED(measured->GetMetrics(&metrics))) value.queue_text_width=int(std::ceil(metrics.widthIncludingTrailingWhitespace));
                    }
                }
                queue_width=std::max(0,value.queue_text_width);
            }
        }
        const int marker=symbol?std::min(size,width):0;
        const int spacing=marker && queue_width?std::min(gap,std::max(0,width-marker)):0;
        const int text_width=std::min(queue_width,std::max(0,width-marker-spacing));
        const int total=marker+spacing+text_width;
        const int x=left+(column.align==HDF_RIGHT?width-total:column.align==HDF_CENTER?(width-total)/2:0);
        if(marker) {
            const unsigned kind=info.playing?(info.paused || play_phase_?2:1):0;
            const uint64_t key=uint64_t(foreground)|(uint64_t(marker)<<24)|(uint64_t(kind+2)<<40);
            auto found=special_icons_.find(key);
            if(found==special_icons_.end()) {
                if(special_icons_.size()>=64) special_icons_.clear();
                found=special_icons_.emplace(key,raster_state_icon(kind,marker,foreground)).first;
            }
            draw_image(found->second,float(x),y+(style_.row_height-marker)/2.f,float(marker),float(marker),dc);
        }
        if(text_width>0) {
            if(dc) {
                RECT rect{x+marker+spacing,LONG(y),x+total,LONG(y)+style_.row_height};
                SelectObject(dc,font_); SetTextColor(dc,foreground);
                DrawTextW(dc,info.queue.c_str(),int(info.queue.size()),&rect,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            } else {
                // State layouts contain queue text only; the glyph is drawn above.
                if(value.width!=text_width) { value.layout.Reset(); value.width=text_width; }
                draw_line(info.queue,value.layout,text_format_.Get(),text_width,style_.row_height,HDF_LEFT,
                    float(x+marker+spacing),y,foreground,false);
            }
        }
    }
    COLORREF highlight_text() const {
        const auto c=search_.color;
        return 299*GetRValue(c)+587*GetGValue(c)+114*GetBValue(c)>=128000?RGB(0,0,0):RGB(255,255,255);
    }
    void draw_gdi_line(HDC dc,const std::wstring& input,RECT rect,UINT flags,bool highlight=true) {
        const bool colored=input.find(wchar_t(3))!=std::wstring::npos;
        const auto parsed=colored?parse_colors(input):colored_text{}; const auto& text=colored?parsed.text:input;
        // Ask GDI for the actual ellipsized string so hidden matches and the
        // ellipsis itself are never highlighted. Extra capacity is required by GDI.
        std::vector<wchar_t> display(text.begin(),text.end()); display.resize(text.size()+5,0);
        RECT measured=rect;
        DrawTextExW(dc,display.data(),int(text.size()),&measured,flags|DT_MODIFYSTRING,nullptr);
        std::wstring shown(display.data());
        size_t visible=shown.size();
        if(shown!=text && visible>=3 && shown.substr(visible-3)==L"...") visible-=3;
        SIZE size{}; GetTextExtentPoint32W(dc,shown.c_str(),int(shown.size()),&size);
        const int x=(flags&DT_RIGHT)?rect.right-size.cx:(flags&DT_CENTER)?rect.left+(rect.right-rect.left-size.cx)/2:rect.left;
        const auto foreground=GetTextColor(dc);
        for(const auto& run:parsed.runs) {
            const auto end=std::min(visible,run.start+run.length); if(run.start>=end) continue;
            SIZE a{},b{}; GetTextExtentPoint32W(dc,shown.c_str(),int(run.start),&a); GetTextExtentPoint32W(dc,shown.c_str(),int(end),&b);
            const int saved=SaveDC(dc); IntersectClipRect(dc,std::max<LONG>(rect.left,x+a.cx),rect.top,std::min<LONG>(rect.right,x+b.cx),rect.bottom);
            SetTextColor(dc,COLORREF(run.color)); DrawTextW(dc,text.c_str(),int(text.size()),&rect,flags); RestoreDC(dc,saved);
        }
        if(!highlight || search_.terms.empty()) return;
        for(const auto& match:search_matches(shown.substr(0,visible),search_.terms)) {
            SIZE start{},end{};
            GetTextExtentPoint32W(dc,shown.c_str(),int(match.start),&start);
            GetTextExtentPoint32W(dc,shown.c_str(),int(match.start+match.length),&end);
            RECT mark{x+start.cx,rect.top+(rect.bottom-rect.top-size.cy)/2,x+end.cx,rect.top+(rect.bottom-rect.top+size.cy)/2};
            RECT clipped{}; if(!IntersectRect(&clipped,&mark,&rect)) continue;
            const int saved=SaveDC(dc); IntersectClipRect(dc,clipped.left,clipped.top,clipped.right,clipped.bottom);
            SetDCBrushColor(dc,search_.color); FillRect(dc,&clipped,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetTextColor(dc,highlight_text());
            DrawTextW(dc,text.c_str(),int(text.size()),&rect,flags);
            RestoreDC(dc,saved);
        }
        SetTextColor(dc,foreground);
    }
    void draw_search_overlay(HDC dc) {
        if(search_.overlay.empty()) return;
        const auto bounds=body(); const int pad=std::max(4,style_.padding*2);
        const int height=std::min(int(bounds.bottom-bounds.top),std::max(40,style_.row_height*2));
        RECT rect{bounds.left+pad,(bounds.top+bounds.bottom-height)/2,bounds.right-pad,(bounds.top+bounds.bottom+height)/2};
        if(rect.right<=rect.left || height<=0) return;
        const std::wstring label=search_.overlay+(search_.found?L"":L" — No match");
        const auto bg=mix(style_.row,style_.text,25);
        LOGFONTW lf{}; GetObjectW(font_,sizeof(lf),&lf); lf.lfHeight=-std::max(20L,std::abs(lf.lfHeight)*2); lf.lfWeight=FW_SEMIBOLD;
        if(dc) {
            SetDCBrushColor(dc,bg); FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            HFONT large=CreateFontIndirectW(&lf); auto old=SelectObject(dc,large?large:font_);
            SetTextColor(dc,style_.text); InflateRect(&rect,-pad,0);
            DrawTextW(dc,label.c_str(),int(label.size()),&rect,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(dc,old); if(large) DeleteObject(large);
        } else {
            brush_->SetColor(color(bg)); target_->FillRectangle(D2D1::RectF(float(rect.left),float(rect.top),float(rect.right),float(rect.bottom)),brush_.Get());
            ComPtr<IDWriteTextFormat> format;
            if(SUCCEEDED(text_factory_->CreateTextFormat(lf.lfFaceName[0]?lf.lfFaceName:L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,float(-lf.lfHeight),L"",&format))) {
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                ComPtr<IDWriteTextLayout> layout;
                draw_line(label,layout,format.Get(),std::max(1,int(rect.right-rect.left)-2*pad),height,HDF_CENTER,float(rect.left+pad),float(rect.top),style_.text,false);
            }
        }
    }
    void draw_drop_marker(HDC dc) {
        if(drop_slot_<0) return;
        const auto bounds=body();
        const int y=std::clamp(int(style_.header_height+double(drop_slot_)*style_.row_height-scroll_.displayed),int(bounds.top),std::max(int(bounds.top),int(bounds.bottom)-2));
        RECT line{bounds.left,y,bounds.right,y+std::max(2,style_.padding/2)};
        if(dc) { SetDCBrushColor(dc,style_.selection); FillRect(dc,&line,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); }
        else { brush_->SetColor(color(style_.selection)); target_->FillRectangle(D2D1::RectF(float(line.left),float(line.top),float(line.right),float(line.bottom)),brush_.Get()); }
    }
    void drop_hit(viewport_drop_position& result) {
        result.valid=false; result.row=result.group=-1; result.after=false;
        const auto bounds=body();
        if(suspended_ || !PtInRect(&bounds,result.point)) { drop_slot_=-1; invalidate_body(); return; }
        hide_tooltip();
        const auto now=GetTickCount64();
        if(result.scroll && now-drop_scroll_time_>=60) {
            const int edge=std::min(style_.row_height,page()/3);
            if(result.point.y<bounds.top+edge) { scroll_.by(-style_.row_height); animate(); }
            else if(result.point.y>=bounds.bottom-edge) { scroll_.by(style_.row_height); animate(); }
            drop_scroll_time_=now;
        }
        result.valid=true;
        const double offset=result.point.y-bounds.top+scroll_.displayed;
        const int visual=int(offset/style_.row_height);
        int marker=int(visual_count());
        if(visual>=0 && visual<int(visual_count())) {
            const auto& entry=group_layout_.slots[visual];
            result.row=entry.track; result.group=entry.group;
            if(entry.track>=0) {
                result.after=offset-visual*style_.row_height>=style_.row_height/2.0;
                marker=visual+int(result.after);
            } else if(entry.line>=0) {
                // Top/bottom half of the two-line header means before/after group.
                result.after=entry.line==1;
                marker=visual-entry.line;
                if(result.after) {
                    marker+=2+int(groups_[entry.group].band.count+groups_[entry.group].band.padding);
                }
            } else {
                result.after=true; marker=visual+1;
                while(marker<int(visual_count()) && group_layout_.slots[marker].group==entry.group &&
                    group_layout_.slots[marker].line==-1) ++marker;
            }
        }
        if(marker!=drop_slot_) { drop_slot_=marker; invalidate_body(); }
    }
    void paint(HDC print_dc = nullptr) {
        PAINTSTRUCT ps{}; const HDC dc=print_dc?print_dc:BeginPaint(window_,&ps);
        struct paint_guard { HWND w; PAINTSTRUCT* p; ~paint_guard(){if(p) EndPaint(w,p);} } guard{window_,print_dc?nullptr:&ps};
        if (!redraw_) return;
        const auto bounds=body(); if(bounds.bottom<=bounds.top || bounds.right<=0) return;
        wallpaper_={}; notify(&wallpaper_.hdr,viewport_background);
        if(!wallpaper_.pixels) background_bitmap_={};
        const auto columns=geometry(); const auto range=scroll_.visible(visual_count(),style_.row_height,page());
        const int first=static_cast<int>(range.first),end=static_cast<int>(range.second); prune_cache(static_cast<int>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),range.first)-group_layout_.track_slots.begin()),
            static_cast<int>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),range.second)-group_layout_.track_slots.begin())); playback_clock();
        const int primary_height=(style_.row_height+1)/2, secondary_top=primary_height;
        const bool fresh=!target_; bool drawn=!print_dc && resources();
        if (drawn) {
            target_->BeginDraw(); target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            RECT dirty=fresh?bounds:ps.rcPaint; IntersectRect(&dirty,&dirty,&bounds);
            target_->PushAxisAlignedClip(D2D1::RectF(float(dirty.left),float(dirty.top),float(dirty.right),float(dirty.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
            brush_->SetColor(color(style_.row)); target_->FillRectangle(D2D1::RectF(0,float(bounds.top),float(bounds.right),float(bounds.bottom)),brush_.Get());
            const bool wallpaper=draw_wallpaper(nullptr,bounds);
            for(int visual=first;visual<end;++visual) {
                const auto& entry=group_layout_.slots[visual]; const int row=entry.track;
                const float y=float(style_.header_height+double(visual)*style_.row_height-scroll_.displayed);
                if(row<0) { if(entry.line==0 || (entry.line==1 && visual==first)) draw_group(entry.group,y-entry.line*style_.row_height,nullptr); continue; }
                if(y+style_.row_height<=dirty.top || y>=dirty.bottom) continue;
                const auto info=row_info(row); const auto background=row_background(row,info),foreground=row_text(row,background,info);
                if(!wallpaper) { brush_->SetColor(color(background)); target_->FillRectangle(D2D1::RectF(0,y,float(bounds.right),y+style_.row_height),brush_.Get()); }
                else {
                    D2D1_COLOR_F overlay=color(selected_[row]?style_.selection:style_.alternate);
                    overlay.a=selected_[row]?style_.selection_alpha/255.f:style_.alternating && alternate_row(info.global_index,info.group_index,!groups_.empty())?.12f:0.f;
                    brush_->SetColor(overlay); target_->FillRectangle(D2D1::RectF(0,y,float(bounds.right),y+style_.row_height),brush_.Get());
                }
                auto& values=cells(row);
                for(size_t col=0;col<columns.size();++col) {
                    const auto& g=columns[col]; const int width=g.rect.right-g.rect.left-2*style_.padding;
                    if(width<=0 || g.rect.right<=dirty.left || g.rect.left>=dirty.right) continue;
                    auto& value=values[col]; state_label(value,info,selected_[row]!=0);
                    if(value.state) { draw_state(value,info,selected_[row]!=0,g,y,foreground,nullptr); continue; }
                    if(value.cover) { draw_row_cover(value,g,row,y,nullptr); continue; }
                    if(value.special!=special_column::none) { draw_special(value,columns[col],row,y,foreground,nullptr); continue; }
                    if(value.width!=width) { value.layout.Reset(); value.secondary_layout.Reset(); value.width=width; }
                    const bool extra=style_.extra_line && !value.state;
                    draw_line(value.text,value.layout,text_format_.Get(),width,extra?primary_height:style_.row_height,g.align,float(g.rect.left+style_.padding),y,foreground,!value.state);
                    if(extra && !value.secondary.empty()) draw_line(value.secondary,value.secondary_layout,extra_text_format_.Get(),width,
                        style_.row_height-secondary_top,g.align,float(g.rect.left+style_.padding),y+secondary_top,
                        info.playing?foreground:style_.derived_extra_color?mix(background,foreground,165):style_.secondary);
                }
                if(row==focus_ && GetFocus()==window_ && style_.focus_alpha) {
                    brush_->SetColor(color(mix(background,style_.focus,style_.focus_alpha)));
                    target_->DrawRectangle(D2D1::RectF(.5f,y+.5f,float(bounds.right)-.5f,y+style_.row_height-.5f),brush_.Get());
                }
            }
            draw_search_overlay(nullptr);
            draw_drop_marker(nullptr);
            target_->PopAxisAlignedClip(); if(FAILED(target_->EndDraw())) { discard_target(); drawn=false; invalidate_body(); }
        }
        if(!drawn) {
            HDC memory=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);
            HGDIOBJ old_bitmap=bitmap && memory?SelectObject(memory,bitmap):nullptr; HDC out=old_bitmap?memory:dc;
            const int saved=SaveDC(out); IntersectClipRect(out,bounds.left,bounds.top,bounds.right,bounds.bottom); SetBkMode(out,TRANSPARENT);
            auto fill=[&](RECT rect,COLORREF c){SetDCBrushColor(out,c); FillRect(out,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}; fill(bounds,style_.row);
            const bool wallpaper=draw_wallpaper(out,bounds);
            for(int visual=first;visual<end;++visual) {
                const auto& entry=group_layout_.slots[visual]; const int row=entry.track;
                if(row<0) { if(entry.line==0 || (entry.line==1 && visual==first)) draw_group(entry.group,float(style_.header_height+double(visual-entry.line)*style_.row_height-scroll_.displayed),out); continue; }
                auto rect=row_rect(row); const auto info=row_info(row); const auto background=row_background(row,info),foreground=row_text(row,background,info);
                if(!wallpaper) fill(rect,background);
                else if(selected_[row]) tint(out,rect,style_.selection,style_.selection_alpha);
                else if(style_.alternating && alternate_row(info.global_index,info.group_index,!groups_.empty())) tint(out,rect,style_.alternate,31);
                auto& values=cells(row);
                for(size_t col=0;col<columns.size();++col) {
                    auto& value=values[col]; state_label(value,info,selected_[row]!=0);
                    if(value.state) { draw_state(value,info,selected_[row]!=0,columns[col],float(rect.top),foreground,out); continue; }
                    if(value.cover) { draw_row_cover(value,columns[col],row,float(rect.top),out); continue; }
                    if(value.special!=special_column::none) { draw_special(value,columns[col],row,float(rect.top),foreground,out); continue; }
                    const bool extra=style_.extra_line && !value.state;
                    RECT cell{columns[col].rect.left+style_.padding,rect.top,columns[col].rect.right-style_.padding,extra?rect.top+primary_height:rect.bottom};
                    if(cell.right<=cell.left) continue;
                    const UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(columns[col].align==HDF_RIGHT?DT_RIGHT:columns[col].align==HDF_CENTER?DT_CENTER:DT_LEFT);
                    SelectObject(out,font_); SetTextColor(out,foreground); draw_gdi_line(out,value.text,cell,flags,!value.state);
                    if(extra && !value.secondary.empty()) {
                        cell.top=rect.top+secondary_top; cell.bottom=rect.bottom; SelectObject(out,extra_font_?extra_font_:font_);
                        SetTextColor(out,info.playing?foreground:style_.derived_extra_color?mix(background,foreground,165):style_.secondary);
                        draw_gdi_line(out,value.secondary,cell,flags);
                    }
                }
                if(row==focus_ && GetFocus()==window_ && style_.focus_alpha) {
                    const auto old_pen=SelectObject(out,GetStockObject(DC_PEN)), old_brush=SelectObject(out,GetStockObject(NULL_BRUSH));
                    SetDCPenColor(out,mix(background,style_.focus,style_.focus_alpha)); Rectangle(out,rect.left,rect.top,rect.right,rect.bottom);
                    SelectObject(out,old_pen); SelectObject(out,old_brush);
                }
            }
            draw_search_overlay(out);
            draw_drop_marker(out);
            RestoreDC(out,saved);
            if(old_bitmap){BitBlt(dc,0,bounds.top,bounds.right,bounds.bottom-bounds.top,memory,0,bounds.top,SRCCOPY);SelectObject(memory,old_bitmap);}
            if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory);
        }
    }
    void draw_group(int index,float y,HDC dc) {
        const auto& g=groups_[index]; const auto bounds=body();
        const int h=style_.row_height,p=style_.padding;
        const auto columns=geometry();
        const RECT cover_column=g.cover && g.cover_column>=0 && size_t(g.cover_column)<columns.size()?
            columns[g.cover_column].rect:RECT{0,0,g.cover?2*h:0,0};
        const float left=float(std::max(0L,cover_column.right)+p+2*h*int(g.artist_art)), right=float(bounds.right-p);
        const int available=std::max(0,int(right-left)), left_width=available*2/3, right_width=available-left_width;
        const auto bg=mix(style_.row,style_.text,12);
        const auto primary=index==playing_group_?style_.focus:style_.text;
        const auto secondary=index==playing_group_?style_.focus:style_.secondary;
        const std::wstring lines[]={g.l1,g.r1,g.l2,g.r2};
        if(dc) {
            RECT band{0,LONG(y),bounds.right,LONG(y+2*h)}; SetDCBrushColor(dc,bg);
            FillRect(dc,&band,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(draw_wallpaper(dc,band,true)) tint(dc,band,style_.row,36);
            for(int line=0;line<4;++line) {
                RECT r{LONG(left+(line%2?left_width:0)),LONG(y+(line/2)*h),LONG(line%2?right:left+left_width-p),LONG(y+(line/2+1)*h)};
                SelectObject(dc,line<2?(group_font_?group_font_:font_):(extra_font_?extra_font_:font_));
                SetTextColor(dc,line<2?primary:secondary);
                draw_gdi_line(dc,lines[line],r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(line%2?DT_RIGHT:DT_LEFT));
            }
        } else {
            brush_->SetColor(color(bg)); target_->FillRectangle(D2D1::RectF(0,y,float(bounds.right),y+2*h),brush_.Get());
            RECT band{0,LONG(std::floor(y)),bounds.right,LONG(std::ceil(y+2*h))};
            if(draw_wallpaper(nullptr,band,true)) tint(nullptr,band,style_.row,36);
            for(int line=0;line<4;++line) {
                ComPtr<IDWriteTextLayout> layout;
                draw_line(lines[line],layout,line<2?group_text_format_.Get():extra_text_format_.Get(),
                    std::max(0,(line%2?right_width:left_width-p)),h,line%2?HDF_RIGHT:HDF_LEFT,
                    left+(line%2?left_width:0),y+(line/2)*h,line<2?primary:secondary);
            }
        }
        for(int kind=0;kind<2;++kind) {
            if(kind==0?!g.cover:!g.artist_art) continue;
            const int offset=kind==0?int(cover_column.left):std::max(0,int(cover_column.right));
            const int width=kind==0?int(cover_column.right-cover_column.left):2*h;
            if(offset+width<=0 || offset>=bounds.right) continue;
            viewport_group_request request; request.group=index; request.artist=kind==1;
            notify(&request.hdr,viewport_group_cover); auto pixels=request.pixels;
            const int margin=std::min(style_.cover_margin,std::max(0,(std::min(width,2*h)-1)/2));
            draw_cover(pixels,kind==1,float(offset+margin),y+margin,width-2*margin,2*h-2*margin,dc);
        }
        // Snap to one physical pixel, including during fractional smooth scrolling.
        // Draw last so zero-margin artwork cannot cover the separator.
        const LONG top=LONG(std::round(y));
        const auto divider=mix(style_.row,style_.text,25);
        if(dc) {
            RECT line{bounds.left,top,bounds.right,top+1}; SetDCBrushColor(dc,divider);
            FillRect(dc,&line,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        } else {
            target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            brush_->SetColor(color(divider));
            target_->FillRectangle(D2D1::RectF(float(bounds.left),float(top),float(bounds.right),float(top+1)),brush_.Get());
        }
    }

    bool key(WPARAM key) {
        hide_tooltip();
        if (!count()) return false;
        const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        const int step=std::max(1,page()/style_.row_height);
        int row=focus_ < 0 ? 0 : focus_;
        switch(key) {
        case VK_UP: --row; break;
        case VK_DOWN: if (focus_>=0) ++row; break;
        case VK_PRIOR: row-=step; break;
        case VK_NEXT: row+=step; break;
        case VK_HOME: row=0; break;
        case VK_END: row=count()-1; break;
        case VK_SPACE: select(row,control,shift); return true;
        default: return false;
        }
        row=std::clamp(row,0,count()-1); scroll_.cancel_inertia();
        if (control && !shift) { focus(row); selection_changed(); }
        else select(row,control,shift);
        reveal(row); return true;
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        switch(msg) {
        case WM_CREATE: {
            header_=CreateWindowExW(0,WC_HEADERW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|HDS_BUTTONS|HDS_FULLDRAG,
                0,0,0,0,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!header_) return -1;
            if(!scrollbar_.create(window_,[this](double target) {
                if(suspended_) return;
                cancel_edit(); hide_tooltip(); scroll_.cancel_inertia(); scroll_.to(target); animate();
            })) return -1;
            GESTURECONFIG pan{GID_PAN,GC_PAN|GC_PAN_WITH_SINGLE_FINGER_VERTICALLY|GC_PAN_WITH_GUTTER,GC_PAN_WITH_INERTIA};
            SetGestureConfig(window_,0,1,&pan,sizeof(pan));
            return 0;
        }
        case WM_DESTROY:
            scrollbar_.destroy();
            cancel_edit();
            stop_timer(); KillTimer(window_,drag_timer); KillTimer(window_,dirty_timer); KillTimer(window_,playback_timer);
            if (tooltip_) { DestroyWindow(tooltip_); tooltip_=nullptr; }
            if (extra_font_) { DeleteObject(extra_font_); extra_font_=nullptr; }
            if (group_font_) { DeleteObject(group_font_); group_font_=nullptr; }
            if (accessible_) accessible_->disconnect();
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lp)==OBJID_CLIENT) {
                if (!accessible_) accessible_.Attach(new viewport_accessibility(window_));
                return LresultFromObject(IID_IAccessible,wp,accessible_.Get());
            }
            break;
        case viewport_access_action: {
            const int row=static_cast<int>(wp);
            if (suspended_ || row<0 || row>=count()) return FALSE;
            if (lp==-1) {
                select(row,false,false); NMITEMACTIVATE item{}; item.iItem=row; notify(&item.hdr,NM_DBLCLK);
            } else {
                if (lp&SELFLAG_TAKEFOCUS) { SetFocus(window_); focus(row); }
                if (lp&SELFLAG_TAKESELECTION) select(row,false,false);
                else if (lp&SELFLAG_EXTENDSELECTION) select(row,false,true);
                else {
                    if (lp&SELFLAG_ADDSELECTION) selected_[row]=true;
                    if (lp&SELFLAG_REMOVESELECTION) selected_[row]=false;
                    invalidate_row(row); selection_changed();
                }
                reveal(row);
            }
            return TRUE;
        }
        case viewport_drop_hit: drop_hit(*reinterpret_cast<viewport_drop_position*>(lp)); return 0;
        case viewport_drop_clear: drop_slot_=-1; invalidate_body(); return 0;
        case viewport_horizontal_offset: return horizontal_;
        case viewport_content_width: return body().right;
        case viewport_scrollbar_capture:
            if(GetCapture()!=scrollbar_.window() || !scrollbar_.window()) return FALSE;
            if(wp==VK_ESCAPE) scrollbar_.cancel();
            return TRUE;
        case viewport_enqueue_query: return style_.enqueue_default;
        case playback_message:
            invalidate_row(playing_row_); playing_row_=static_cast<int>(wp); paused_=lp!=0;
            invalidate_row(playing_row_); playback_clock(); return 0;
        case playing_group_message:
            if(playing_group_!=static_cast<int>(wp)) { playing_group_=static_cast<int>(wp); invalidate_body(); }
            return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS|DLGC_WANTCHARS;
        case WM_SETFONT: {
            font_=reinterpret_cast<HFONT>(wp); reset_text(); if(extra_font_) DeleteObject(extra_font_);
            LOGFONTW lf{}; GetObjectW(font_,sizeof(lf),&lf); icon_pitch_=int(std::max(1L,std::abs(lf.lfHeight))); lf.lfHeight=MulDiv(lf.lfHeight,9,10); extra_font_=CreateFontIndirectW(&lf);
            if(group_font_) DeleteObject(group_font_);
            GetObjectW(font_,sizeof(lf),&lf); lf.lfHeight=MulDiv(lf.lfHeight,11,10); lf.lfWeight=std::max<LONG>(lf.lfWeight,FW_SEMIBOLD);
            group_font_=CreateFontIndirectW(&lf);
            if(lp) invalidate_body(); return 0;
        }
        case WM_GETFONT: return reinterpret_cast<LRESULT>(font_);
        case WM_SETREDRAW: redraw_=wp!=0; if (redraw_) { layout(); invalidate_body(); } return 0;
        case WM_SETTINGCHANGE: layout(); invalidate_body(); return 0;
        case WM_SIZE: scrollbar_.cancel(); cancel_edit(); hide_tooltip(); layout(); invalidate_body(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(); return 0;
        case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(wp)); return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS: scrollbar_.cancel(); cancel_edit(); hide_tooltip(); invalidate_row(focus_); return 0;
        case WM_SHOWWINDOW:
            if (!wp) { scrollbar_.cancel(); cancel_edit(); hide_tooltip(); scroll_.cancel_inertia(); stop_timer(); KillTimer(window_,playback_timer); play_timer_running_=false; }
            else animate();
            break;
        case WM_TIMER:
            if(wp==drag_timer) {
                if(!mouse_down_ || drag_sent_ || suspended_ || !(GetKeyState(VK_LBUTTON)&0x8000)) { KillTimer(window_,drag_timer); return 0; }
                POINT point{}; GetCursorPos(&point); ScreenToClient(window_,&point);
                SendMessageW(window_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y)); return 0;
            }
            if(wp==playback_timer) { playback_clock(); if(play_timer_running_) invalidate_row(playing_row_); return 0; }
            if (wp==frame_timer) {
                const auto now=GetTickCount64(); const double elapsed=double(now-last_frame_); last_frame_=now;
                if (scroll_.tick(elapsed)) invalidate_body();
                sync_scrollbar();
                if (!scroll_.moving()) stop_timer();
                return 0;
            }
            if (wp==dirty_timer) {
                KillTimer(window_,dirty_timer);
                if (dirty_pending_ && redraw_) InvalidateRect(window_,&dirty_,FALSE);
                dirty_pending_=false; return 0;
            }
            break;
        case WM_VSCROLL: cancel_edit(); if (!suspended_) vertical(wp); return 0;
        case WM_HSCROLL: cancel_edit(); horizontal(wp); return 0;
        case WM_MOUSEWHEEL: {
            cancel_edit();
            if (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL) return SendMessageW(GetParent(window_),msg,wp,lp);
            if (suspended_) return 0;
            scroll_.cancel_inertia();
            UINT lines=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
            // Keep sub-detent precision rather than dropping high-resolution input.
            const double distance=lines==WHEEL_PAGESCROLL ? page() : double(lines)*style_.row_height;
            const double delta=-GET_WHEEL_DELTA_WPARAM(wp)*distance/WHEEL_DELTA;
            wheel_remainder_+=static_cast<int>(std::round(delta*1000));
            scroll_.by(wheel_remainder_/1000); wheel_remainder_%=1000;
            animate(); return 0;
        }
        case WM_MOUSEHWHEEL:
            cancel_edit();
            hide_tooltip();
            horizontal_remainder_+=GET_WHEEL_DELTA_WPARAM(wp)*style_.row_height;
            horizontal_+=horizontal_remainder_/WHEEL_DELTA; horizontal_remainder_%=WHEEL_DELTA;
            layout(); invalidate_body(); return 0;
        case WM_KEYDOWN: if(edit_row_>=0) { if(wp==VK_ESCAPE) cancel_edit(); return 0; } if (!suspended_ && key(wp)) return 0; break;
        case WM_LBUTTONDOWN: {
            hide_tooltip();
            if (suspended_) return 0;
            SetFocus(window_); scroll_.interrupt(style_.row_height); animate();
            mouse_start_={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const int visual=scroll_.hit(mouse_start_.y-style_.header_height,style_.row_height,page(),visual_count());
            if(visual>=0 && group_layout_.slots[visual].line>=0) {
                viewport_group_request request; request.group=group_layout_.slots[visual].group;
                notify(&request.hdr,viewport_group_toggle); return 0;
            }
            if(begin_edit(mouse_start_)) return 0;
            const int row=hit(mouse_start_);
            mouse_down_time_=GetTickCount64();
            mouse_down_=row>=0; drag_sent_=false; defer_single_=false;
            if (row>=0) {
                const bool control=(wp&MK_CONTROL)!=0, shift=(wp&MK_SHIFT)!=0;
                defer_single_=selected_[row] && !control && !shift;
                if (!defer_single_) select(row,control,shift);
                else { focus(row); selection_changed(); }
                SetCapture(window_); SetTimer(window_,drag_timer,30,nullptr);
            } else if (!(wp&(MK_CONTROL|MK_SHIFT))) {
                std::fill(selected_.begin(),selected_.end(),static_cast<unsigned char>(0)); invalidate_body(); selection_changed();
            }
            return 0;
        }
        case WM_MOUSELEAVE: hide_tooltip(); hover_row_=-1; return 0;
        case WM_MOUSEHOVER: show_tooltip(); return 0;
        case WM_MOUSEMOVE:
            if(edit_row_>=0) { move_edit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}); return 0; }
            if(!mouse_down_ && !touching_) track_hover({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (mouse_down_ && !drag_sent_ && !suspended_ && GetTickCount64()-mouse_down_time_>=150 &&
                (std::abs(GET_X_LPARAM(lp)-mouse_start_.x)>=GetSystemMetrics(SM_CXDRAG) ||
                 std::abs(GET_Y_LPARAM(lp)-mouse_start_.y)>=GetSystemMetrics(SM_CYDRAG))) {
                drag_sent_=true; KillTimer(window_,drag_timer); hide_tooltip(); NMLISTVIEW drag{}; drag.iItem=focus_; drag.ptAction=mouse_start_; notify(&drag.hdr,LVN_BEGINDRAG);
            }
            return 0;
        case WM_LBUTTONUP:
            if(edit_row_>=0) { finish_edit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}); return 0; }
            KillTimer(window_,drag_timer);
            if (mouse_down_ && !drag_sent_ && defer_single_ && !suspended_) select(hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}),false,false);
            mouse_down_=false; defer_single_=false; if (GetCapture()==window_) ReleaseCapture(); return 0;
        case WM_CAPTURECHANGED: cancel_edit(); KillTimer(window_,drag_timer); mouse_down_=drag_sent_=defer_single_=false; return 0;
        case WM_CANCELMODE:
            cancel_edit();
            KillTimer(window_,drag_timer);
            touching_=mouse_down_=drag_sent_=false; scroll_.cancel_inertia(); if (GetCapture()==window_) ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: {
            hide_tooltip();
            if (suspended_) return 0;
            SetFocus(window_); scroll_.interrupt(style_.row_height); animate();
            const int row=hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (row>=0 && !selected_[row]) select(row,false,false);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (suspended_) return 0;
            if(begin_edit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})) { cancel_edit(); return 0; }
            const int row=hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (row>=0) { focus(row); NMITEMACTIVATE item{}; item.iItem=row; notify(&item.hdr,NM_DBLCLK); }
            return 0;
        }
        case WM_GESTURE: {
            cancel_edit();
            GESTUREINFO info{sizeof(info)};
            if (!GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lp),&info)) break;
            if (info.dwID!=GID_PAN) break; // DefWindowProc closes unhandled gesture handles.
            if (!suspended_) {
                POINT point{info.ptsLocation.x,info.ptsLocation.y};
                if (info.dwFlags&GF_BEGIN) {
                    touching_=true; touch_previous_=point; touch_start_=GetTickCount64(); scroll_.cancel_inertia();
                    mouse_down_=drag_sent_=false;
                    // Cancel any promoted mouse drag before touch panning takes over.
                    if (GetCapture()==window_) ReleaseCapture();
                } else if (touching_) { scroll_.by(touch_previous_.y-point.y); touch_previous_=point; }
                if (info.dwFlags&GF_END) { touching_=false; scroll_.fling(double(GetTickCount64()-touch_start_)); }
                animate();
            }
            CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lp)); return 0;
        }
        case WM_NOTIFY: {
            const auto* hdr=reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom==header_) {
                if (hdr->code==HDN_ITEMCLICKW || hdr->code==HDN_ITEMCLICKA) {
                    if (!suspended_) { NMLISTVIEW click{}; click.iSubItem=reinterpret_cast<NMHEADERW*>(lp)->iItem; notify(&click.hdr,LVN_COLUMNCLICK); }
                    return 0;
                }
                if (hdr->code==HDN_ITEMCHANGEDW || hdr->code==HDN_ITEMCHANGEDA || hdr->code==HDN_ENDDRAG) {
                    cancel_edit(); layout(); invalidate_body();
                }
            }
            break;
        }
        // Compatibility surface for the existing panel/controller. Keeping this
        // small and explicit prevents two competing sources of scroll geometry.
        case viewport_scrollbar_window: return reinterpret_cast<LRESULT>(scrollbar_.window());
        case LVM_GETHEADER: return reinterpret_cast<LRESULT>(header_);
        case LVM_SETEXTENDEDLISTVIEWSTYLE: return 0;
        case LVM_GETITEMCOUNT: return count();
        case LVM_GETITEMTEXTW: {
            const int row=static_cast<int>(wp); auto* item=reinterpret_cast<LVITEMW*>(lp);
            if (row<0 || row>=count() || item->iSubItem<0 || item->iSubItem>=Header_GetItemCount(header_) || item->cchTextMax<=0) return 0;
            // Accessibility can request offscreen names without growing the viewport cache.
            NMLVDISPINFOW request{}; request.item.mask=LVIF_TEXT; request.item.iItem=row; request.item.iSubItem=item->iSubItem;
            notify(&request.hdr,LVN_GETDISPINFOW);
            const auto plain=parse_colors(request.item.pszText?request.item.pszText:L"").text;
            lstrcpynW(item->pszText,plain.c_str(),item->cchTextMax);
            return lstrlenW(item->pszText);
        }
        case LVM_SETITEMCOUNT:
            cancel_edit();
            selected_.resize(std::min<size_t>(wp,INT_MAX));
            groups_.clear(); group_layout_.build(selected_.size(),{});
            if (focus_>=count()) focus_=-1;
            if (anchor_>=count()) anchor_=-1;
            cache_.clear(); scroll_.cancel_inertia(); if(redraw_) layout(); invalidate_body(); return TRUE;
        case LVM_GETITEMSTATE: {
            const int row=static_cast<int>(wp);
            if (row<0 || row>=count()) return 0;
            return ((selected_[row]?LVIS_SELECTED:0)|(focus_==row?LVIS_FOCUSED:0))&lp;
        }
        case LVM_SETITEMSTATE: {
            const auto* item=reinterpret_cast<const LVITEMW*>(lp); const int row=static_cast<int>(wp);
            if (row < -1 || row>=count()) return FALSE;
            bool changed=false;
            const int first=row<0?0:row, end=row<0?count():row+1;
            if (item->stateMask&LVIS_SELECTED) for (int i=first;i<end;++i) {
                const bool value=(item->state&LVIS_SELECTED)!=0;
                if ((selected_[i]!=0)!=value) { selected_[i]=value; if (row>=0) invalidate_row(i); changed=true; }
            }
            if (changed && row<0) invalidate_body();
            if (item->stateMask&LVIS_FOCUSED) {
                const int old=focus_;
                if (item->state&LVIS_FOCUSED) focus(row<0?(count()?0:-1):row);
                else if (row<0 || focus_==row) focus(-1);
                changed|=old!=focus_;
            }
            if (changed) selection_changed(); return TRUE;
        }
        case LVM_GETNEXTITEM: {
            const int start=static_cast<int>(wp)+1;
            if (lp&LVNI_FOCUSED) return focus_>=start?focus_:-1;
            for (int i=std::max(0,start);i<count();++i) if (!(lp&LVNI_SELECTED) || selected_[i]) return i;
            return -1;
        }
        case LVM_GETTOPINDEX: return static_cast<LRESULT>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),size_t(std::max(0.0,std::floor(scroll_.displayed/style_.row_height))))-group_layout_.track_slots.begin());
        case LVM_GETCOUNTPERPAGE: return page()/style_.row_height;
        case LVM_GETITEMRECT: {
            const int row=static_cast<int>(wp); if (row<0 || row>=count()) return FALSE;
            *reinterpret_cast<RECT*>(lp)=row_rect(row); return TRUE;
        }
        case LVM_HITTEST: {
            auto* info=reinterpret_cast<LVHITTESTINFO*>(lp);
            info->iItem=hit(info->pt); info->flags=info->iItem>=0?LVHT_ONITEMLABEL:LVHT_NOWHERE; return info->iItem;
        }
        case LVM_ENSUREVISIBLE: reveal(static_cast<int>(wp)); return TRUE;
        case LVM_SCROLL:
            cancel_edit();
            horizontal_+=static_cast<int>(wp); scroll_.by(static_cast<int>(lp)); layout(); animate(); invalidate_body(); return TRUE;
        case LVM_GETCOLUMNORDERARRAY:
            return Header_GetOrderArray(header_,static_cast<int>(wp),reinterpret_cast<int*>(lp));
        case LVM_SETCOLUMNORDERARRAY: {
            cancel_edit();
            const auto result=Header_SetOrderArray(header_,static_cast<int>(wp),reinterpret_cast<int*>(lp)); invalidate_body(); return result;
        }
        case LVM_INSERTCOLUMNW: {
            cancel_edit();
            const auto* col=reinterpret_cast<LVCOLUMNW*>(lp);
            HDITEMW item{}; item.mask=HDI_TEXT|HDI_WIDTH|HDI_FORMAT;
            item.pszText=col->pszText; item.cxy=col->cx; item.fmt=HDF_STRING|(col->fmt&LVCFMT_JUSTIFYMASK);
            const auto result=Header_InsertItem(header_,static_cast<int>(wp),&item);
            cache_.clear(); layout(); invalidate_body(); return result;
        }
        case LVM_DELETECOLUMN: {
            cancel_edit();
            const auto result=Header_DeleteItem(header_,static_cast<int>(wp));
            cache_.clear(); layout(); invalidate_body(); return result;
        }
        case LVM_GETCOLUMNWIDTH: {
            HDITEMW item{}; item.mask=HDI_WIDTH; Header_GetItem(header_,static_cast<int>(wp),&item); return item.cxy;
        }
        case LVM_SETCOLUMNWIDTH: {
            cancel_edit();
            HDITEMW item{}; item.mask=HDI_WIDTH; item.cxy=std::max(0,static_cast<int>(lp));
            const auto result=Header_SetItem(header_,static_cast<int>(wp),&item); layout(); invalidate_body(); return result;
        }
        case search_message:
            search_=*reinterpret_cast<const viewport_search*>(lp); hide_tooltip(); invalidate_body(); return 0;
        case groups_message: {
            cancel_edit();
            hide_tooltip(); groups_=*reinterpret_cast<const std::vector<viewport_group>*>(lp);
            std::vector<group_band> bands; for(const auto& g:groups_) bands.push_back(g.band);
            group_layout_.build(selected_.size(),bands); cache_.clear(); if(redraw_) layout(); invalidate_body(); return 0;
        }
        case style_message: {
            scrollbar_.cancel();
            cancel_edit();
            const auto next=*reinterpret_cast<const viewport_style*>(lp);
            const double factor=double(next.row_height)/style_.row_height;
            scroll_.target*=factor; scroll_.displayed*=factor;
            default_cover_pixels_.reset();
            style_=next; scrollbar_.colors(style_.row,style_.text,style_.selection); reset_text(); layout(); invalidate_body(); return 0;
        }
        case invalidate_row_message: queue_dirty(static_cast<int>(wp)); return 0;
        case invalidate_rows_message: {
            const auto& affected=*reinterpret_cast<const std::function<bool(int)>*>(lp);
            for (auto it=cache_.begin();it!=cache_.end();) {
                const int row=it->first; ++it;
                if (affected(row)) queue_dirty(row);
            }
            return 0;
        }
        case reset_scroll_message: scrollbar_.cancel(); hide_tooltip(); scroll_.reset(); stop_timer(); sync_scrollbar(); invalidate_body(); return 0;
        case suspend_input_message:
            cancel_edit();
            suspended_=wp!=0; scrollbar_.enable(!suspended_);
            if (suspended_) {
                hide_tooltip(); hover_row_=-1; drop_slot_=-1; KillTimer(window_,drag_timer);
                scroll_.cancel_inertia(); stop_timer(); touching_=mouse_down_=drag_sent_=false;
                if (GetCapture()==window_) ReleaseCapture();
                cache_.clear();
            } else animate();
            return 0;
        }
        return DefWindowProcW(window_,msg,wp,lp);
    }
public:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self=reinterpret_cast<viewport*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (msg==WM_NCCREATE) {
            self=new(std::nothrow) viewport;
            if (!self) return FALSE;
            self->window_=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window,msg,wp,lp);
        if (msg==WM_NCDESTROY) {
            SetWindowLongPtrW(window,GWLP_USERDATA,0); delete self; return DefWindowProcW(window,msg,wp,lp);
        }
        try { return self->message(msg,wp,lp); }
        catch (...) {
            // Never propagate C++ exceptions through a Win32 callback.
            self->laying_out_=false;
            if (msg==WM_CREATE) return -1;
            return 0;
        }
    }
};
}
HWND create_playlist_viewport(HWND parent, HINSTANCE instance) {
    WNDCLASSW wc{}; wc.lpfnWndProc=viewport::proc; wc.hInstance=instance;
    wc.lpszClassName=L"foo_modernplaylist.viewport"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.style=CS_DBLCLKS;
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"Playlist",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN,
        0,0,0,0,parent,nullptr,instance,nullptr);
}
void set_playlist_search(HWND w,const viewport_search& search) { SendMessageW(w,search_message,0,reinterpret_cast<LPARAM>(&search)); }
void set_playlist_groups(HWND w,const std::vector<viewport_group>& groups) { SendMessageW(w,groups_message,0,reinterpret_cast<LPARAM>(&groups)); }
void set_playlist_playback(HWND w,int row,bool paused) { SendMessageW(w,playback_message,static_cast<WPARAM>(row),paused); }
void set_playlist_playing_group(HWND w,int group) { SendMessageW(w,playing_group_message,static_cast<WPARAM>(group),0); }
void configure_playlist_viewport(HWND w,const viewport_style& style) { SendMessageW(w,style_message,0,reinterpret_cast<LPARAM>(&style)); }
void invalidate_playlist_row(HWND w,int row) { SendMessageW(w,invalidate_row_message,static_cast<WPARAM>(row),0); }
void invalidate_playlist_rows(HWND w,const std::function<bool(int)>& affected) { SendMessageW(w,invalidate_rows_message,0,reinterpret_cast<LPARAM>(&affected)); }
void reset_playlist_scroll(HWND w) { SendMessageW(w,reset_scroll_message,0,0); }
void suspend_playlist_input(HWND w,bool suspended) { SendMessageW(w,suspend_input_message,suspended,0); }
}
