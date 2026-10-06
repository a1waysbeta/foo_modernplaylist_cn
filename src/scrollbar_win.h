#pragma once
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
    std::function<void(double)> changed_;
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
        auto fill=[&](RECT r,scrollbar_part part,bool thumb) {
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
        for(int direction:{-1,1}) {
            const RECT r=direction<0?up:down;
            const int radius=std::max(0,std::min(model_.width/4,model_.arrow/4));
            if(radius>0) draw_smooth_chevron(dc,model_.width/2,(r.top+r.bottom)/2,radius,
                std::max(2,model_.width/8),direction>0,blend(background_,foreground_,enabled_?220:60));
        }
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
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            if(!enabled_) return 0;
            SetFocus(GetParent(window_)); pointer({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            model_.begin(pointer_);
            if(model_.pressed==scrollbar_part::none) return 0;
            SetCapture(window_); invalidate();
            if(model_.pressed!=scrollbar_part::thumb) {
                change(model_.step(pointer_,line_)); repeat_=false; SetTimer(window_,1,400,nullptr);
            }
            return 0;
        case WM_MOUSEMOVE: {
            pointer({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window_,0}; TrackMouseEvent(&track);
            if(model_.pressed==scrollbar_part::thumb) change(model_.drag(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_MOUSELEAVE: hover_=scrollbar_part::none; pointer_=-1; invalidate(); return 0;
        case WM_TIMER:
            if(wp==1 && model_.pressed!=scrollbar_part::none) {
                if(!repeat_) { repeat_=true; SetTimer(window_,1,60,nullptr); }
                POINT p{}; GetCursorPos(&p); ScreenToClient(window_,&p); pointer(p);
                change(model_.step(pointer_,line_));
            }
            return 0;
        case WM_LBUTTONUP: case WM_CAPTURECHANGED: case WM_CANCELMODE: cancel(); return 0;
        case WM_SHOWWINDOW: if(!wp) { cancel(); hover_=scrollbar_part::none; pointer_=-1; } break;
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
    bool create(HWND parent,std::function<void(double)> changed,std::function<bool(HDC,const RECT&)> paint_background={}) {
        changed_=std::move(changed); paint_background_=std::move(paint_background);
        WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"foo_modernplaylist.scrollbar"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.style=CS_DBLCLKS;
        RegisterClassW(&wc);
        return CreateWindowExW(0,wc.lpszClassName,L"Vertical scrollbar",WS_CHILD|WS_CLIPSIBLINGS,0,0,0,0,parent,nullptr,wc.hInstance,this)!=nullptr;
    }
    void cancel() {
        model_.cancel(); repeat_=false;
        if(window_) { KillTimer(window_,1); if(GetCapture()==window_) ReleaseCapture(); invalidate(); }
    }
    void destroy() { changed_={}; paint_background_={}; if(window_) { cancel(); DestroyWindow(window_); } }
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
        SetWindowPos(window_,HWND_TOP,x,inset,width,length,SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        invalidate();
    }
};
}
