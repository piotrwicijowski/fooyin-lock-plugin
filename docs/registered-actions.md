# Registered-action protection

Ticket 004 audit: fooyin `bb8d9f0371b254571fffec310dce3ee1ce005c87`
(0.13.1), Linux, Qt 6.8.2. Identifiers below are case-sensitive; labels and
translations are not used for matching. Implementation: `src/actionguard.cpp`.

## Inventory and invocation routes

For the following commands, protect the command proxy and **all discovered
underlying context actions**, including inactive widget instances. Covered routes
are menus holding the proxy, player controls using `setDefaultAction()`, registered
application shortcuts, and consumers retaining the underlying QAction and calling
`trigger()`. A checkable action cannot toggle through these activation routes.
Host-driven `setChecked()` updates remain available.

The built-in Command Button and script-command handler also resolve these proxies
or context actions. Targeted selection dispatch may temporarily call
`setEnabled(true)` before `trigger()`; the disabled group still prevents activation.
The button may retain the host's selection-derived enabled appearance for that
target. Custom unaudited script/controller dispatch is outside this inventory.

| Identifiers | Purpose | Baseline contexts and consumers |
| --- | --- | --- |
| `Playback.Play`, `Playback.Pause`, `Playback.Stop`, `Playback.PlayPause` | Play from start, pause, stop, play/pause | Global; `PlaybackMenu`, `PlayerControl`, shortcuts |
| `Playback.Next`, `Playback.Previous`, `Playback.NextAlbum`, `Playback.PreviousAlbum` | Manual track/album navigation | Global; playback/skip menus, controls, shortcuts |
| `Playback.RandomTrack`, `Playback.RandomAlbum` | Manual random selection | Global; random menu, random-track control, shortcuts |
| `Playback.Order.Default`, `Playback.Order.RepeatTrack`, `Playback.Order.RepeatAlbum`, `Playback.Order.RepeatPlaylist`, `Playback.Order.ShuffleTracks`, `Playback.Order.ShuffleAlbums`, `Playback.Order.Random` | Playback order, repeat, shuffle | Global; playback-order menu and shortcuts |
| `Playback.StopAfterCurrent`, `Playback.StopAfterCurrent.Reset` | Stop-after state and automatic-reset policy | Global; playback menu and shortcuts |
| `Playback.PlaybackFollowsCursor` | Prevent action-based changes to cursor-follow playback policy | Global; playback menu and shortcuts. Suspending the setting itself belongs to 009. |
| `Seek.Forward.Small`, `Seek.Forward.Large`, `Seek.Backward.Small`, `Seek.Backward.Large` | Registered relative seek commands | Global; shortcuts/command invocation |
| `Playback.ABRepeat.SetStart`, `Playback.ABRepeat.SetEnd`, `Playback.ABRepeat.Enable`, `Playback.ABRepeat.Clear` | Set A/B markers, toggle or clear the loop | Hosts with A–B repeat; playback menu, shortcuts and command buttons. Direct seekbar routes are covered in [seeking protection](seeking-protection.md#ab-repeat). |
| `File.AddFiles`, `File.AddFolders`, `File.AddStreamUrl`, `File.LoadPlaylist` | Import/append/load into playlists | Global; File menu and shortcuts |
| `File.NewPlaylist`, `File.NewAutoPlaylist` | Playlist creation | Global plus per-instance playlist switcher/manager and organiser contexts |
| `File.EditAutoPlaylist` | Autoplaylist query/sort editing | Per-instance switcher/manager and organiser contexts |
| `File.RemovePlaylist` | Delete current playlist | Global; GUI application command/shortcuts |
| `Playlist.Clear`, `Playlist.Lock` | Clear content; normal content-lock toggle (including unlock) | Global; GUI application command/shortcuts |
| `Tracks.AddToCurrent`, `Tracks.AddToActive`, `Tracks.SendToCurrent`, `Tracks.SendToNew` | Append/replace selected tracks or create their destination | Global selection controller; Directory Browser overrides; playlist/track context menus and shortcuts |
| `Tracks.AddToQueue`, `Tracks.QueueNext`, `Tracks.RemoveFromQueue` | Queue insertion/reordering/removal by selected tracks | Global selection controller; per-instance playlist and library-tree overrides; Directory Browser add/next overrides |
| `Edit.Sort.Randomise`, `Edit.Sort.Reverse`, `Edit.Sort.Preset.<id>` | Playlist/queue sorting, including dynamic preset commands | Global disabled fallbacks; per-instance playlist/queue sort handlers; menus and shortcuts |
| `PlaylistOrganiser.NewGroup`, `PlaylistOrganiser.SortAllPlaylists`, `PlaylistOrganiser.SortGroupPlaylists` | Playlist-list structure and ordering | Per-instance organiser context |
| `Tracks.AddToPlaylist`, `Tracks.SendToQueue` | Reserved playlist/queue mutation identifiers | Public constants, defensively protected if registered. The baseline's add-to-other-playlist and send-to-queue implementations also have direct-call routes below. |

Shared edit commands require **context-specific** suppression:

| Identifiers | Protected purpose | Protected contexts |
| --- | --- | --- |
| `Edit.Cut`, `Edit.Paste` | Playlist clipboard mutation | Playlist widget instances |
| `Edit.Undo`, `Edit.Redo` | Playlist content undo/redo | Playlist widget instances; layout-editing contexts remain available |
| `Edit.Clear`, `Edit.Remove` | Clear/remove playlist tracks, queue items, or playlists | Playlist widgets, queue viewers, playlist manager/switcher and organiser instances, where registered |
| `Edit.Rename` | Playlist/group rename | Playlist manager/switcher and organiser instances |

Audited context identity patterns:

- `Context.Global` and `Fooyin.Context.TrackSelection` are fixed public IDs.
- Playlist widget: `Id{Constants::Context::Playlist}.append(widget.id())`, whose
  textual prefix is `Fooyin.Context.Playlist` (no extra separator is inserted).
- Switcher/manager/tabs: `Fooyin.Context.PlaylistSwitcher.<id>`,
  `Fooyin.Context.PlaylistManager.<id>`, `Fooyin.Context.PlaylistTabs.<id>`.
- Queue: `Context.QueueViewer.<id>`.
- Organiser: `Context.PlaylistOrganiser.<unique hash>`.

The `Fooyin.Context.Playlist` prefix covers the built-in playlist family. Shared
edit proxies are inhibited only while their selected source belongs to a protected
context; other contexts and ordinary text-widget cut/paste/undo remain usable.
No entire WidgetContext is disabled or replaced.

Allowed registered commands include `Volume.VolumeUp`, `Volume.VolumeDown`,
`Volume.Mute`, `Edit.Copy`, `Edit.SelectAll`, search/focus/navigation commands,
`Playback.CursorFollowsPlayback`, playlist export/save, `File.Exit`, and
`SessionLock.Toggle`. File metadata/library operations retain their existing host
policy; the session does not make the library or media files read-only.

## Suppression and restoration

`ActionGuard` is installed before startup recovery. Entry requires a live public
ActionManager. Each restricted QAction joins a plugin-owned **disabled,
non-exclusive QActionGroup**. Its QObject ownership stays with the host.

Qt separates group suppression from an action's explicit enabled value. Thus
`setEnabled(true)` cannot enable a grouped action, and `trigger()` cannot activate
it. Crucially, `setEnabled(false)` while already effectively disabled still records
the host's latest intent even when it emits no `changed` signal. This avoids
changed-handler recursion, re-enable windows, and stale enabled-state snapshots.
Signals are not blocked: labels, check states, menus, and buttons keep updating.

Discovery uses public `commands()`, `actionForContext()`, command current contexts,
and WidgetContexts obtained through `contextObject()` and direct widget children.
The latter include contexts constructed before their host registers them, covering
late inactive overrides while another context is forced for a popup. The guard
listens synchronously to `commandsChanged` and each protected proxy's
`currentActionChanged`; it needs no polling or queued disable operation. In-flight
work submitted before entry remains subject to the approved transition boundary.

On context selection, both the new source and its proxy are restricted before the
public selection call returns. Inactive, replaced, and unregistered sources stay
restricted until unlock so a retained consumer cannot escape. QPointers handle
destruction; expired entries are pruned during refresh. Guard-scoped connections
and a refresh reentrancy flag prevent callbacks into dead state or refresh loops.

Unlock/rollback/shutdown disconnect enforcement, remove plugin groups, and let Qt
recompute enabled state from its current host value. Original action-group
membership, if present, is weakly observed and restored using that group's current
state. The audited built-in protected actions have no original groups; custom
plugins' group-level dispatch/exclusivity is not an additional supported route.
Finally, proxies are refreshed from their current underlying actions through an
unchanged public attribute setter/remover. This discards proxy state derived from
temporarily disabled sources while preserving host-disabled sources. No action
state is persisted or entered into the recovery journal.

The design was checked against Qt's public group behavior and the Qt 6.8.2
implementations of `QAction::setEnabled`, `QAction::activate`, and
`QActionGroup::setEnabled`/`addAction`. Fooyin's ProxyAction disconnects all
source `changed` connections when changing sources, so enforcement deliberately
does not depend on a source `changed` connection surviving a focus switch.

## Media keys

- **Registered shortcut delivery:** application media-key bindings use the same
  protected proxy as other shortcuts.
- **Global Hotkeys delivery:** baseline `GlobalShortcutManager::activate()` checks
  the global underlying action's enabled/visible state and calls `trigger()`.
  The guard protects this action even when it is not the current context source.
  Focused tests exercise this dispatch primitive. OS shortcut backends themselves
  were not launched in the offscreen test environment.
- **MPRIS / Media Controls / direct-controller delivery:** outside the action
  boundary. A physical media key is covered only when it takes a protected action
  route, not merely because it is a media key.

## Direct paths reserved for subsequent guards

These are **not protected by ticket 004**, even when their labels resemble entries
above. The full behavior contract is assembled by tickets 005–010.

Ticket 005 now supplies regular-playlist locks, durable restoration and targeted
playlist input guards. See the [playlist audit](playlist-protection.md) for the
completed tab/switcher/manager/organiser routes and autoplaylist policy. The table
below retains ownership of the broader remaining work.

Ticket 007 now adds [production gesture and queue protection](playback-protection.md),
including root-owned unregistered playback/content/queue actions. The historical
004 audit below identifies route ownership; 007's audit records completed routes
and its explicit transition/configuration follow-ups.

| Direct path | Required work |
| --- | --- |
| Playlist/queue/library double-click, Enter, middle-click and configured activation; queue Play Now/Queue Next/Stop After This context actions | 006/007: targeted discovery and gesture/context-menu filters |
| TrackSelectionController's direct play/send-to-queue and per-destination add-to-other-playlist menu callbacks; Directory Browser/library/filter gesture handlers | 007: suppress originating playback/queue/playlist mutation input |
| Queue sort/clear/move/drop handlers and automatic consumption | 007: block manual mutations; permit automatic consumption |
| Playlist tabs' add/close/middle-close, double-click rename, drag reorder/drop; direct manager/organiser edits, list sorts, deleted-playlist restoration callbacks | 005/006/007: regular content locks plus lifecycle/edit input protection |
| Direct playlist context actions (play, crop, stop-after marker), inline/autoplaylist editing, pending imports | 005/007 and 010: content locks/input boundary; pre-submitted imports may complete |
| Seekbar, wavebar and spectrogram pointer/wheel/key/drag handlers | 006/008: seeking input guards |
| Playback Queue settings page changes queue mode directly; other settings pages change order/cursor/stop-after configuration | 007–009 audit and 010 transition integration; Settings itself stays available |
| Cursor-follow scheduling and library/filter/search selection-generated playlists, including result rename/remap/switch | 009: suspend supported side effects while retaining browsing |
| Arbitrary controller calls, scripts/plugins with unaudited direct dispatch, CLI/MPRIS | Outside the approved action enforcement boundary |

Audit source homes in the pinned checkout: `include/gui/guiconstants.h`,
`src/utils/actions/{actionmanager,command,proxyaction}.cpp`,
`src/gui/menubar/{filemenu,editmenu,playbackmenu}.cpp`, `src/gui/guiapplication.cpp`,
`src/gui/controls/{playercontrol,commandbutton}.cpp`,
`src/gui/scripting/scriptcommandhandler.cpp`, `src/gui/sortactionhandler.cpp`,
`src/gui/trackselectioncontroller.cpp`, `src/gui/dirbrowser/dirbrowser.cpp`,
`src/gui/librarytree/librarytreewidget.cpp`,
`src/gui/playlist/{editableplaylistsession,playlistwidget,playlistbox,playlisttabs}.cpp`,
`src/gui/playlist/manager/playlistmanagerwidget.cpp`,
`src/gui/playlist/organiser/playlistorganiser.cpp`, `src/gui/queueviewer/queueviewer.cpp`,
`src/gui/settings/playback/playbackqueuepage.cpp`, and
`src/plugins/globalhotkeys/globalshortcutmanager.cpp`.

See [verification evidence](verification.md#ticket-004-verification) for focused
and real-host results.
