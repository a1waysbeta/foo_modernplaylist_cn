#pragma once
#include <SDK/foobar2000.h>
namespace modern_playlist {
// The playlist-manager Lock command: one Modern Playlist lock that blocks adding,
// removing, reordering and replacing items, renaming and removing the playlist.
// Locked playlists are remembered by GUID across restarts.
bool user_locked(t_size playlist);
bool can_toggle_user_lock(t_size playlist);
void toggle_user_lock(t_size playlist);
}
