#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <utility>
#include "scrollbar_model.h"
#include "artwork_win.h"

namespace modern_playlist {
// Reusable child control. The owner supplies colors, extents and a target setter;
// it retains ownership of scrolling/animation. No SDK or playlist dependency.
class scrollbar_control {
    HWND window_=nullptr;
    scrollbar_model model_;
    scrollbar_part hover_=scrollbar_part::none;
    COLORREF background_=0,foreground_=0,accent_=0;
    int inset_=0, line_=1, pointer_=-1;
    bool enabled_=true, repeat_=false;
    // Thin scrollbar: by default the track paints at ~37% of the full window
    // width (right-aligned); hovering or pressing expands it to full width.
    // Hitting still uses the full window width, so any part of the edge can
    // grab the thumb — a Fluent-style thin track.
    static constexpr UINT ANIM_TIMER=2;
    int thin_width_=0, visible_width_=0;
    bool hovering_=false, animating_=false;
    int anim_target_=0;
    std::function<void(double)> changed_;
    std::function<void()> show_playing_;
    std::function<bool(HDC,const RECT&)> paint_background_;
    static COLORREF blend(COLORREF a,COLORREF b,int alpha) {
        return RGB((GetRValue(a)*(255-alpha)+GetRValue(b)*alpha)/255,
            (GetGValue(a)*(255-alpha)+GetGValue(b)*alpha)/255,
            (GetBValue(a)*(255-alpha)+GetBValue(b)*alpha)/255);
    }
    void invalidate() { if(window_) InvalidateRect(window_,nullptr,FALSE); }
    void change(double target) { if(enabled_ && changed_) changed_(target); }
    void paint(HDC dc) {
        RECT bounds{}; GetClientRect(window_,&bounds);
        const bool artwork=paint_background_ && paint_background_(dc,bounds);
        if(!artwork) {
            SetDCBrushColor(dc,background_); FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        // The track paints right-aligned: x0 = full - visible, so the thin
        // track hugs the edge and expands left on hover.
        const int vw=std::max(1,visible_width_), x0=std::max(0,model_.width-vw);
        auto fill=[&](RECT r,scrollbar_part part,bool thumb) {
            r.left+=x0; r.right=x0+std::max(1,r.right-r.left);
            const bool down=model_.pressed==part && (part==scrollbar_part::thumb || hover_==part);
            const int alpha=!enabled_?35:down?170:hover_==part?110:thumb?65:0;
            if(artwork) {
                if(alpha) tint_artwork_gdi(dc,r,down?accent_:foreground_,unsigned(alpha));
            } else {
                SetDCBrushColor(dc,blend(background_,down?accent_:foreground_,alpha));
                FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        };
        RECT up{0,0,model_.width,model_.arrow};
        RECT down{0,model_.length-model_.arrow,model_.width,model_.length};
        fill(up,scrollbar_part::up,false); fill(down,scrollbar_part::down,false);
        RECT thumb{1,model_.thumb_top(),std::max(1,model_.width-1),model_.thumb_top()+model_.thumb_length()};
        fill(thumb,scrollbar_part::thumb,true);
        // Arrows take 65% of the foreground (166/255), the panel's other
        // icons' ink; disabled ones 60/255.
        for(int direction:{-1,1}) {
            const RECT r=direction<0?up:down;
            const int radius=std::max(0,std::min(vw/4,model_.arrow/4));
            if(radius>0) draw_smooth_chevron(dc,x0+vw/2,(r.top+r.bottom)/2,radius,
                modern_playlist::glyph_weight(vw),direction>0,blend(background_,foreground_,enabled_?166:60));
        }
    }
    // Start an animation toward a target width. Linear 80ms ease-in-out is
    // fine; no need for a real easing curve at these sizes.
    void animate_to(int target) {
        if(target==visible_width_) return;
        anim_target_=target; animating_=true; SetTimer(window_,ANIM_TIMER,16,nullptr);
    }
    void step_anim() {
        if(!animating_) return;
        const int delta=anim_target_-visible_width_;
        if(std::abs(delta)<=1) { visible_width_=anim_target_; animating_=false; KillTimer(window_,ANIM_TIMER); invalidate(); return; }
        // 33% per frame → full transition in ~80ms (~5 frames).
        visible_width_+=delta/3;
        invalidate();
    }
    void update_hovering() {
        const auto target=hovering_?model_.width:thin_width_;
        if(target!=visible_width_ && !animating_) animate_to(target);
    }
    void pointer(POINT point) {
        pointer_=point.x>=0 && point.x<model_.width?point.y:-1;
        const auto next=enabled_?model_.hit(pointer_):scrollbar_part::none;
        if(next!=hover_) { hover_=next; invalidate(); }
    }
    LRESULT message(UINT msg,WPARAM wp,LPARAM lp) {
        switch(msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; auto dc=BeginPaint(window_,&ps); paint(dc); EndPaint(window_,&ps); return 0;
        }
        case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(wp)); return 0;
        case WM_LBUTTONDBLCLK: {
            if(!enabled_) return 0;
            if(wp&MK_SHIFT) return message(WM_LBUTTONDOWN,wp,lp); // A quick second Shift+click jumps again.
            pointer({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            const auto part=model_.hit(pointer_); cancel();
            if(part==scrollbar_part::thumb) { if(show_playing_) show_playing_(); }
            else if(part==scrollbar_part::up) change(0);
            else if(part==scrollbar_part::down) change(model_.maximum());
            return 0;
        }
        case WM_LBUTTONDOWN:
            if(!enabled_) return 0;
            SetFocus(GetParent(window_)); pointer({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            model_.begin(pointer_);
            if(model_.pressed==scrollbar_part::none) return 0;
            SetCapture(window_); invalidate();
            // Shift+click moves the thumb straight to the pointer, then drags it.
            if((wp&MK_SHIFT) && model_.jump()) change(model_.drag(pointer_));
            if(model_.pressed!=scrollbar_part::thumb) {
                change(model_.step(pointer_,line_)); repeat_=false; SetTimer(window_,1,400,nullptr);
            }
            return 0;
        case WM_MOUSEMOVE: {
            pointer({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window_,0}; TrackMouseEvent(&track);
            if(!hovering_) { hovering_=true; update_hovering(); }
            if(model_.pressed==scrollbar_part::thumb) change(model_.drag(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_=scrollbar_part::none; pointer_=-1; hovering_=false; update_hovering(); invalidate(); return 0;
        case WM_TIMER:
            if(wp==ANIM_TIMER) { step_anim(); return 0; }
            if(wp==1 && model_.pressed!=scrollbar_part::none) {
                if(!repeat_) { repeat_=true; SetTimer(window_,1,60,nullptr); }
                POINT p{}; GetCursorPos(&p); ScreenToClient(window_,&p); pointer(p);
                change(model_.step(pointer_,line_));
            }
            return 0;
        case WM_LBUTTONUP: case WM_CAPTURECHANGED: case WM_CANCELMODE: cancel(); return 0;
        case WM_SHOWWINDOW: if(!wp) { cancel(); hover_=scrollbar_part::none; pointer_=-1; hovering_=false; } break;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_MBUTTONUP:
            return SendMessageW(GetParent(window_),msg,wp,lp);
        case WM_CONTEXTMENU: return 0;
        case WM_NCDESTROY: {
            const auto w=window_; cancel(); window_=nullptr; SetWindowLongPtrW(w,GWLP_USERDATA,0);
            return DefWindowProcW(w,msg,wp,lp);
        }
        }
        return DefWindowProcW(window_,msg,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<scrollbar_control*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<scrollbar_control*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_=w; SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(w,msg,wp,lp);
        try { return self->message(msg,wp,lp); } catch(...) { self->cancel(); return 0; }
    }
public:
    scrollbar_control()=default;
    scrollbar_control(const scrollbar_control&)=delete;
    scrollbar_control& operator=(const scrollbar_control&)=delete;
    ~scrollbar_control() { destroy(); }
    HWND window() const { return window_; }
    static int metric(int index,unsigned dpi) {
        using metric_fn=int(WINAPI*)(int,UINT);
        const auto fn=reinterpret_cast<metric_fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetSystemMetricsForDpi"));
        if(fn) return std::max(1,fn(index,dpi));
        HDC dc=GetDC(nullptr); const int system_dpi=dc?GetDeviceCaps(dc,LOGPIXELSX):96;
        if(dc) ReleaseDC(nullptr,dc);
        return std::max(1,MulDiv(GetSystemMetrics(index),dpi,std::max(1,system_dpi)));
    }
    bool create(HWND parent,std::function<void(double)> changed,std::function<bool(HDC,const RECT&)> paint_background={},std::function<void()> show_playing={}) {
        changed_=std::move(changed); paint_background_=std::move(paint_background); show_playing_=std::move(show_playing);
        WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"foo_modernplaylist.scrollbar"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.style=CS_DBLCLKS;
        RegisterClassW(&wc);
        return CreateWindowExW(0,wc.lpszClassName,L"Vertical scrollbar",WS_CHILD|WS_CLIPSIBLINGS,0,0,0,0,parent,nullptr,wc.hInstance,this)!=nullptr;
    }
    void cancel() {
        model_.cancel(); repeat_=false; animating_=false;
        if(window_) { KillTimer(window_,1); KillTimer(window_,ANIM_TIMER); if(GetCapture()==window_) ReleaseCapture(); invalidate(); }
    }
    void destroy() { changed_={}; paint_background_={}; show_playing_={}; if(window_) { cancel(); DestroyWindow(window_); } }
    void enable(bool enabled) { if(enabled_!=enabled) { enabled_=enabled; if(!enabled) cancel(); invalidate(); } }
    void colors(COLORREF background,COLORREF foreground,COLORREF accent) {
        background_=background; foreground_=foreground; accent_=accent; invalidate();
    }
    void position(double target) {
        const auto old=model_.thumb_top(); const auto previous=hover_;
        model_.position=std::clamp(target,0.0,model_.maximum());
        hover_=enabled_?model_.hit(pointer_):scrollbar_part::none;
        if(old!=model_.thumb_top() || previous!=hover_) invalidate();
    }
    void layout(int x,int height,int width,int inset,int arrow,double content,double page,double target,int line,bool visible) {
        if(!window_) return;
        const int length=std::max(0,height-inset);
        if(!visible || length!=model_.length || width!=model_.width || inset!=inset_ || content!=model_.content || page!=model_.page) {
            cancel(); hover_=scrollbar_part::none; pointer_=-1;
        }
        inset_=inset; line_=line; model_.geometry(length,width,arrow,content,page,target);
        thin_width_=std::max(2,width*3/8); // ~37% thin track, right-aligned.
        // Reset the visible width on geometry change (hovering keeps full width).
        if(!hovering_) visible_width_=thin_width_; else visible_width_=width;
        anim_target_=visible_width_; animating_=false;
        SetWindowPos(window_,HWND_TOP,x,inset,width,length,SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        invalidate();
    }
};
}
