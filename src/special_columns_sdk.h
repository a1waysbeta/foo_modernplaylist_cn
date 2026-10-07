#pragma once
#include <SDK/foobar2000.h>
#include "special_columns.h"
#include <cwchar>
#include <functional>
#include <memory>
#include <stdexcept>

namespace modern_playlist {
class column_format_hook : public titleformat_hook {
    size_t index_, total_; bool playing_;
public:
    column_format_hook(size_t index,size_t total,bool playing):index_(index),total_(total),playing_(playing) {}
    bool process_field(titleformat_text_out* out,const char* name,t_size length,bool& found) override {
        const std::string field(name,length);
        if(_stricmp(field.c_str(),"list_index")==0 || _stricmp(field.c_str(),"list_total")==0) {
            found=true; out->write_int(titleformat_inputtypes::unknown,_stricmp(field.c_str(),"list_index")==0?index_+1:total_); return true;
        }
        if(_stricmp(field.c_str(),"isplaying")==0) {
            found=playing_; if(found) out->write(titleformat_inputtypes::unknown,"1"); return true;
        }
        return false;
    }
    bool process_function(titleformat_text_out* out,const char* name,t_size length,titleformat_hook_function_params* params,bool& found) override {
        if(_stricmp(std::string(name,length).c_str(),"rgb")!=0) return false;
        found=false;
        if(params->get_param_count()!=3) return true;
        unsigned value=0;
        for(size_t i=0;i<3;++i) {
            const char* data; t_size size; params->get_param(i,data,size);
            unsigned channel=0; if(!size) return true;
            for(size_t j=0;j<size;++j) { if(data[j]<'0' || data[j]>'9') return true; channel=std::min(255U,channel*10+unsigned(data[j]-'0')); }
            value|=channel<<(8*i);
        }
        char escape[9]={3}; const char* hex="0123456789ABCDEF";
        for(int i=0;i<6;++i) escape[i+1]=hex[(value>>(4*(5-i)))&15];
        escape[7]=3; out->write(titleformat_inputtypes::unknown,escape,8); return true;
    }
};
class column_tag_filter : public file_info_filter {
    std::string tag_, value_;
public:
    column_tag_filter(const char* tag,std::string value):tag_(tag),value_(std::move(value)) {}
    bool apply_filter(trackRef,t_filestats,file_info& info) override {
        if(value_.empty()) info.meta_remove_field(tag_.c_str()); else info.meta_set(tag_.c_str(),value_.c_str());
        return true;
    }
};
// foo_playcount keeps ratings in its own database. Its commands are identified
// by the module that implements them and by the Rating group's digit names, so
// a translated menu path still reaches them instead of the file tags.
inline bool playcount_module(const void* object) {
    HMODULE module=nullptr; wchar_t path[MAX_PATH]{};
    const auto vtable=*static_cast<const void* const*>(object);
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(vtable),&module)) return false;
    const DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
    if(!length || length>=MAX_PATH) return false;
    const wchar_t* name=wcsrchr(path,L'\\');
    return _wcsicmp(name?name+1:path,L"foo_playcount.dll")==0;
}
enum class playcount_rating_result { absent, rated, missing };
inline playcount_rating_result playcount_rating(metadb_handle_list_cref tracks,int value) {
    const GUID caller=contextmenu_item::caller_undefined;
    std::vector<std::unique_ptr<contextmenu_item_node_root>> roots;
    std::vector<std::pair<contextmenu_item_node*,unsigned>> nodes;
    std::vector<menu_command_name> names;
    std::function<void(contextmenu_item_node*,const std::string&)> walk=[&](contextmenu_item_node* node,const std::string& group) {
        pfc::string8 name; unsigned flags=0;
        if(!node || !node->get_display_data(name,flags,tracks,caller)) return; // Hidden for this track.
        if(node->get_type()==contextmenu_item_node::type_command) { nodes.push_back({node,flags}); names.push_back({group,name.c_str()}); }
        else if(node->get_type()==contextmenu_item_node::type_group) {
            const auto key=std::to_string(reinterpret_cast<uintptr_t>(node));
            for(t_size i=0;i<node->get_children_count();++i) walk(node->get_child(i),key);
        }
    };
    bool installed=false;
    for(auto item:contextmenu_item::enumerate()) {
        if(!playcount_module(item.get_ptr())) continue;
        installed=true;
        // Static commands are grouped by their parent menu group.
        const std::string parent=pfc::print_guid(item->get_parent_()).c_str();
        for(unsigned i=0;i<item->get_num_items();++i) {
            roots.emplace_back(item->instantiate_item(i,tracks,caller));
            walk(roots.back().get(),parent);
        }
    }
    if(!installed) return playcount_rating_result::absent;
    const int target=rating_menu_target(names,value);
    if(target<0) return playcount_rating_result::missing;
    if(nodes[size_t(target)].second & contextmenu_item_node::FLAG_DISABLED) throw std::runtime_error("Playback Statistics rating command is unavailable for this track.");
    nodes[size_t(target)].first->execute(tracks,caller);
    return playcount_rating_result::rated;
}
// Match foo_nowbar's Playback Statistics integration, without requiring it.
inline bool rating_command(contextmenu_node* node,const std::string& target,const std::string& parent={}) {
    if(!node) return false;
    for(t_size i=0;i<node->get_num_children();++i) {
        auto* child=node->get_child(i); if(!child || !child->get_name()) continue;
        const auto path=parent.empty()?std::string(child->get_name()):parent+"/"+child->get_name();
        if(child->get_type()==contextmenu_item_node::type_command && stricmp_utf8(path.c_str(),target.c_str())==0) {
            if(child->get_display_flags() & contextmenu_item_node::FLAG_DISABLED_GRAYED) throw std::runtime_error("Playback Statistics rating command is unavailable for this track.");
            child->execute(); return true;
        }
        if(child->get_type()==contextmenu_item_node::type_group && rating_command(child,target,path)) return true;
    }
    return false;
}
inline void write_special_column(HWND parent,metadb_handle_ptr track,special_column kind,int value) {
    metadb_handle_list tracks; tracks.add_item(track);
    if(kind==special_column::rating) {
        // The full view includes commands hidden in Preferences > Display > Context Menu.
        contextmenu_manager::ptr menu; contextmenu_manager::g_create(menu); menu->init_context(tracks,contextmenu_manager::flag_view_full);
        if(rating_command(menu->get_root(),"Playback Statistics/Rating/"+(value?std::to_string(value):"<not set>"))) return;
        const auto result=playcount_rating(tracks,value);
        if(result==playcount_rating_result::rated) return;
        // While foo_playcount is installed, a rating never goes to the file tags.
        if(result==playcount_rating_result::missing)
            throw std::runtime_error("foo_playcount is installed, but its Rating command was not found. The rating was not written to the file.");
    }
    auto filter=fb2k::service_new<column_tag_filter>(kind==special_column::rating?"RATING":"MOOD",value?std::to_string(value):std::string());
    metadb_io_v2::get()->update_info_async(tracks,filter,parent,metadb_io_v2::op_flag_partial_info_aware,nullptr);
}
}
