#include "playlist_locks.h"
#include "special_playlists.h"
#include <SDK/autoplaylist.h>
#include <algorithm>
#include <string>
#include <vector>

namespace modern_playlist {
namespace {
// GUIDs of locked playlists, one per line.
cfg_string saved_locks(GUID{0x397e28ca,0x9049,0x461a,{0xb9,0x27,0x7d,0x41,0x68,0xa9,0xf7,0x5a}},"");

class user_lock : public playlist_lock {
public:
    bool query_items_add(t_size,metadb_handle_list_cref,const bit_array&) override { return false; }
    bool query_items_reorder(const t_size*,t_size) override { return false; }
    // Forced removals (for example deleted files) only notify; they cannot be refused.
    bool query_items_remove(const bit_array&,bool force) override { return force; }
    bool query_item_replace(t_size,const metadb_handle_ptr&,const metadb_handle_ptr&) override { return false; }
    bool query_playlist_rename(const char*,t_size) override { return false; }
    bool query_playlist_remove() override { return false; }
    // Double-click keeps starting playback.
    bool execute_default_action(t_size) override { return false; }
    void on_playlist_index_change(t_size) override {}
    void on_playlist_remove() override;
    void get_lock_name(pfc::string_base& out) override { out="现代播放列表：已锁定"; }
    void show_ui() override {}
    t_uint32 get_filter_mask() override {
        return filter_add|filter_remove|filter_reorder|filter_replace|filter_rename|filter_remove_playlist;
    }
};
struct installed_lock { GUID playlist; service_ptr_t<user_lock> lock; };
std::vector<installed_lock> installed;
bool running=false;

void save() {
    pfc::string8 text;
    for(const auto& entry:installed) text << pfc::print_guid(entry.playlist) << "\n";
    saved_locks.set(text.c_str());
}
std::vector<installed_lock>::iterator find_lock(const GUID& playlist) {
    return std::find_if(installed.begin(),installed.end(),[&](const installed_lock& entry) { return entry.playlist==playlist; });
}
// Playlists already locked by another owner (autoplaylists, Queue Content, other
// components) and the special playlists cannot take this lock.
bool lockable(t_size playlist) {
    auto pm=playlist_manager::get();
    return playlist<pm->get_playlist_count() && !pm->playlist_lock_is_present(playlist) &&
        !autoplaylist_manager::get()->is_client_present(playlist) && !special_reserved(playlist);
}
bool install(t_size playlist) {
    if(!lockable(playlist)) return false;
    auto pm=playlist_manager_v5::get();
    auto lock=fb2k::service_new<user_lock>();
    // Record first: installing notifies panels, which then query the lock state.
    installed.push_back({pm->playlist_get_guid(playlist),lock});
    if(pm->playlist_lock_install(playlist,lock)) return true;
    installed.pop_back();
    return false;
}
void user_lock::on_playlist_remove() {
    const auto entry=std::find_if(installed.begin(),installed.end(),[this](const installed_lock& e) { return e.lock.get_ptr()==this; });
    if(entry==installed.end()) return;
    installed.erase(entry);
    if(running) save();
}
class lifecycle : public initquit {
    void on_init() override {
        running=true;
        auto pm=playlist_manager_v5::get();
        const std::string text=saved_locks.get().c_str();
        // Restore locks on playlists that still exist; forget removed playlists.
        for(size_t begin=0;begin<text.size();) {
            const size_t end=std::min(text.find('\n',begin),text.size());
            const auto line=text.substr(begin,end-begin); begin=end+1;
            const GUID id=pfc::GUID_from_text(line.c_str());
            if(id==pfc::guid_null || find_lock(id)!=installed.end()) continue;
            const auto index=pm->find_playlist_by_guid(id);
            if(index!=pfc::infinite_size) install(index);
        }
        save();
    }
    void on_quit() override {
        running=false;
        auto pm=playlist_manager_v5::get();
        for(auto& entry:installed) {
            const auto index=pm->find_playlist_by_guid(entry.playlist);
            if(index!=pfc::infinite_size) pm->playlist_lock_uninstall(index,entry.lock);
        }
        installed.clear();
    }
};
initquit_factory_t<lifecycle> lifecycle_factory;
}
bool user_locked(t_size playlist) {
    auto pm=playlist_manager_v5::get();
    return playlist<pm->get_playlist_count() && find_lock(pm->playlist_get_guid(playlist))!=installed.end();
}
bool can_toggle_user_lock(t_size playlist) { return user_locked(playlist) || lockable(playlist); }
void toggle_user_lock(t_size playlist) {
    auto pm=playlist_manager_v5::get();
    if(playlist>=pm->get_playlist_count()) return;
    const auto entry=find_lock(pm->playlist_get_guid(playlist));
    if(entry!=installed.end()) {
        // Forget first so panels notified by the uninstall see it unlocked.
        const auto lock=entry->lock; installed.erase(entry);
        pm->playlist_lock_uninstall(playlist,lock);
    } else if(!install(playlist)) return;
    save();
}
}
