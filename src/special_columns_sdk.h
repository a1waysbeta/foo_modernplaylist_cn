#pragma once
#include <SDK/foobar2000.h>
#include "special_columns.h"
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
        contextmenu_manager::ptr menu; contextmenu_manager::g_create(menu); menu->init_context(tracks,0);
        if(rating_command(menu->get_root(),"Playback Statistics/Rating/"+(value?std::to_string(value):"<not set>"))) return;
    }
    auto filter=fb2k::service_new<column_tag_filter>(kind==special_column::rating?"RATING":"MOOD",value?std::to_string(value):std::string());
    metadb_io_v2::get()->update_info_async(tracks,filter,parent,metadb_io_v2::op_flag_partial_info_aware,nullptr);
}
}
