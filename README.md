# foo_modernplaylist

**Modern Playlist** is a high-performance, native Windows playlist component for foobar2000 featuring smooth Direct2D/DirectWrite rendering, configurable title-format columns, album grouping, asynchronous artwork, interactive rating and mood tags, customizable cover backgrounds, instant search, full drag-and-drop, and an integrated horizontal playlist manager with a status bar.

The same DLL provides a native **Default User Interface (DUI)** element and a **Columns UI (CUI)** panel; no JavaScript panel host is required.

---

## Contents

- [Requirements and Installation](#requirements-and-installation)
- [Adding the Panel](#adding-the-panel)
  - [Default UI](#default-ui)
  - [Columns UI](#columns-ui)
  - [Enabling and Placing Built-in Tabs](#enabling-and-placing-built-in-tabs)
- [Features Overview](#features-overview)
- [Playlist Manager and Status Bar](#playlist-manager-and-status-bar)
  - [Tab Navigation and Management](#tab-navigation-and-management)
  - [Status Bar and Playlist Sorting](#status-bar-and-playlist-sorting)
  - [Autoplaylists](#autoplaylists)
  - [Special Playlists](#special-playlists)
- [Columns and Appearance](#columns-and-appearance)
  - [Built-in Column Catalog](#built-in-column-catalog)
  - [Column Editor and Title Formatting](#column-editor-and-title-formatting)
  - [Adjacent Divider Resizing](#adjacent-divider-resizing)
  - [Coloring and Display Variables](#coloring-and-display-variables)
- [Interactive Special Columns](#interactive-special-columns)
  - [Playback and Queue State](#playback-and-queue-state)
  - [Mood Tagging](#mood-tagging)
  - [Star Ratings](#star-ratings)
- [Album Groups and Artwork](#album-groups-and-artwork)
  - [Grouping Patterns and Display](#grouping-patterns-and-display)
  - [Artwork Thumbnails and Placeholders](#artwork-thumbnails-and-placeholders)
  - [Auto-Collapse and Playlist Filters](#auto-collapse-and-playlist-filters)
- [Cover Background and Transparency](#cover-background-and-transparency)
  - [Background Sources](#background-sources)
  - [Appearance and Blending](#appearance-and-blending)
- [Search and Quick Locating](#search-and-quick-locating)
  - [Search Bar and Scopes](#search-bar-and-scopes)
  - [Text Highlighting](#text-highlighting)
  - [Prefix Typing Search (Type-to-Locate)](#prefix-typing-search-type-to-locate)
- [Drag and Drop](#drag-and-drop)
- [Custom Vertical Scrollbar](#custom-vertical-scrollbar)
- [Hover Tooltips and Row Appearance](#hover-tooltips-and-row-appearance)
- [Track Operations and Context Menus](#track-operations-and-context-menus)
- [Keyboard Shortcuts and Gestures](#keyboard-shortcuts-and-gestures)
- [Saved Settings and Upgrades](#saved-settings-and-upgrades)
- [Current Roadmap](#current-roadmap)
- [Building and Validation](#building-and-validation)
- [Credits](#credits)

---

## Requirements and Installation

- **foobar2000 2.0 or newer for Windows.** The component relies on playlist GUIDs for persistent playlist tracking across reorders, renames, and restarts.
- **x64 or Win32**, matching your foobar2000 installation architecture.
- **Default UI or Columns UI.** Columns UI is optional; Default UI requires no additional components.

### Installation

1. Download the `foo_modernplaylist.fb2k-component` package.
2. In foobar2000, navigate to **File → Preferences → Components**.
3. Click **Install…**, select the `.fb2k-component` file, click **Apply**, and restart foobar2000.
4. Verify that **Modern Playlist** appears in your installed components list.

---

## Adding the Panel

### Default UI

1. Select **View → Layout → Enable layout editing mode**.
2. Right-click the element you wish to replace (or add a new container) and choose **Playlist renderers → Modern Playlist**.
3. Click **View → Layout → Enable layout editing mode** again to exit editing mode.

> [!NOTE]
> **Layout Editing Mode Integration:** While Default UI's layout editing mode is active, right-clicking anywhere within Modern Playlist—including tracks, groups, artwork, column headers, scrollbar, playlist tabs, status bar, and search controls—opens Default UI's standard UI-element editing menu without modifying track selections. Exiting layout editing mode immediately restores Modern Playlist's rich context menus.

The panel inherits Default UI's playlist fonts, tab fonts, and system/theme color schemes (including automatic light and dark mode switching).

### Columns UI

1. Open **File → Preferences → Display → Columns UI → Layout**.
2. Insert **Playlist views → Modern Playlist** into your desired splitter container.
3. Click **Apply**.

The Columns UI panel uses Segoe UI typography and follows the component's optimized light/dark palette and host accent colors.

### Enabling and Placing Built-in Tabs

The built-in playlist tabs are **disabled by default** to avoid duplicating host-managed tabs:

- **Toggle Tabs:** Press `Tab` while the panel is focused, or right-click any column header and select **Header Bar → Show playlist tabs**.
- **Bottom Tabs:** In the header menu, select **Header Bar → Playlist manager below playlist** for an Excel-style bottom tab bar. Search stays positioned above the track list.
- **Replacing Host Tabs:**
  - *Default UI:* Enable layout editing mode, copy the Modern Playlist element, and paste it directly over the outer Playlist Tabs element.
  - *Columns UI:* In Columns UI Layout preferences, replace the outer Playlist tabs container with a **Vertical splitter** containing Modern Playlist.

---

## Features Overview

- **Direct2D / DirectWrite Viewport:** Smooth pixel scrolling, hardware-accelerated rendering, subpixel font antialiasing, touch panning/inertia, and buffered GDI fallback.
- **15 Built-in Columns:** Configurable title-format strings, extra lines, custom sorting, `$rgb(r,g,b)` colors, and live radio stream artist detection.
- **Interactive Ratings & Mood:** Click and drag Material stars to set ratings (1–5) via Playback Statistics or file tags; click heart to toggle loved tracks (`MOOD`).
- **Album Grouping:** Two-line headers with track counts and duration, cover art thumbnails, semibold titles, 1px top dividers, and auto-collapse to the playing track.
- **Custom Artwork Backgrounds:** Display track cover art, custom image directories with wildcards, or simulated parent transparency with configurable blur and opacity.
- **Search & Quick Locating:** 8 search fields, playlist filtering, locate-first mode, Media Library search snapshots, IME-aware input, literal text highlighting, and 1-second artist prefix typing search.
- **Complete Drag & Drop:** Move or Ctrl-copy tracks, drop onto playlist tabs to append copies, drop onto `+` to spawn a new playlist, and drag files directly from Windows File Explorer.
- **Themed Vertical Scrollbar:** Custom smooth-scrolling scrollbar matching the active theme with symmetric chevrons, proportional thumb sizing, and auto-hiding.
- **Horizontal Playlist Manager & Status Bar:** Tab strip with smooth scrolling, `+` tab creation, close buttons, playing speaker indicators, a reveal-playing tab button, total item count display, and one-click A–Z / Z–A playlist name sorting.

---

## Playlist Manager and Status Bar

### Tab Navigation and Management

- **Switch Playlists:** Click any tab to activate its playlist. The playing playlist displays a speaker indicator glyph in place of the close button.
- **Create Playlists:** Click **+** or press `Ctrl+N` to instantly create and activate a new automatically named playlist without prompting for a name.
- **Scroll Tabs:** Scroll overflowing tabs smoothly with the mouse wheel/trackpad or by clicking the left/right scroll buttons. The **+** button remains fixed and accessible.
- **Reveal Playing Playlist (◎):** When tabs overflow the viewport, a **◎ Show playing playlist** button appears next to **+**. Clicking it selects the playing playlist and scrolls its tab into view (even while paused). If that playlist has no search query active, it also scrolls to and reveals the playing track, expanding its group if collapsed. When stopped, it reveals the currently selected tab.
- **Reorder Tabs:** Drag tabs horizontally to reorder them. A translucent preview and vertical insertion line indicate the new position. Edge scrolling engages when dragging near either end. Press `Escape` to cancel.
- **Close Playlists:** Click the `×` button on any non-playing tab or choose **Remove** from its context menu.
- **Keyboard Navigation:** When the tab bar is focused, use `Left`, `Right`, `Home`, and `End` to switch playlists. Press `F2` to rename the active ordinary playlist.

Right-click any tab, the **+** button, or scroll buttons to open the manager context menu:

| Command | Description |
| --- | --- |
| **Insert… / Add…** | Create an empty playlist or autoplaylist before the clicked tab or at the end. |
| **Load a Playlist…** | Open the host's playlist file loading dialog. |
| **Save this Playlist…** | Activate and save the clicked playlist using the host dialog. |
| **Duplicate** | Create a normal playlist containing all tracks from the clicked playlist. Duplicating an autoplaylist creates a static snapshot. |
| **Rename… / Remove** | Rename or delete the clicked playlist (subject to locks and special playlist rules). |
| **Move left / Move right** | Shift the tab's position (the pinned Media Library remains first). |
| **Autoplaylist properties…** | Open foobar2000's native autoplaylist query configuration dialog. |
| **Add files… / Add folder…** | Activate the clicked playlist and open the host file/folder browser. |
| **Sort playlists by name A–Z / Z–A** | Sort all playlist tabs alphabetically (case-insensitive, stable sort, keeping Media Library first). |

### Status Bar and Playlist Sorting

The bottom status row is enabled by default (**Show status bar** in the header menu or track **View** submenu):

- **Item Count:** Displays the active playlist's total item count (e.g. `1,420 items`), including repeated tracks and tracks currently hidden by search filters or collapsed groups. Shows `0 items` for empty playlists and `No active playlist` when empty.
- **A–Z / Z–A Sort Buttons:** Instantly sort all playlist tabs alphabetically. Sorting is stable, keeps duplicate names in relative order, preserves the active playlist selection, and leaves the pinned Media Library at index zero.
- **Styling:** Buttons feature subtle background-derived pressed fills that clear cleanly upon mouse release, with keyboard focus underlines following Windows accessibility settings. Right-clicking the status row does nothing, preventing accidental menu popups.

### Autoplaylists

- **New Autoplaylist…** opens a creation dialog accepting a playlist name, foobar2000 query string, sort title-format pattern, and a **Keep sorted** checkbox. Keeping an autoplaylist sorted locks track ordering; unchecking it allows manual track reordering within the autoplaylist.
- **Pre-defined Autoplaylist** provides instant presets for common library queries:
  - *Never played:* `%play_count% MISSING`
  - *Played in the last 5 days:* `%last_played% DURING LAST 5 DAYS`
  - *Unrated:* `%rating% MISSING`
  - *Rated 3–5 / Rated 4 / Rated 5:* `%rating% GREATER 2`, etc.
  - *Loved tracks:* `%mood% PRESENT`

### Special Playlists

Under the manager's **Special playlists** submenu, you can toggle three persistent global playlists (saved across panel instances and restarts):

| Special Playlist | Behavior |
| --- | --- |
| **Media Library** | A fixed `ALL` autoplaylist containing your entire library, kept sorted and pinned permanently at index 0. |
| **Historic** | Automatically logs newly played tracks in chronological order, including repeated plays. Logging runs globally even if all panels are hidden. |
| **Queue Content** | A read-only mirror of the current playback queue, including duplicate track entries. Locked against manual track edits and renames; closing the tab or removing the playlist disables the mirror without clearing the actual playback queue. |

---

## Columns and Appearance

### Built-in Column Catalog

Modern Playlist includes 15 built-in semantic columns. Older saved layouts automatically gain newly added columns as hidden entries:

| Column | Default Ref | Default Alignment | Default Visible | Primary Format / Behavior |
| --- | --- | --- | --- | --- |
| **Cover** | `Cover` | Left | No | Album front cover artwork thumbnail. |
| **State** | `State` | Center | **Yes** | Wingdings animated play/pause indicator, queue numbers, selection mark. |
| **Index** | `Index` | Right | No | Underlying 1-based playlist index (`%list_index%`). |
| **#** | `Tracknumber` | Right | **Yes** | `$if2(%tracknumber%,-)` |
| **Title** | `Title` | Left | **Yes** | `$if2(%title%,%filename_ext%)` (Extra line: `$if(%length%,%artist%,)`) |
| **Year** | `Date` | Right | No | `$if(%date%,$year($replace(%date%,/,-,.,-)),'-')` |
| **Artist** | `Artist` | Left | **Yes** | `$if(%isplaying%,%artist%,$if(%length%,%artist%,Stream))` |
| **Album** | `Album` | Left | **Yes** | `$if2(%album%,$if(%length%,'Single','Web radios'))` |
| **Genre** | `Genre` | Left | No | `$if2(%genre%,'Other')` |
| **Mood** | `Mood` | Center | No | `$if(%mood%,1,0)` (Interactive heart icon). |
| **Rating** | `Rating` | Center | No | `$if2(%rating%,0)` (Interactive 5-star rating). |
| **Plays** | `Playcount` | Right | No | `$if2(%play_counter%,$if2(%play_count%,0))` |
| **Bitrate** | `Bitrate` | Right | No | `%__bitrate% kbps` |
| **Time** | `Duration` | Right | **Yes** | `$if2(%length%,'00:00')` (Extra line: `%__bitrate% kbps`) |
| **Artist Art** | `ArtistArt` | Left | No | Artist artwork thumbnail. |

Right-click any column header to open the **Columns** menu to toggle visibility, add new custom columns, edit the clicked column, or reset to defaults. At least one column must remain visible.

### Column Editor and Title Formatting

Select **Columns → Edit this column…** to configure:
- **Primary and Extra-Line Formats:** Evaluated in real time with a live preview showing the currently focused and playing track.
- **Sort Pattern:** Title-format expression used when sorting by this column. Defaults to the primary format if left blank.
- **Semantic Reference (`ref`):** Associates the column with special behavior (`State`, `Mood`, `Rating`, `Cover`, `ArtistArt`, `Index`, or `Text`).
- **Alignment & Width Weight:** Left, Center, or Right text alignment, and proportional width weighting.

### Adjacent Divider Resizing

- Drag any internal header divider to resize columns.
- **Width Transfer:** Resizing transfers width strictly between the two visible columns adjacent to the divider—one grows while the other shrinks, preserving their combined width and leaving all other columns unchanged.
- Minimum column width is enforced at 32 scaled pixels.
- The outer right edge cannot be dragged; a single visible column retains its allocated width.
- Pressing `Escape` or losing capture cancels the drag and restores original widths.

### Coloring and Display Variables

- **Inline Colors (`$rgb(r,g,b)`):** Use `$rgb(r,g,b)` in primary or extra-line title formats to colorize text until the next color code or line end (values 0–255). For Mood and Rating columns, the first `$rgb()` color run overrides the active icon accent color:
  ```text
  $if(%title%,$rgb(80,170,240)%title%,)
  $rgb(239,83,80)$if(%mood%,1,0)
  $rgb(255,193,7)$if2(%rating%,0)
  ```
- **Context Variables:**
  - `%isplaying%`: Returns `1` only for the playing track occurrence (including when paused); empty otherwise.
  - `%list_index%`: 1-based index of the track in the playlist before search filtering.
  - `%list_total%`: Total track count in the playlist before search filtering.
- **Radio Stream Artist Formatting:** The default Artist format dynamically switches between track metadata and live stream titles:
  ```text
  $if(%isplaying%,%artist%,$if(%length%,%artist%,Stream))
  ```
- **Playing Track Highlight:** The active playing track's text and State indicator automatically use the host's accent/highlight color.

---

## Interactive Special Columns

### Playback and Queue State

The **State** column (`ref="State"`) provides unboxed indicators and queue tracking:
- **Selection:** Displays an unboxed Wingdings 2 checkmark (`U+0050`) on selected, non-playing rows.
- **Playback Animation:** While playing, toggles between a solid triangle (Wingdings 3 `U+0075`) and an outline triangle (`U+0077`) once per second at elapsed animation seconds (0, 2, 4… solid; 1, 3, 5… outline). Paused playback holds the outline triangle (`U+0077`) steadily.
- **Playback Queue:** Shows 1-based queue position numbers in the standard font alongside playback or selection glyphs. Duplicate queue occurrences list all queued positions.

### Mood Tagging

The **Mood** column (`ref="Mood"`) displays a solid Material heart shape:
- Displays red when active and muted when inactive. Default format: `$if(%mood%,1,0)`.
- **Toggle:** Click the heart icon on any row to set `MOOD=1` or remove the `MOOD` tag from the audio file.
- Writes metadata asynchronously via foobar2000's tagging system without interrupting playback or clearing playlist selection.

### Star Ratings

The **Rating** column (`ref="Rating"`) displays five solid Material stars:
- Rated tracks display amber stars; unrated tracks show muted stars. Default format: `$if2(%rating%,0)`.
- **Click to Rate:** Click any star (1–5) to set that rating. Clicking the currently assigned rating clears it back to unrated.
- **Drag to Preview:** Click and drag across stars to preview ratings dynamically; the new rating commits once you release the mouse button. Press `Escape` or drag outside the row to cancel.
- **Storage Backend:** Checks for foobar2000's **Playback Statistics** component rating first. If absent, writes or clears the file's `RATING` tag directly.

---

## Album Groups and Artwork

### Grouping Patterns and Display

Enable grouping via **Groups → Enable Groups** in the header or track context menu:
- Default **Album** pattern groups tracks by Album Artist (falling back to Artist), Album Title, and Disc Number.
- **Two-Line Headers:** Display album title, artist, release date (`[$date(%date%)]`), codec, total group track count, and duration.
- **Typography & Styling:** Primary group titles are rendered 10% larger in semibold weight. Every group header features a full-width 1px top divider. The playing group's header text uses the host highlight color.
- **Row Parity:** Alternating row shading restarts at the first track of each group with the normal row color.
- **Compact Single-Track Groups:** Single-track groups omit blank padding rows even if minimum/extra group rows are configured.

### Artwork Thumbnails and Placeholders

- **Group Covers:** When the **Cover** column is enabled with grouping active, it is pinned to the first column and its thumbnail aligns within the two-row group header.
- **Row Covers:** When grouping is disabled, visible **Cover** and **Artist Art** columns render compact thumbnails inside individual track rows.
- **Missing Artwork Placeholder:** Missing covers display a clean, theme-blended disc ring on a subtle square background rather than generic blank space or silhouette art. Missing Artist Art displays an independent artist silhouette.
- **Asynchronous Engine:** Decodes images on background worker threads with a dedicated 256 MiB memory cache and 64 MiB Direct2D bitmap cache. Press `F5` or select **Refresh artwork** to reload images.

### Auto-Collapse and Playlist Filters

- **Collapse / Expand:** Click any group header to toggle its collapsed state.
- **Auto-Collapse to Playing Group:** Automatically collapses all groups except the currently playing group.
- **Manual Overrides:** Manually expanding or collapsing groups while auto-collapse is enabled temporarily preserves your manual adjustments until the playing track changes.
- **Playlist Filters:** Group patterns can specify semicolon-separated playlist names with `*` as a fallback, automatically selecting pattern layouts when switching playlists.

---

## Cover Background and Transparency

Configure backgrounds by opening **Panel Settings… → Cover Background** from the header or track context menu:

### Background Sources

- **Off:** Standard solid theme background.
- **Track front cover:** Displays the playing track's album art, falling back to the focused track when stopped.
- **Custom image:** Loads an image file from a static path or title-format expression (e.g. `C:\Music\Art\%artist%\*.jpg`). Supports `*` and `?` wildcards. Decoded via Windows Imaging Component (WIC) supporting JPEG, PNG, BMP, WEBP, and TIFF.
- **Simulated transparency:** Captures the parent window's client background for a simulated acrylic/translucent effect.

### Appearance and Blending

- **Placement Modes:**
  - *Center Crop:* Scales to fill the panel, cropping evenly from all sides.
  - *Top Crop:* Scales to fill the panel while keeping the top edge anchored.
  - *Stretch / Fit / Center:* Additional scaling modes.
- **Regions:** Apply artwork behind the **Whole panel** (including search and status bars), **Playlist only**, or **Groups only**.
- **Opacity & Blur:** Adjustable opacity (0–255) and fast hardware box blur radius (0–32 pixels).
- **Smooth Transition:** Switching tracks retains the existing background until the new artwork finishes decoding, eliminating blank background flashes.

---

## Search and Quick Locating

### Search Bar and Scopes

Press `Ctrl+F` while the panel is focused or middle-click the track area to toggle the search bar:

- **Field Selector:**
  - `All fields`: Full foobar2000 query syntax (`artist HAS radiohead`, `%rating% GREATER 3`, Boolean `AND`/`OR`/`NOT`).
  - Specific fields: `Artist`, `Title`, `Album`, `Genre`, `Album Artist`, `Comment`, `Path` (searches full file directory, filename, and extension or stream URLs). Specific field searches perform fast, case-insensitive literal substring matches.
- **Scope Selector:**
  - `Current playlist`: Filters visible tracks in real time after 500 ms debounce. Select **Search → Search box locates tracks** to keep all rows visible and jump/scroll to the first match instead.
  - `Media library`: Executes the search across your entire library and writes results into a reusable snapshot playlist named `Media Library Search`.
- **IME Support:** Native IME composition and candidate windows (e.g. Chinese, Japanese, Korean) display cleanly without placeholder overlap.

### Text Highlighting

Matching query terms are automatically highlighted across track cells, extra lines, and group headers:
- Change the highlight color via **Search → Highlight color…**.
- Highlighting calculates contrasting text colors automatically for optimal readability.

### Prefix Typing Search (Type-to-Locate)

Quickly jump to tracks by typing directly on your keyboard while the track list has focus:
- **Prefix Matching:** Matches the beginning of artist names (e.g., typing `AB` jumps to **ABBA**, not Black Sabbath).
- **Group Key Search:** Enable **Search → Typing searches group key** to match album/group titles instead.
- **Overlay:** A large centered overlay displays your typed string and match status.
- **Timeout:** The typed prefix clears automatically after **1 second** of inactivity or when focus leaves the list. Press `Backspace` to remove the last character or `Escape` to dismiss immediately.

---

## Drag and Drop

Modern Playlist provides full OLE drag-and-drop integration:

- **Reorder Tracks:** Click and hold any selected track for 150 ms, then drag to reposition within the playlist. A clear horizontal insertion line indicates the target position.
- **Copy Tracks (Ctrl-Drag):** Hold `Ctrl` while dropping within the playlist to insert duplicate copies.
- **Drop onto Playlist Tabs:** Drag tracks onto any tab in the horizontal playlist manager to append copies to that playlist.
- **Drop onto `+` Button:** Drag tracks onto the **+** button to instantly create, populate, and activate a new playlist.
- **File Explorer Drops:** Drag audio files, folders, or playlist files (`.m3u`, `.m3u8`, `.fpl`) from Windows Explorer directly into the track list, onto tabs, or onto **+**.
- **External Drag:** Drag tracks out of Modern Playlist into other foobar2000 components or external applications.
- **Lock Protection:** Autoplaylists and locked playlists prevent prohibited move/insert operations with the standard not-allowed cursor.

---

## Custom Vertical Scrollbar

Modern Playlist features an integrated custom vertical scrollbar:

- **Native Palette Integration:** Renders using the active panel colors for background, thumb, and track.
- **Symmetric Chevrons:** Crisp, antialiased geometric chevrons at top and bottom.
- **Proportional Thumb:** Dynamically reflects visible-to-total content height (including group headers and padding) with a minimum thumb size.
- **Smooth Eased Scrolling:** Dragging the thumb, clicking arrows (line scroll), clicking the track (page scroll), or using the mouse wheel smoothly eases toward the target position.
- **Auto-Hide & Toggle:** Automatically hides when all tracks fit within the viewport. Can be hidden manually via **Show scrollbar** in context menus while retaining mouse wheel and touch scrolling.

---

## Hover Tooltips and Row Appearance

### Hover Tooltips

Enable tooltips in **Panel Settings… → General**:
- **Targeting Modes:**
  - *Show selected-track hover tooltips (default):* Hovering any track displays metadata for the first selected track in playlist order, including off-screen tracks.
  - *Hovered track:* Uncheck the setting to display information for the specific track beneath the mouse cursor.
- **Live Playback Updates:** Tooltip text updates in real time for changing stream metadata and playback timers (`%playback_time%`, `%bitrate%`) without flickering or jumping.
- **Work Area Clamping:** The tooltip grows for longer text and clamps cleanly to the current monitor's visible work area (including multi-monitor setups with negative coordinates).
- **Multiline Formats:** Supports multiline expressions directly in the settings editor using `Enter` or `$char(10)`. Configurable hover delay (100–5,000 ms).

### Two-Line Rows and Alternating Colors

- **Show Row Extra-Line Infos:** Toggles two-line rows displaying secondary column formats in a compact secondary font.
- **Alternating Rows:** Alternating background shading restarts within each group, ensuring visually consistent group headers.

---

## Track Operations and Context Menus

Right-clicking selected tracks opens the context menu:

- **Native Commands:** Integrates foobar2000's standard track commands (Tagging, Convert, ReplayGain, Properties, Add to playback queue).
- **Selection… Submenu:**
  - *Crop:* Retains selected visible tracks and removes unselected tracks from the playlist.
  - *Remove:* Removes selected visible tracks from the playlist (does not delete files from disk).
  - *Add to…:* Appends selected tracks to an existing playlist or a newly created playlist.
  - *Send to…:* Clears the destination playlist, populates it with selected tracks, and activates it.
- **Show playback queue:** Activates the read-only **Queue Content** special playlist.
- **View Submenu:** Quick access to Panel Settings, extra-line info, scrollbar, status bar, column headers, Search, and Groups.

---

## Keyboard Shortcuts and Gestures

| Shortcut / Gesture | Target / Context | Action |
| --- | --- | --- |
| `Ctrl+F` | Anywhere in panel | Show, focus, and select search box query. |
| `Ctrl+N` | Panel | Create and activate a new playlist. |
| `Ctrl+T` | Panel | Toggle column headers visibility. |
| `Tab` | Playlist / Manager | Toggle playlist tabs visibility. |
| `F2` | Playlist manager | Rename the active playlist. |
| `F5` | Panel | Refresh artwork and cover backgrounds. |
| `Ctrl+Wheel` | Panel | Zoom panel scaling from 50% to 250% (in 10% steps). |
| `Middle-Click` | Track list or Search box | Toggle search bar visibility. |
| `Enter` | Track list | Play focused track. |
| `Delete` | Track list | Remove selected visible tracks from playlist. |
| `Ctrl+A` | Track list | Select all visible tracks. |
| `Ctrl+C` / `Ctrl+X` | Track list | Copy / cut selected tracks. |
| `Ctrl+V` | Track list | Append tracks from clipboard. |
| `Ctrl+Z` / `Ctrl+Y` | Track list | Undo / redo playlist edits. |
| `Alt+Up` / `Alt+Down` | Track list | Nudge selected tracks up / down. |
| `Left` / `Right` | Tab bar | Switch to adjacent playlist. |
| `Home` / `End` | Tab bar | Jump to first / last playlist. |
| `Escape` | Track list | Cancel active typing search, column drag, or track drag. |
| `Escape` | Search box | Clear search text and return focus to track list. |

---

## Saved Settings and Upgrades

Modern Playlist saves configuration **version 16**:
- **Per-Panel Settings:** Column layouts per playlist GUID, widths, grouping patterns, zoom level, background settings, tabs, status bar, and search state are saved individually per panel instance.
- **Columns UI Portability:** Layout export and import fully preserves all Modern Playlist panel configurations.
- **Seamless Migration:** Upgrades transparently from versions 1–15, preserving existing column order, widths, and custom title-format expressions while injecting missing built-ins into the catalog.
- **Global Settings:** Special playlist settings (Media Library, Historic, Queue Content) are saved globally in foobar2000's central configuration.

---

## Building and Validation

### Windows: MSBuild (Recommended)

Requires Visual Studio 2022 with the **C++ Desktop Development** workload (MSVC v145 toolset) and Windows SDK:

```powershell
msbuild foo_modernplaylist.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal
```

Output: `build/x64/Release/foo_modernplaylist.dll`. Win32 and Debug configurations are also available.


### Windows: CMake Alternative

```powershell
cmake -S . -B build-cmake-x64 -A x64 -T v145
cmake --build build-cmake-x64 --config Release --parallel
ctest --test-dir build-cmake-x64 -C Release --output-on-failure
```

---

## Credits

Built with the foobar2000 SDK, PFC, Columns UI SDK, and Windows APIs. This component is provided as-is for educational and personal use.
