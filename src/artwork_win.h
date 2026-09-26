#pragma once
#include "artwork.h"
#include "icon_shapes.h"
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>

namespace modern_playlist {
template<typename Abort>
inline std::wstring resolve_image_pattern(const std::wstring& pattern,Abort& abort) {
    if(pattern.find_first_of(L"*?")==std::wstring::npos) return pattern;
    const auto slash=pattern.find_last_of(L"/\\");
    // Wildcards apply only to the filename, never recursively to directories.
    if(slash!=std::wstring::npos && pattern.substr(0,slash).find_first_of(L"*?")!=std::wstring::npos) return {};
    WIN32_FIND_DATAW data{}; HANDLE handle=FindFirstFileW(pattern.c_str(),&data);
    if(handle==INVALID_HANDLE_VALUE) return {};
    struct find_scope { HANDLE handle; ~find_scope(){FindClose(handle);} } scope{handle};
    std::wstring chosen;
    do { abort.check(); if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && (chosen.empty() || _wcsicmp(data.cFileName,chosen.c_str())<0)) chosen=data.cFileName; } while(FindNextFileW(handle,&data));
    return chosen.empty()?chosen:(slash==std::wstring::npos?L"":pattern.substr(0,slash+1))+chosen;
}
template<typename Abort>
inline std::shared_ptr<cover_pixels> decode_artwork(IStream* stream,const std::wstring& path,unsigned maximum,Abort& abort) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())))) return {};
    HRESULT result=E_FAIL;
    if(stream) result=factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf());
    else {
        const auto file=resolve_image_pattern(path,abort); if(file.empty()) return {};
        result=factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf());
    }
    abort.check();
    if(FAILED(result) || FAILED(decoder->GetFrame(0,frame.GetAddressOf()))) return {};
    UINT width=0,height=0;
    if(FAILED(frame->GetSize(&width,&height)) || !width || !height || uint64_t(width)*height>100000000) return {};
    const double ratio=std::min(1.0,double(maximum)/std::max(width,height));
    width=std::max(1U,UINT(width*ratio)); height=std::max(1U,UINT(height*ratio));
    ComPtr<IWICBitmapScaler> scaler; ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) || FAILED(scaler->Initialize(frame.Get(),width,height,WICBitmapInterpolationModeFant)) ||
       FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) || FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=width; pixels->height=height; pixels->bgra.resize(size_t(width)*height*4);
    if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels->bgra.size()),pixels->bgra.data()))) return {};
    abort.check(); return pixels;
}
inline BITMAPINFO artwork_bitmap_info(const cover_pixels& p) {
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=p.width; info.bmiHeader.biHeight=-int(p.height);
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB; return info;
}
// Opaque composed backgrounds can be copied without a temporary DIB/DC.
inline void draw_surface_gdi(HDC dc,const cover_pixels& pixels,int x,int y) {
    auto info=artwork_bitmap_info(pixels);
    SetDIBitsToDevice(dc,x,y,pixels.width,pixels.height,0,0,0,pixels.height,pixels.bgra.data(),&info,DIB_RGB_COLORS);
}
inline void draw_artwork_gdi(HDC dc,const cover_pixels& pixels,int x,int y,int w,int h,unsigned opacity=255) {
    auto info=artwork_bitmap_info(pixels); void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0); HDC memory=CreateCompatibleDC(dc);
    if(bitmap && memory && bits) {
        memcpy(bits,pixels.bgra.data(),pixels.bgra.size()); auto old=SelectObject(memory,bitmap);
        BLENDFUNCTION blend{AC_SRC_OVER,0,BYTE(std::min(255U,opacity)),AC_SRC_ALPHA};
        AlphaBlend(dc,x,y,w,h,memory,0,0,pixels.width,pixels.height,blend); SelectObject(memory,old);
    }
    if(memory) DeleteDC(memory); if(bitmap) DeleteObject(bitmap);
}
inline void tint_artwork_gdi(HDC dc,RECT r,COLORREF color,unsigned opacity) {
    cover_pixels pixel; pixel.width=pixel.height=1;
    pixel.bgra={GetBValue(color),GetGValue(color),GetRValue(color),255};
    draw_artwork_gdi(dc,pixel,r.left,r.top,r.right-r.left,r.bottom-r.top,opacity);
}
inline void draw_smooth_chevron(HDC dc,int cx,int cy,int radius,int thickness,bool down,COLORREF color) {
    const auto pixels=chevron_icon(radius,thickness,down,color);
    draw_artwork_gdi(dc,*pixels,cx-int(pixels->width)/2,cy-int(pixels->height)/2,pixels->width,pixels->height);
}
// Use the requested legacy symbol fonts through GDI's symbol character mapping.
// Rasterizing once supplies the same premultiplied glyph to GDI and Direct2D;
// queue numbers stay in the normal playlist font. kind: check, play, alternate.
inline std::shared_ptr<cover_pixels> raster_state_icon(unsigned kind,unsigned size,COLORREF color) {
    size=std::clamp(size,1U,256U); kind=std::min(kind,2U);
    const wchar_t* face=kind==0?L"Wingdings 2":L"Wingdings 3";
    const wchar_t glyphs[]={L'\u0050',L'\u0075',L'\u0077'};
    wchar_t glyph=glyphs[kind];
    cover_pixels canvas; canvas.width=canvas.height=size*4;
    auto info=artwork_bitmap_info(canvas); void* bits=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc) return {};
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap || !bits) { if(bitmap) DeleteObject(bitmap); DeleteDC(dc); return {}; }
    const auto old_bitmap=SelectObject(dc,bitmap);
    auto make_font=[&](const wchar_t* name,BYTE charset) {
        return CreateFontW(-int(canvas.height),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,charset,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,name);
    };
    const auto old_font=GetCurrentObject(dc,OBJ_FONT);
    HFONT font=make_font(face,SYMBOL_CHARSET); if(font) SelectObject(dc,font);
    wchar_t actual[LF_FACESIZE]{}; GetTextFaceW(dc,LF_FACESIZE,actual);
    if(!font || _wcsicmp(actual,face)!=0) {
        SelectObject(dc,old_font); if(font) DeleteObject(font);
        font=make_font(L"Segoe UI Symbol",DEFAULT_CHARSET); SelectObject(dc,font);
        const wchar_t fallback[]={L'\u2713',L'\u25b6',L'\u25b7'}; glyph=fallback[kind];
    }
    memset(bits,0,size_t(canvas.width)*canvas.height*4);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
    RECT bounds{0,0,LONG(canvas.width),LONG(canvas.height)};
    DrawTextW(dc,&glyph,1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    GdiFlush();
    auto pixels=std::make_shared<cover_pixels>(); pixels->width=pixels->height=size;
    pixels->bgra.resize(size_t(size)*size*4); const auto* mask=static_cast<const unsigned char*>(bits);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        unsigned alpha=0;
        for(unsigned dy=0;dy<4;++dy) for(unsigned dx=0;dx<4;++dx)
            alpha+=mask[(size_t(y*4+dy)*canvas.width+x*4+dx)*4];
        alpha/=16; const size_t i=(size_t(y)*size+x)*4;
        pixels->bgra[i]=static_cast<unsigned char>(GetBValue(color)*alpha/255);
        pixels->bgra[i+1]=static_cast<unsigned char>(GetGValue(color)*alpha/255);
        pixels->bgra[i+2]=static_cast<unsigned char>(GetRValue(color)*alpha/255);
        pixels->bgra[i+3]=static_cast<unsigned char>(alpha);
    }
    SelectObject(dc,old_font); if(font) DeleteObject(font);
    SelectObject(dc,old_bitmap); DeleteObject(bitmap); DeleteDC(dc);
    return pixels;
}

}
