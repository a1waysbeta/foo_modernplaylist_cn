#pragma once
#include "artwork_win.h"
#include <d2d1.h>
#include <dwrite_2.h>
#include <tuple>
#include <vector>

namespace modern_playlist {
// Lists the text ranges DirectWrite draws with a color font, without drawing.
class color_run_finder final : public IDWriteTextRenderer {
public:
    std::vector<DWRITE_TEXT_RANGE> ranges;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(id==__uuidof(IUnknown) || id==__uuidof(IDWritePixelSnapping) || id==__uuidof(IDWriteTextRenderer)) {
            *out=static_cast<IDWriteTextRenderer*>(this); return S_OK;
        }
        *out=nullptr; return E_NOINTERFACE;
    }
    // Lives on the stack for one Draw call.
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled) override { *disabled=FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* transform) override { *transform={1,0,0,1,0,0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* pixels) override { *pixels=1; return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT,FLOAT,DWRITE_MEASURING_MODE,const DWRITE_GLYPH_RUN* run,
        const DWRITE_GLYPH_RUN_DESCRIPTION* description,IUnknown*) override {
        Microsoft::WRL::ComPtr<IDWriteFontFace2> face;
        if(run && run->fontFace && description && description->stringLength &&
            SUCCEEDED(run->fontFace->QueryInterface(IID_PPV_ARGS(face.GetAddressOf()))) && face->IsColorFont())
            ranges.push_back({description->textPosition,description->stringLength});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*) override { return S_OK; }
};
// Render text into transparent, premultiplied bitmaps so color glyphs compose
// over the existing GDI tab contours and artwork without an opaque rectangle.
class tab_text_renderer {
    template<typename T> using ptr=Microsoft::WRL::ComPtr<T>;
    ptr<ID2D1Factory> drawing_;
    ptr<IDWriteFactory> writing_;
    ptr<IWICImagingFactory> imaging_;
    ptr<IDWriteTextFormat> format_;
    ptr<IDWriteRenderingParams> symmetric_;
    using key=std::tuple<std::wstring,int,int,COLORREF,bool,bool>;
    std::map<key,std::shared_ptr<cover_pixels>> cache_;
    ptr<IDWriteTextLayout> layout(const std::wstring& text,int width,int height,bool trim,bool center) {
        ptr<IDWriteTextLayout> result;
        if(!format_ || FAILED(writing_->CreateTextLayout(text.c_str(),UINT32(text.size()),format_.Get(),
            float(width),float(height),&result))) return {};
        result->SetTextAlignment(center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
        if(trim) {
            ptr<IDWriteInlineObject> ellipsis;
            if(FAILED(writing_->CreateEllipsisTrimmingSign(format_.Get(),&ellipsis))) return {};
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
            if(FAILED(result->SetTrimming(&trimming,ellipsis.Get()))) return {};
        }
        return result;
    }
public:
    void clear() {cache_.clear();}
    bool configure(HFONT font) {
        format_.Reset(); clear();
        if(!drawing_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,drawing_.GetAddressOf()))) return false;
        if(!writing_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writing_.GetAddressOf())))) return false;
        if(!imaging_ && FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(imaging_.GetAddressOf())))) return false;
        // Small text renders in natural mode, which smooths only horizontally:
        // round emoji kept stair-stepped tops and bottoms. They are drawn with
        // these both-axis grayscale settings; other text keeps the defaults.
        if(!symmetric_) {
            ptr<IDWriteRenderingParams> defaults;
            if(SUCCEEDED(writing_->CreateRenderingParams(&defaults)))
                writing_->CreateCustomRenderingParams(defaults->GetGamma(),defaults->GetEnhancedContrast(),0,
                    DWRITE_PIXEL_GEOMETRY_FLAT,DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,&symmetric_);
        }
        LOGFONTW lf{};
        if(!font || !GetObjectW(font,sizeof(lf),&lf)) return false;
        // The host font already contains DPI/zoom scaling. Direct2D uses 96 DPI
        // below so layout and bitmap dimensions stay in those physical pixels.
        HDC dc=GetDC(nullptr); const auto old=SelectObject(dc,font);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc,&metrics);
        SelectObject(dc,old); ReleaseDC(nullptr,dc);
        const float size=float(std::max(1L,metrics.tmHeight-metrics.tmInternalLeading));
        if(FAILED(writing_->CreateTextFormat(lf.lfFaceName,nullptr,
            DWRITE_FONT_WEIGHT(lf.lfWeight?lf.lfWeight:FW_NORMAL),
            lf.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,size,L"",&format_))) return false;
        format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return true;
    }
    int measure(const std::wstring& text) {
        auto text_layout=layout(text,1000000,4096,false,false);
        DWRITE_TEXT_METRICS metrics{};
        if(!text_layout || FAILED(text_layout->GetMetrics(&metrics))) return -1;
        return int(std::ceil(metrics.widthIncludingTrailingWhitespace));
    }
    std::shared_ptr<cover_pixels> render(const std::wstring& text,int width,int height,COLORREF ink,bool colored,bool center=false) {
        if(width<=0 || height<=0 || width>8192 || height>2048) return {};
        key id{text,width,height,ink,colored,center};
        if(const auto found=cache_.find(id);found!=cache_.end()) return found->second;
        auto text_layout=layout(text,width,height,true,center);
        if(!text_layout) return {};
        ptr<IWICBitmap> bitmap;
        if(FAILED(imaging_->CreateBitmap(UINT(width),UINT(height),GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,&bitmap))) return {};
        auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        ptr<ID2D1RenderTarget> target; ptr<ID2D1SolidColorBrush> brush;
        if(FAILED(drawing_->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target)) ||
            FAILED(target->CreateSolidColorBrush(D2D1::ColorF(GetRValue(ink)/255.f,GetGValue(ink)/255.f,GetBValue(ink)/255.f),&brush))) return {};
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        color_run_finder emoji; ptr<ID2D1SolidColorBrush> hidden;
        if(symmetric_) text_layout->Draw(nullptr,&emoji,0,0);
        if(!emoji.ranges.empty() && FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0,0.f),&hidden))) emoji.ranges.clear();
        target->BeginDraw(); target->Clear(D2D1::ColorF(0,0.f));
        auto options=D2D1_DRAW_TEXT_OPTIONS_CLIP;
        if(colored) options=options|D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
        if(emoji.ranges.empty()) target->DrawTextLayout(D2D1::Point2F(0,0),text_layout.Get(),brush.Get(),options);
        else {
            // One layout keeps both passes on the same glyph positions: the
            // text first with the default rendering, then only the emoji.
            for(const auto& range:emoji.ranges) text_layout->SetDrawingEffect(hidden.Get(),range);
            target->DrawTextLayout(D2D1::Point2F(0,0),text_layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            text_layout->SetDrawingEffect(hidden.Get(),{0,UINT32(text.size())});
            for(const auto& range:emoji.ranges) text_layout->SetDrawingEffect(brush.Get(),range);
            target->SetTextRenderingParams(symmetric_.Get());
            target->DrawTextLayout(D2D1::Point2F(0,0),text_layout.Get(),brush.Get(),options);
        }
        if(FAILED(target->EndDraw())) return {};
        auto pixels=std::make_shared<cover_pixels>();
        pixels->width=unsigned(width); pixels->height=unsigned(height);
        pixels->bgra.resize(size_t(width)*height*4);
        if(FAILED(bitmap->CopyPixels(nullptr,UINT(width)*4,UINT(pixels->bgra.size()),pixels->bgra.data()))) return {};
        if(cache_.size()>=128) clear();
        cache_.emplace(std::move(id),pixels);
        return pixels;
    }
    bool draw(HDC dc,const std::wstring& text,const RECT& rect,COLORREF ink,bool colored,bool center=false) {
        const auto pixels=render(text,rect.right-rect.left,rect.bottom-rect.top,ink,colored,center);
        if(!pixels) return false;
        draw_artwork_gdi(dc,*pixels,rect.left,rect.top,int(pixels->width),int(pixels->height));
        return true;
    }
};
}
