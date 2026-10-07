# foo_modernplaylist

**Modern Playlist** is a high-performance, native Windows playlist component for foobar2000 featuring smooth Direct2D/DirectWrite rendering, a column layout shared by every playlist in a panel, album grouping with configurable headers, asynchronous artwork, interactive rating and mood tags, customizable cover backgrounds, instant search, full drag-and-drop, and an integrated Excel-style playlist manager with playlist locking and a status bar.

The same DLL provides a native **Default User Interface (DUI)** element and a **Columns UI (CUI)** panel; no JavaScript panel host is required. foobar2000 continues to own playlists, metadata, playback, the queue, locks, and undo history.

---

## Contents

- [Requirements and Installation](#requirements-and-installation)
- [Adding the Panel](#adding-the-panel)
  - [Default UI](#default-ui)
  - [Columns UI](#columns-ui)
  - [Enabling and Placing Built-in Tabs](#enabling-and-placing-built-in-tabs)
- [Features Overview](#features-overview)
- [Panel Settings](#panel-settings)
  - [Apply, Reset, Import and Export](#apply-reset-import-and-export)
  - [General](#general)
  - [Playlist Manager Settings](#playlist-manager-settings)
- [Playlist Manager and Status Bar](#playlist-manager-and-status-bar)
  - [Tab Appearance](#tab-appearance)
  - [Tab Navigation and Management](#tab-navigation-and-management)
  - [Locking Playlists](#locking-playlists)
  - [Status Bar and Playlist Sorting](#status-bar-and-playlist-sorting)
  - [Autoplaylists](#autoplaylists)
  - [Special Playlists](#special-playlists)
- [Columns and Appearance](#columns-and-appearance)
  - [Built-in Column Catalog](#built-in-column-catalog)
  - [Column Editor and Title Formatting](#column-editor-and-title-formatting)
  - [Sorting and Adjacent Divider Resizing](#sorting-and-adjacent-divider-resizing)
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
  - [View Buttons](#view-buttons)
  - [Text Highlighting](#text-highlighting)
  - [Prefix Typing Search (Type-to-Locate)](#prefix-typing-search-type-to-locate)
- [Drag and Drop](#drag-and-drop)
- [Custom Vertical Scrollbar](#custom-vertical-scrollbar)
- [Hover Tooltips and Row Appearance](#hover-tooltips-and-row-appearance)
- [Track Operations and Context Menus](#track-operations-and-context-menus)
- [Keyboard Shortcuts and Gestures](#keyboard-shortcuts-and-gestures)
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

The component's preferences page has a single, empty **General** tab. Every panel's settings live in its own **[Panel Settings](#panel-settings)** dialog.

---

## Adding the Panel

### Default UI

1. Select **View → Layout → Enable layout editing mode**.
2. Right-click the element you wish to replace (or add a new container) and choose **Playlist renderers → Modern Playlist**.
3. Click **View → Layout → Enable layout editing mode** again to exit editing mode.

> [!NOTE]
> **Layout Editing Mode Integration:** While Default UI's layout editing mode is active, right-clicking anywhere within Modern Playlist—including tracks, groups, artwork, empty space, column headers, scrollbar, playlist tabs and buttons, status bar, and search controls (including their dropdown lists)—opens Default UI's standard UI-element editing menu without modifying track selections. Keyboard context-menu requests do the same. Exiting layout editing mode immediately restores Modern Playlist's own menus.

The panel inherits Default UI's playlist fonts, tab fonts, and colors, and follows light and dark mode changes immediately.

### Columns UI

1. Open **File → Preferences → Display → Columns UI → Layout**.
2. Insert **Playlist views → Modern Playlist** into your desired splitter container.
3. Click **Apply**.

The Columns UI panel uses Segoe UI typography and the component's own light/dark palette with the host accent color. Both hosts share the same implementation; each panel saves its own configuration, including Columns UI layout export/import.

### Enabling and Placing Built-in Tabs

The built-in playlist tabs are **disabled by default** (including in old layouts) to avoid duplicating host-managed tabs:

- **Toggle Tabs:** Press `Tab` while the playlist, header, or manager has focus, or right-click any column header and select **Header Bar → Show playlist tabs**.
- **Bottom Tabs:** Select **Header Bar → Playlist manager below playlist** (also in **Panel Settings → Playlist Manager**) for a bottom tab bar. Search stays positioned above the track list.
- **Replacing Host Tabs:**
  - *Default UI:* Enable layout editing mode, copy the Modern Playlist element, and paste it over the outer Playlist Tabs element. Replacing that container with Modern Playlist also works, but creates fresh panel settings.
  - *Columns UI:* In Columns UI Layout preferences, change the outer Playlist tabs container to a **Vertical splitter**, retaining its child panel, then apply.

---

## Features Overview

- **Direct2D / DirectWrite Viewport:** Smooth pixel scrolling, hardware-accelerated rendering, partial invalidation, touch panning/inertia, MSAA accessibility, and a buffered GDI fallback.
- **15 Built-in Columns:** One column layout shared by every playlist in a panel, with configurable title formats, extra lines, sort expressions, DUI-style relative colors, `$rgb(r,g,b)` colors, and live radio stream artist detection.
- **Interactive Ratings & Mood:** Click and drag Material stars to set ratings (1–5) via Playback Statistics or file tags; click the heart to toggle loved tracks (`MOOD`).
- **Album Grouping:** Two- or three-row headers with four configurable text lines, track counts and duration, header artwork, playlist-specific templates, a **No grouping** template, and auto-collapse to the playing group.
- **Custom Artwork Backgrounds:** Track front cover, track artist image, custom images with wildcards, or pseudo transparency, with blur, opacity, dimming, and crop modes.
- **Search & Quick Locating:** 8 search fields, playlist filtering, Media Library search snapshots, field-scoped highlighting, IME-aware input, and configurable prefix typing search.
- **Complete Drag & Drop:** Move or Ctrl-copy tracks, drop onto playlist tabs to append copies, drop onto `+` to create a new playlist, and drag files directly from Windows File Explorer.
- **Themed Vertical Scrollbar:** Custom smooth-scrolling scrollbar matching the active theme, with symmetric chevrons, proportional thumb sizing, auto-hiding, and double-click shortcuts.
- **Excel-Style Playlist Manager & Status Bar:** Antialiased sheet tabs, colored emoji in tab names, a reveal-playing button, **Lock** for protecting playlists, a total item count, and one-click ↑ / ↓ playlist name sorting.
- **Panel Settings Dialog:** Six tabs with **Apply**, **Reset**, and `.mpsettings` **Import…** / **Export…**, following foobar2000's light/dark mode.

---

## Panel Settings

Open **Panel Settings** from any of these places:

- **Panel Settings…** in the column-header menu, or **View → Panel Settings…** in the track menu (opens on **General**).
- **Panel Settings...** in the playlist-tab menu (opens on **Playlist Manager**).
- **Columns → More...** in the column-header menu (opens on **Columns**, with the column under the pointer selected).
- **Groups → More...** in either menu (opens on **Groups**).

The dialog has six tabs: **General**, **Cover Background**, **Columns**, **Groups**, **Playlist Manager**, and **Search**. The window and every page follow foobar2000's light/dark mode, including changes while the dialog is open. Columns and Groups are taller than the dialog and scroll with their scrollbar or the mouse wheel; the wheel never changes a closed dropdown's value.

### Apply, Reset, Import and Export

- **Apply** (`Alt+A`) validates every tab and applies the settings while keeping the dialog open, so you can adjust, apply, inspect, and continue.
- **OK** applies and closes. **Cancel**, `Escape`, or closing the window discards only edits made since the last Apply.
- Invalid values keep the dialog open on the relevant tab (focusing the offending field on Columns and Groups) and prevent every tab's changes from being applied.
- **Reset** asks for confirmation, then loads a new panel's defaults into every tab.
- **Import…** loads a `.mpsettings` file (up to 1 MiB, any configuration version from 1 to 28), migrating it as a saved layout would be.
- **Export…** validates the tabs and saves exactly what the dialog shows, including edits not yet applied: an 8-byte `MPLSETS1` tag followed by the configuration record.

Reset and Import are staged like any other edit and take effect only on **Apply** or **OK**. When applied, they also replace the settings that have no tab: search row visibility, field, scope, and highlight color; column-header visibility and alignment; playlist tab visibility; scrollbar and status-bar visibility; and the panel zoom. The global special-playlist options and playlist locks are not part of a panel.

### General

- **Double-click action:** **Play** or **Add to playback queue**. `Enter` always plays.
- **Selection opacity** (0–255, default **60**): a light selection tint that keeps the normal text colors. At 255, selected rows use an opaque selection with the selected-text color.
- **Focus outline opacity** (0–255, default 180).
- **Alternate row colors** and **Show Row Extra-Line Infos**.
- **Hover tooltips:** enable, dwell time, selected-track targeting, and title format (see [Hover Tooltips](#hover-tooltips-and-row-appearance)).
- **Rating style** (**Style 1 - Stars** or **Style 2 - Dots**) and Rating **Spacing** (**Default** or **Compact**).
- **Minimum row height:** rows normally use the roomy modern height (the larger of 30 pixels and the font height plus 10; with extra lines, the larger of 36 pixels and 1.9 font heights plus 4). Enabling the option uses the entered value from 1 to 300 pixels for compact classic rows, but rows never shrink below the font height plus 2 pixels. Values scale with DPI and panel zoom.

The **Search** tab holds the [typing search field](#prefix-typing-search-type-to-locate); **Cover Background**, **Columns**, and **Groups** are described in their own sections below.

### Playlist Manager Settings

| Option | Default | Description |
| --- | --- | --- |
| **Playlist manager below playlist** | Off | Places the manager below the playlist instead of above the search row. Same setting as **Header Bar → Playlist manager below playlist**. |
| **Hide close buttons on playlist tabs** | On | Leaves name-only tabs. Available only on this page. The playing playlist keeps its speaker and a locked playlist its padlock. |
| **Show colored emoji in tab names** | On | Renders supported emoji in the font's own colors, including in the dragged tab's preview. Uncheck for monochrome emoji in the tab text color. |
| **Highlight active tab text** | Off | Draws the active tab's name in the highlight color. |
| **Show active tab underline** | On | Underlines the active tab's name. Drop targets keep their underline either way. |
| **Show separators between inactive tabs** | On | Thin separators between inactive tabs. |
| **Use custom highlight color** / **Choose color...** | Off | Replaces the host highlight color for highlighted text and underlines. |

All of these are saved per panel, including settings files and Columns UI layout export/import. Changing the underline never shifts the text or icons.

---

## Playlist Manager and Status Bar

### Tab Appearance

The strip follows Microsoft Excel's sheet tabs, in the panel's own colors:

- **Active sheet:** joins the edge next to the playlist with concave flares and a small gap along its free edge. Free corners and flares share a 3-pixel radius, with 4×4 antialiasing and no shadow.
- **Hovered tabs** use the same attached contour with a distinct fill; the active sheet is painted above adjacent hover flares.
- **Tabs directly above the column header:** with the manager on top, the search row hidden, and column headers shown, the strip takes the playlist background and the active sheet takes the header color, so the active tab flows into the header instead of floating on it as an island.
- **End padding:** the strip keeps 3 pixels of padding before the first tab and after the last, so both end tabs show complete flares, including next to the scroll arrows and `+`.
- **Artwork backgrounds:** with a **Whole panel** background image, inactive tabs and action buttons receive a subtle tint, and the active sheet reveals the original artwork so it stays joined to the playlist image.
- **Tab names** use DirectWrite with Direct2D color-font rendering, so supported emoji keep their colors over solid fills and artwork, with a monochrome GDI fallback.
- **Sizes:** tabs have no minimum width and at most 240 pixels. A name-only tab reserves 8 pixels on both sides; each 14-pixel icon (close button, speaker, or padlock) tightens the frame to 6 pixels before the name, 2 pixels before each icon, and 4 pixels after. All sizes scale with DPI and panel zoom.
- **Icons:** the `+` and `◎` buttons share the view buttons' bar weight, drawn on whole pixels for crisp lines; the tab scroll arrows share the scrollbar arrows' weight.

### Tab Navigation and Management

- **Switch Playlists:** Click any tab to activate its playlist. The playing playlist shows a speaker indicator instead of a close button.
- **Create Playlists:** Click **+** or press `Ctrl+N` to create and activate a new automatically named playlist without prompting for a name.
- **Scroll Tabs:** Scroll overflowing tabs with the mouse wheel/trackpad or the left/right scroll buttons. An arrow is dimmed at its end of the strip. The **+** button keeps its own space.
- **Reveal Playing Playlist (◎):** When tabs overflow, a **◎ Show playing playlist** button appears after **+**. It activates the playing playlist and scrolls its tab into view (even while paused). Without a search query in that playlist, it also reveals the playing track and expands its group. When stopped, it reveals the selected tab.
- **Reorder Tabs:** Drag tabs horizontally. A translucent preview, an insertion line, and edge scrolling show the destination. Press `Escape` to cancel. The optional Media Library playlist stays first.
- **Close Playlists:** Choose **Remove** from the tab menu, or enable close buttons in **Panel Settings → Playlist Manager** and click `×`. Removal uses foobar2000's confirmation UI.
- **Keyboard Navigation:** With the manager focused, use `Left`, `Right`, `Home`, and `End` to switch playlists. Press `F2` to rename the active playlist.

Right-click any tab, the **+** button, or the scroll buttons to open the manager menu:

| Command | Description |
| --- | --- |
| **Insert… / Add…** | Create a normal playlist, a new autoplaylist, or a pre-defined autoplaylist before the clicked tab or at the end. Also contains the Media Library, Historic, and Queue Content toggles. |
| **Load a Playlist…** | Open the host's playlist file loading dialog. |
| **Save this Playlist…** | Activate and save the clicked playlist using the host dialog. |
| **Duplicate** | Create a normal playlist containing all tracks from the clicked playlist. Duplicating an autoplaylist creates a static snapshot. |
| **Rename… / Remove** | Rename or delete the clicked playlist (subject to locks and special playlist rules). |
| **Lock** | Lock or unlock the clicked playlist (see [Locking Playlists](#locking-playlists)). |
| **Move left / Move right** | Shift the tab's position (the pinned Media Library remains first). |
| **Autoplaylist properties…** | Open foobar2000's native autoplaylist dialog for an autoplaylist. |
| **Add files… / Add folder…** | Activate the clicked playlist and open the host file/folder browser. |
| **Sort playlists by name (ascending / descending)** | Sort all playlist tabs by name, as the status-bar buttons do. |
| **Panel Settings...** | Open Panel Settings on the Playlist Manager tab. |

Commands target the clicked playlist by GUID, so they stay correct even if playlists are reordered while a menu or dialog is open.

### Locking Playlists

**Lock** in the tab menu (checked while locked) protects the clicked playlist with a single Modern Playlist lock:

- **Blocks:** adding, removing, reordering, and replacing items, renaming the playlist, and removing it.
- **Still allowed:** double-click playback, tag edits, copying tracks out of the playlist, and moving its tab.
- **Indicator:** the tab shows a 🔒-style padlock beside its name, placed like the playing playlist's speaker. Locked playlists never show a close button.
- **Persistence:** locked playlists are remembered by GUID and locked again when foobar2000 starts.
- **Unavailable** (grayed out) for autoplaylists, the special playlists, and playlists already locked by foobar2000 or another component.

Choose **Lock** again to unlock.

### Status Bar and Playlist Sorting

The bottom status row is enabled by default (**Show status bar** in the header menu or the track menu's **View** submenu) and stays at the bottom regardless of the manager's position:

- **Item Count:** Shows the active playlist's total item count (e.g. `1,420 items`), including repeated tracks and tracks hidden by search filters or collapsed groups. Shows `0 items` for an empty playlist and `No active playlist` when there is none. Double-click the count to show the playing track.
- **↑ / ↓ Sort Buttons:** Sort all playlist tabs by name in ascending or descending Windows Explorer order: Windows' case-insensitive Unicode comparison handles Chinese and other languages, accents, symbols, and embedded numbers (`Playlist 2` before `Playlist 10`), and honors Explorer's numerical-sorting policy. Equal names keep their order, the active playlist stays active, and the pinned Media Library stays first. The buttons are disabled when fewer than two playlists can be sorted.
- **Styling:** Pressing a button adds a subtle fill that clears on release; keyboard focus shows an underline that follows Windows' focus-cue setting. Right-clicking the status row does nothing, preventing accidental menu popups.

### Autoplaylists

- **New Autoplaylist…** opens a dialog for a playlist name, foobar2000 query, sort title format, and a **Keep sorted** checkbox. Invalid queries and formats are rejected before anything is created. foobar2000 owns and saves the autoplaylist definition.
- **Pre-defined Autoplaylist** provides presets for common library queries:
  - *Tracks never played:* `%play_count% MISSING OR %play_count% IS 0`
  - *Tracks played in the last 5 days:* `%last_played% DURING LAST 5 DAYS` (sorted by `%last_played%`)
  - *Tracks unrated:* `%rating% MISSING OR %rating% IS 0`
  - *Tracks rated 3 to 5:* `%rating% GREATER 2 AND %rating% LESS 6`
  - *Tracks rated 4 / Tracks rated 5:* `%rating% IS 4`, `%rating% IS 5`
  - *Loved Tracks:* `%mood% GREATER 0`

Results depend on the tags and statistics in your library; this component does not write playback statistics itself.

### Special Playlists

The manager's **Insert… / Add…** submenu includes three checkable special-playlist toggles below **Pre-defined Autoplaylist**. They are initially off and saved globally across panel instances and restarts:

| Special Playlist | Behavior |
| --- | --- |
| **Media Library** | A fixed `ALL` autoplaylist containing your entire library, kept sorted and pinned at index 0. |
| **Historic** | Logs each newly started track in order, including repeated plays. Logging runs once globally, even with multiple or no visible panels. |
| **Queue Content** | A read-only mirror of the playback queue, including duplicate entries, with each row's queue position in the State column. Closing its tab or choosing **Remove** disables the mirror without clearing the actual queue. |

Special playlists are tracked by GUID, so existing playlists with the same names are never taken over. Turning a toggle off removes its playlist through the host's confirmation; cancelling keeps it enabled.

---

## Columns and Appearance

### Built-in Column Catalog

Modern Playlist includes 15 built-in columns. **All playlists in a panel share one column layout**: visibility, order, widths, alignment, and custom formats follow you when switching or creating playlists. Older saved layouts gain newly added built-ins as hidden entries.

| Column | Default Ref | Default Alignment | Default Visible | Primary Format / Behavior |
| --- | --- | --- | --- | --- |
| **Cover** | `Cover` | Left | No | Album front cover artwork thumbnail. |
| **State** | `State` | Center | **Yes** | Play/pause indicator, queue numbers, selection mark. |
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

Right-click any column header and open **Columns** to toggle visibility. Entries are listed in natural name order (`Column 2` before `Column 10`) and keep that order when checked or dragged. At least one column must remain visible. **More...** at the end opens **Panel Settings → Columns** with the right-clicked column selected.

### Column Editor and Title Formatting

**Panel Settings → Columns** lists every column with its editor on the same page:

- **Show this column**, **Title**, **Title format**, **Extra-line TF**, **Sort format**, **Semantic ref**, **Alignment**, and **Width weight (%)**.
- **Primary and Extra-Line Formats** are previewed against the focused track, including playing-track information. **Title formatting help** opens foobar2000's reference.
- **Sort format:** the expression used when sorting by this column; the primary format is used if it is blank.
- **Semantic ref:** associates the column with special behavior (`State`, `Mood`, `Rating`, `Cover`, `ArtistArt`, `Index`, or plain text). Renaming a column does not change its behavior.
- **New column** adds a visible `%title%` column; **Delete column** removes custom columns and extra copies of a built-in; **Reset columns** restores the default catalog. Drag column headers in the panel to reorder columns.

Invalid input (a missing title, an invalid title format, or a weight outside 0–100) keeps the column selected and focuses the field.

### Sorting and Adjacent Divider Resizing

- **Sorting:** Click a header to sort the current playlist ascending or descending by its sort expression. A separate chevron shows the direction without moving the centered label. Filtered sorting preserves hidden tracks' slots, and locks prevent prohibited sorting.
- **Width Transfer:** Dragging an internal header divider transfers width strictly between the two visible columns beside it—one grows while the other shrinks—leaving all other columns unchanged.
- **Minimum Widths:** 32 scaled pixels, while Rating keeps room for all five slots.
- **Automatic Fitting:** Columns always fit the panel width using their saved ratios. If the minimums exceed the window, the overflow is clipped at the right edge; there is no horizontal scrollbar.
- The outer right edge cannot be dragged. `Escape` or losing capture cancels a resize and restores the original widths.
- Headers are centered by default; **Header Bar → Headers follow content alignment** changes this, and `Ctrl+T` toggles the header row.

### Coloring and Display Variables

- **DUI-Style Relative Colors:** `<text>`, `<<text>>`, and `<<<text>>>` dim text toward the row background by 25%, 50%, or 75%; `>text<`, `>>text<<`, and `>>>text<<<` blend toward the highlight color. Colors follow theme, selection, and playing-row changes in both hosts:
  ```text
  [%title% <<$ifequal(%itunesadvisory%,1,🅴,)>>]
  ```
- **Inline Colors (`$rgb(r,g,b)`):** Color the following text until the next color code or the end of the line (values 0–255). For Mood and Rating columns, the first `$rgb()` overrides the icon's accent color:
  ```text
  $if(%title%,$rgb(80,170,240)%title%,)
  $rgb(255,120,170)$if(%mood%,1,0)
  $rgb(255,255,50)$if2(%rating%,0)
  ```
  Put color markup in result branches, never in conditions or comparisons, and use a separate sort format for colored columns.
- **Context Variables:**
  - `%isplaying%`: `1` only for the playing track occurrence (including when paused); empty otherwise.
  - `%list_index%`: 1-based index of the track in the playlist before search filtering.
  - `%list_total%`: total track count in the playlist before search filtering.
- **Radio Stream Artist Formatting:** The default Artist format switches between track metadata and live stream titles.
- **Playing Track Highlight:** The playing occurrence's text and State indicator use the host's highlight color, including while paused or selected.

---

## Interactive Special Columns

Mood and Rating work in both hosts and both renderers. They act on the **clicked track** without changing the selection, never start playback or a drag, and write tags asynchronously through foobar2000. Tag edits are not part of playlist undo, and playlist locks do not prevent them.

### Playback and Queue State

The **State** column (`ref="State"`) provides compact indicators:
- **Idle Rows:** a small, faint rectangular background. Queued tracks show compact, bold queue numbers (`01`, `02`, …) inside it; repeated queue entries list every position (`01, 03`).
- **Selection:** selected, non-queued, non-playing tracks show a thin, antialiased vector checkmark (nominal 18px) that scales with DPI and zoom.
- **Playback Animation:** while playing, alternates once per second between a solid triangle (Wingdings 3 `U+0075`) and an outline triangle (`U+0077`) at one fixed position. Paused playback holds the outline triangle. Segoe UI Symbol is used if Wingdings 3 is unavailable.
- **Queue Content:** shows each row's own queue position.

### Mood Tagging

The **Mood** column (`ref="Mood"`) displays a solid Material heart (nominal 15px):
- Loved hearts are pink (`RGB(255,120,170)`); inactive hearts are a faint version of the row text color. Default format: `$if(%mood%,1,0)`.
- **Toggle:** click the heart to set `MOOD=1` or remove the `MOOD` tag. Empty output, `0`, and `?` count as inactive.

### Star Ratings

The **Rating** column (`ref="Rating"`) displays five solid Material stars (nominal 16px):
- Filled stars are yellow (`RGB(255,255,50)`) with a subtle shadow; empty positions show faint stars (**Style 1 - Stars**, default) or small dots (**Style 2 - Dots**). **Spacing** offers **Default** or **Compact**. Default format: `$if2(%rating%,0)`.
- **Click to Rate:** click any star (1–5) to set that rating. Clicking the current rating clears it.
- **Drag to Preview:** drag across stars to preview; the rating commits once on release. `Escape` or leaving the row cancels.
- **Storage Backend:** uses foobar2000's **Playback Statistics** rating command when present; otherwise writes or clears the file's `RATING` tag.

---

## Album Groups and Artwork

### Grouping Patterns and Display

Enable grouping via **Groups → Enable Groups** in the header or track context menu:
- New panels offer two built-in templates: **Album** (grouped by Album Artist, falling back to Artist, then Album and Disc Number) and **No grouping** (a flat list).
- **Change Group Pattern** selects a template; **Apply Group Sorting** sorts the playlist by the current template. **Groups → More...** opens **Panel Settings → Groups**, where templates are added, edited, and deleted (up to 64): **Label**, **Group key**, four header lines (**Top left**, **Top right**, **Bottom left**, **Bottom right**), **Sort order**, **Playlist filter**, and **Show group headers**.
- **Header Appearance:** headers are two or three rows tall. Each of the four lines has its own font size offset (−2 to +4 points) and bold setting; by default the top line is 1 point larger and bold and the bottom line 1 point smaller. The track count and total duration are appended to the bottom-right text (e.g. `FLAC | 12 tracks | 45:07`).
- **Styling:** every group header has a full-width 1px top divider, and the playing group's header text uses the host highlight color.
- **Row Parity:** alternating row shading restarts at the first track of each group with the normal row color.
- Header fields, artwork, and dividers scroll together on one physical-pixel grid, so mixed font sizes never shimmer.

### Artwork Thumbnails and Placeholders

- **Group Header Artwork:** with **Groups → Display artwork in group headers** (on by default), visible **Cover** and **Artist Art** columns leave the header row and draw their artwork as squares at the left of each group header, cover first.
- **Artwork Beneath Headers:** with that option off, the columns stay in place and draw their artwork beneath each group header at the column's width; short groups gain padding rows.
- **Row Covers:** without grouping, visible **Cover** and **Artist Art** columns draw compact thumbnails inside each row.
- **Theme-Adapted Placeholders:** missing covers show a faint disc ring on a subtle square, and missing artist images show a head-and-shoulders silhouette on the same square. Both blend the panel background toward the text color, so they follow light and dark themes.
- **Asynchronous Engine:** images decode on background threads, with up to 256 MiB of decoded thumbnails per panel and a 64 MiB Direct2D bitmap cache. Press `F5` or select **Refresh artwork** to reload images, including previously missing files.

### Auto-Collapse and Playlist Filters

- **Collapse / Expand:** click any group header to toggle it. The **Groups** menu also offers **Collapse All**, **Expand All**, and **Collapse groups by default**.
- **Auto-collapse to playing group:** collapses all groups except the currently playing group. Manual expanding or collapsing is kept until the playing track changes.
- **Playlist Filters:** with **Groups → Enable Playlist Filter**, each template's semicolon-separated **Playlist filter** assigns it to playlists by name; `*` marks a default template. Explicit names take precedence over the default. While an assignment applies, **Change Group Pattern** is grayed out. Filters affect grouping only; all playlists keep the panel's shared columns.

For example, assign **No grouping** to `Radio;Queue Content;Historic` and give **Album** the `*` filter to group music while leaving radio, queue, and history playlists flat.

---

## Cover Background and Transparency

Open **Panel Settings → Cover Background**. Backgrounds start disabled; check **Enable cover background** to turn them on. Settings are saved per panel.

### Background Sources

- **Track front cover:** the playing track's album art, falling back to the focused track when stopped.
- **Track Artist Image:** the playing (or focused) track's artist artwork.
- **Custom image:** an image file from a static path or a title-format expression (e.g. `C:\Music\Art\%artist%\*.jpg`). `*` and `?` match the filename only; the first match in case-insensitive alphabetical order wins. Images are decoded through Windows Imaging Component (JPEG, PNG, BMP, and other installed WIC formats).
- **Pseudo transparency:** makes the panel appear see-through to the container that hosts it. The panel asks its parent window (for example a Columns UI splitter) to paint its background into an off-screen copy, then blurs and blends that copy. It never reads screen pixels, so the desktop, desktop icons, and other windows do not show through. A parent that paints only a plain color gives a plain result. The copy is reused across repaints and refreshed when the panel or its parent moves or resizes, the main window resizes, colors or settings change, or on `F5`. It behaves the same on Windows 10 and Windows 11.

### Appearance and Blending

- **Placement Modes:** *Center Crop* scales to fill the panel, cropping evenly; *Top Crop* fills the panel while keeping the image's top edge. Older layouts saved with Stretch, Fit, or Center use Top Crop.
- **Regions:** **Whole panel** places one image behind the track list, scrollbar, column headers, playlist tabs, status row, and search row; **Playlist** covers the track area, its scrollbar, and the status row.
- **Opacity, Blur & Dimming:** opacity (0–255), box blur radius (0–32 pixels), and **Image dimming** (0–255, default 192), which blends the image toward the panel background. Dimming and placement do not apply to Pseudo transparency.
- **Smooth Transition:** switching tracks keeps the current background until the new artwork has finished decoding, avoiding blank flashes. Backgrounds stay fixed while tracks scroll.

---

## Search and Quick Locating

### Search Bar and Scopes

Press `Ctrl+F` while the panel has focus, middle-click the playlist or search box, or use **Search → Show search row** to toggle the search bar:

- **Search Button:** the magnifying glass before the box focuses it; as soon as the box has text it changes to **X**, which clears the query immediately and keeps focus in the box.
- **Field Selector:**
  - `All fields`: full foobar2000 query syntax (`artist HAS radiohead`, `%rating% GREATER 3`, Boolean `AND`/`OR`/`NOT`).
  - `Artist`, `Title`, `Album`, `Genre`, `Album Artist`, `Comment`, `Path`: fast, case-insensitive literal substring matches in that field. `Path` searches the full location, including directories, filename, and extension (or a stream URL).
- **Scope Selector:**
  - `Current playlist`: filters the visible tracks after a 500 ms pause in typing.
  - `Media library`: searches the entire library and writes the results into a reusable snapshot playlist named `Media Library Search`.
- **Escape** clears the box; hiding the row keeps the query and filter. Invalid queries show an inline notice.
- **IME Support:** native IME composition and candidate windows (e.g. Chinese, Japanese, Korean) display cleanly; the `Search…` placeholder only appears while the box is empty and unfocused.

### View Buttons

Two icon buttons at the end of the search row switch the view without sorting tracks or clearing the search:
- **Ungrouped view** turns grouping off while keeping the current pattern and collapse preferences.
- **Grouped view** turns on the current pattern if it has headers, otherwise the first pattern with headers.

### Text Highlighting

Matching literal text is highlighted in track cells, extra lines, and group headers:
- Field-specific searches only highlight formats drawn from that field (an Album search does not highlight a matching Artist).
- Change the highlight color via **Search → Highlight color…**; contrasting text is chosen automatically.

### Prefix Typing Search (Type-to-Locate)

Jump to tracks by typing while the track list has focus:
- **Prefix Matching:** matches the beginning of the configured field (e.g., typing `AB` jumps to **ABBA**, not Black Sabbath).
- **Typing Search Field:** **Panel Settings → Search** selects **Artist** (default), **Title**, **Album**, **Genre**, **Album Artist**, **Comment**, **Path**, or the current **Group key**.
- **Overlay:** a large centered overlay shows your typed string and match status.
- **Timeout:** the typed prefix clears after **1 second** of inactivity or when focus leaves the list. Press `Backspace` to remove the last character or `Escape` to dismiss it. Typing search never changes the search-box query or filter.

---

## Drag and Drop

Modern Playlist provides full OLE drag-and-drop integration:

- **Reorder Tracks:** after moving past the Windows drag threshold and holding for 150 ms, drag selected tracks to reposition them. An insertion line marks the destination, including before or after a group.
- **Copy Tracks (Ctrl-Drag):** hold `Ctrl` while dropping within the playlist to insert copies.
- **Drop onto Playlist Tabs:** drop tracks onto any tab to append copies to that playlist.
- **Drop onto `+` Button:** drop tracks onto **+** to create, populate, and activate a new playlist.
- **File Explorer Drops:** drag audio files, folders, or supported playlist files from Windows Explorer into the track list, onto tabs, or onto **+**.
- **External Drag:** drag tracks to other foobar2000 components or external applications.
- **Lock Protection:** autoplaylists and locked playlists (including playlists locked with **Lock**) reject prohibited operations with the standard not-allowed cursor.

---

## Custom Vertical Scrollbar

- **Native Palette Integration:** drawn in the panel's background, text, and selection colors, over the artwork when a background image is used.
- **Symmetric Chevrons:** crisp, antialiased chevrons whose stroke follows the scrollbar width at the current DPI.
- **Proportional Thumb:** reflects visible-to-total content height (including group headers and padding) with a minimum size.
- **Smooth Eased Scrolling:** dragging the thumb, clicking arrows (line scroll), clicking the track (page scroll), and the mouse wheel all ease toward the target.
- **Double-Click Shortcuts:** double-click the thumb to show the playing track; double-click an arrow to jump to the top or bottom.
- **Auto-Hide & Toggle:** hides automatically when all tracks fit. **Show scrollbar** in the context menus hides it permanently while keeping wheel, keyboard, and touch scrolling.

---

## Hover Tooltips and Row Appearance

### Hover Tooltips

Hover tooltips are **disabled by default**; enable them in **Panel Settings → General**:
- **Targeting Modes:**
  - *Show selected-track hover tooltips (default):* hovering any track shows the first selected track in playlist order, including off-screen tracks.
  - *Hovered track:* uncheck the setting to show the track beneath the mouse cursor.
- **Live Playback Updates:** tooltip text updates in real time for changing stream metadata and playback fields without flickering or jumping.
- **Work Area Clamping:** the tooltip grows for longer text and stays within the current monitor's work area.
- **Multiline Formats:** press `Enter` in the settings editor (or use `$char(10)`) for multiline tooltips. Hover delay is configurable from 100 to 5,000 ms.

### Two-Line Rows and Alternating Colors

- **Show Row Extra-Line Infos:** toggles two-line rows that show each column's extra-line format in a smaller secondary font.
- **Alternating Rows:** alternating shading restarts within each group.
- **Selection:** selected rows use a light selection tint by default (Selection opacity 60); raise it to 255 for opaque selection.

---

## Track Operations and Context Menus

Right-clicking tracks opens the track menu, starting with **View**, **Show playback queue**, and a separator:

- **Native Commands:** foobar2000's standard track commands (Tagging, Convert, ReplayGain, Properties, Add to playback queue) with their shortcut labels.
- **Selection… Submenu:**
  - *Crop:* keeps selected visible tracks and removes the other visible tracks.
  - *Remove:* removes selected visible tracks from the playlist (does not delete files).
  - *Add to…:* appends selected tracks to an existing playlist or a new one.
  - *Send to…:* replaces the destination playlist's contents with the selected tracks and activates it.
- **Show playback queue:** activates the read-only **Queue Content** special playlist.
- **View Submenu:** Panel Settings, extra-line info, scrollbar, status bar, column headers, Search, and Groups.

Right-clicking a column header opens the header menu: Panel Settings, Show Row Extra-Line Infos, Show scrollbar, and Show status bar; then Show NOW Playing and Refresh artwork; then the **Header Bar**, **Search**, **Groups**, and **Columns** submenus.

---

## Keyboard Shortcuts and Gestures

| Shortcut / Gesture | Target / Context | Action |
| --- | --- | --- |
| `Ctrl+F` | Anywhere in panel | Show and focus the search box, selecting its query. |
| `Ctrl+N` | Panel | Create and activate a new playlist. |
| `Ctrl+T` | Panel | Toggle column headers. |
| `Tab` | Playlist / header / manager | Toggle playlist tabs. |
| `F2` | Playlist / header / manager | Rename the active playlist. |
| `F5` | Panel | Refresh artwork and cover backgrounds. |
| `Ctrl+Wheel` | Panel | Zoom the panel from 50% to 250% in 10% steps. |
| `Middle-Click` | Track list or search box | Toggle the search row. |
| `Double-Click` | Status count or scrollbar thumb | Show the playing track. |
| `Double-Click` | Scrollbar arrow | Jump to the top / bottom. |
| `Enter` | Track list | Play the focused track. |
| `Delete` | Track list | Remove selected visible tracks from the playlist. |
| `Ctrl+A` | Track list | Select all visible tracks. |
| `Ctrl+C` / `Ctrl+X` | Track list | Copy / cut selected tracks. |
| `Ctrl+V` | Track list | Append tracks from the clipboard. |
| `Ctrl+Z` / `Ctrl+Y` | Track list | Undo / redo playlist edits. |
| `Alt+Up` / `Alt+Down` | Track list | Nudge selected tracks up / down. |
| `Left` / `Right` | Tab bar | Switch to the adjacent playlist. |
| `Home` / `End` | Tab bar | Jump to the first / last playlist. |
| `Escape` | Track list | Cancel typing search, a column drag, or a track drag. |
| `Escape` | Search box | Clear the search text. |

---

## Building and Validation

### Windows: MSBuild (Recommended)

Requires Microsoft C++ build tools with the **MSVC v145** toolset, MSBuild, and a Windows SDK. From a Visual Studio Developer Command Prompt or PowerShell:

```powershell
msbuild foo_modernplaylist.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal
```

Output: `build/x64/Release/foo_modernplaylist.dll`. Win32 and Debug configurations are also available; match foobar2000's architecture.


### Windows: CMake Alternative

```powershell
cmake -S . -B build-cmake-x64 -A x64 -T v145
cmake --build build-cmake-x64 --config Release --parallel
ctest --test-dir build-cmake-x64 -C Release --output-on-failure
```

### Tests

Portable Python-driven suites exercise production logic with host doubles; on Linux, CMake configures them without building the Windows component:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Native Windows suites (for example `python tests/manager_native_test.py cl.exe` and `python tests/search_layout_test.py cl.exe`) run from a Visual Studio developer shell. These checks do not replace a component build and live foobar2000 testing; see the [Windows validation checklist](tests/WINDOWS.md).

---

## Credits

- Built with the foobar2000 SDK, PFC, Columns UI SDK, and Windows APIs. This component is provided as-is for educational and personal use.
