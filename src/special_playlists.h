#pragma once
#include <SDK/foobar2000.h>
namespace modern_playlist {
enum class special_playlist { library, history, queue };
bool special_enabled(special_playlist kind);
bool is_queue_playlist(t_size playlist);
void show_playback_queue();
void toggle_special(special_playlist kind);
bool special_reserved(t_size playlist);
bool can_close_playlist(t_size playlist);
void close_playlist(t_size playlist);
bool library_pinned(t_size playlist);
}
