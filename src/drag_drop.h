#pragma once
#include <windows.h>
#include <ole2.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <vector>

namespace modern_playlist {
// OLE owns references during its nested message loop. Disconnect callbacks before
// revoking windows so neither late DragLeave nor a cancelled source touches a view.
class track_drop_target final : public IDropTarget {
    LONG refs_=1;
    Microsoft::WRL::ComPtr<IDataObject> data_;
    DWORD allowed_=0, keys_=0;
    POINTL point_{};
public:
    using handler=std::function<DWORD(IDataObject*,DWORD,POINTL,DWORD,bool)>;
    handler receive;
    std::function<void()> leave;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(id!=IID_IUnknown && id!=IID_IDropTarget) return E_NOINTERFACE;
        *out=static_cast<IDropTarget*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=InterlockedDecrement(&refs_); if(!n) delete this; return n; }
    DWORD update(bool drop=false) noexcept {
        try {
            // A callback can destroy the panel and disconnect this object.
            auto callback=receive;
            auto data=data_;
            if(data && callback) return callback(data.Get(),keys_,point_,allowed_,drop);
        } catch(...) {}
        return DROPEFFECT_NONE;
    }
    void tick() { if(data_) { POINT p{}; GetCursorPos(&p); point_={p.x,p.y}; update(); } }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data,DWORD keys,POINTL point,DWORD* effect) override {
        if(!effect) return E_POINTER;
        data_=data; allowed_=*effect; keys_=keys; point_=point; *effect=update(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys,POINTL point,DWORD* effect) override {
        if(!effect) return E_POINTER;
        keys_=keys; point_=point; *effect=update(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        data_.Reset(); try { auto callback=leave; if(callback) callback(); } catch(...) {} return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD keys,POINTL point,DWORD* effect) override {
        if(!effect) return E_POINTER;
        data_=data; keys_=keys; point_=point; *effect=update(true); DragLeave(); return S_OK;
    }
};
class drop_registrations {
    struct entry { HWND window; Microsoft::WRL::ComPtr<track_drop_target> target; };
    std::vector<entry> entries_;
public:
    ~drop_registrations() { clear(); }
    bool add(HWND window,track_drop_target::handler receive,std::function<void()> leave) {
        Microsoft::WRL::ComPtr<track_drop_target> target; target.Attach(new track_drop_target);
        target->receive=std::move(receive); target->leave=std::move(leave);
        entries_.push_back({window,target});
        if(FAILED(RegisterDragDrop(window,target.Get()))) { entries_.pop_back(); return false; }
        return true;
    }
    void clear() noexcept {
        for(auto& e:entries_) { e.target->receive={}; e.target->leave={}; e.target->DragLeave(); RevokeDragDrop(e.window); }
        entries_.clear();
    }
    void tick() { for(auto& e:entries_) e.target->tick(); }
};
class track_drop_source final : public IDropSource {
    LONG refs_=1;
    std::shared_ptr<bool> alive_;
public:
    explicit track_drop_source(std::shared_ptr<bool> alive):alive_(std::move(alive)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(id!=IID_IUnknown && id!=IID_IDropSource) return E_NOINTERFACE;
        *out=static_cast<IDropSource*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=InterlockedDecrement(&refs_); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        if(escape || !*alive_ || (keys&MK_RBUTTON)) return DRAGDROP_S_CANCEL;
        return keys&MK_LBUTTON?S_OK:DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override {
        if(effect==DROPEFFECT_NONE) { SetCursor(LoadCursor(nullptr,IDC_NO)); return S_OK; }
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};
}
