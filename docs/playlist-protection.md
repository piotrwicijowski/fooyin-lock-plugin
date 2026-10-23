# Playlist protection and recovery

Audited against fooyin `bb8d9f0371b254571fffec310dce3ee1ce005c87`
(0.13.1), Linux/Qt. Production code uses installed public headers only. The
database encoding and runtime widget class names below are version-sensitive
assumptions, rather than new stable host APIs.

## Protected set and restoration

`PlaylistGuard` locks every regular playlist returned by
`PlaylistHandler::playlists()`, and synchronously protects new regular playlists
on `playlistAdded`, including restored playlists. Existing content locks remain
untouched. Autoplaylists receive GUI protection, rather than core content locks.
Hidden temporary playlists are not enumerated or announced by these host APIs;
generated-playlist handling belongs to ticket 009.

Before changing a persistent unlocked playlist:

1. Add a canonical UUID extra property, `SessionLock.RecoveryIdentity`, if absent.
   Reject invalid or duplicate markers; never derive identity from a name, runtime
   UUID, index, or database row ID. The marker remains as permanent identity metadata.
2. Save the marker through `savePlaylist()`, read back the committed property blob
   through a separate read-only SQLite connection, and sync durable storage.
3. Durably journal `guard=playlists`, `kind=playlist-lock`, the marker, and typed
   boolean `original=false`, `temporary=true`.
4. Apply `setPlaylistLocked(true)`, save, and verify/sync again.

Unlock and shutdown detach signal/input enforcement first. The coordinator then
routes records to `recover()`. Only the guard's recorded temporary changes are
removed; pre-existing locks have no mutation record and remain locked. Recovery
retries work even when the original has already been restored in memory or on disk.

Resolution includes active, removed, and pending-removed objects, deduplicated by
object identity. A removed object has its temporary `core/locked` extra property
removed too, so restoring it later cannot resurrect the session lock. Its row must
already be absent from durable storage. A marker missing from both loaded objects
and storage is acknowledged as deleted. Same-named replacements and reused row
IDs are never recovery matches. Duplicate markers, loaded/durable identity
disagreement, unavailable services, corrupt properties and unconfirmed saves leave
records unresolved and block another lock.

If a newly added playlist cannot be protected, `protectionFailed` makes the
coordinator unlock/restore immediately and report the failure. It cannot continue
displaying a successful Locked state with an unprotected late target.

### Durable storage adapter

The core context's public `dbPool` is borrowed with `std::weak_ptr`.
`DbConnectionProvider` supplies the actual host database; no guessed profile path
or global connection name is used. Playlist properties are read from
`Playlists(PlaylistID, ExtraProperties)`, using the audited Qt 6.0 QDataStream
encoding: a `quint32` pair count followed by QString key/value pairs. Truncated,
duplicate-key and unknown lock-value encodings fail verification.

The reader uses a separate `QSQLITE_OPEN_READONLY` clone, so a failed host save or
open host transaction cannot make uncommitted changes look durable. The expected
marker, current row and complete serialized property blob must match. The row ID
is only a consistency check after resolving the UUID, never a recovery identity.
Linux `fsync` flushes the database, its WAL if present, and directory ancestors
before journal acknowledgement. Database writes themselves go through the public
playlist handler; the plugin does not issue SQL updates or change host pragmas.

## GUI route audit

| Route | Baseline enforcement |
| --- | --- |
| Append, replace (both overloads), remove, clear, same-name playlist replacement | `PlaylistHandler` rejects locked regular-playlist changes. |
| Playlist controller edits, undo/redo, sorting, reordering | Controller/session checks plus registered-action suppression. |
| Track-list, manager, organiser and tab drops, including empty-tab drops that create playlists | Application input filter consumes drag-enter/move/drop before receiver filters/handlers. |
| Tab middle-close/create, double-click rename/create, draggable tab order | Targeted mouse-event suppression; ordinary tab selection and scrolling remain available. |
| Configurable tab close, corner add and corner clear buttons | Button mouse/keyboard activation is suppressed; Qt scroll-arrow buttons remain available. |
| Switcher/manager/organiser/tab context-menu deletion, rename, unlock, creation and restore | Suppress these context menus, including activation of an already-open menu. These menus contain unregistered commands. Escape can still dismiss a menu. |
| Manager selected-click inline rename | Temporarily mask view edit triggers; retain and restore current observed host triggers. F2/Delete activation and switcher double-clicks are filtered. |
| Already-open tab/switcher inline rename | Send Escape to the audited `Fooyin::PopupLineEdit` before installing input protection. |
| Autoplaylist query editing, creation/deletion, manual sorting/reordering | Registered actions, switcher guards and drop/header-sort filtering; no fake autoplaylist content lock. |

Audit sources in the pinned host: `src/core/playlist/playlisthandler.cpp`,
`src/core/database/playlistdatabase.cpp`, `src/core/playlist/playlist.cpp`, and
`src/gui/playlist/{playlistcontroller,editableplaylistsession,playlistwidget,playlisttabs,playlistbox}.cpp`,
plus `manager/playlistmanagerwidget.cpp` and `organiser/playlistorganiser.cpp`.

Metadata, natural track/index progression, background library deletion and automatic
autoplaylist regeneration are not frozen or restored from a content snapshot.
Autoplaylists can still change through their natural regeneration path. Direct
controller/API calls remain outside the supported GUI boundary.

## Input infrastructure and compatibility

`WidgetInputFilter` is a scoped application event filter. It resolves runtime
ancestry **at dispatch**, before receiver event filters and widget handlers.
Consequently a new viewport, reparented widget or replacement subtree is protected
on its first event without a timer, constructor-time cast or per-widget filter
installation. The filter is removed on every leave, partial rollback and destruction.

Recognized surface class names are:

- `Fooyin::PlaylistWidget` (including its view, viewport and header);
- `Fooyin::PlaylistTabs` (its `SingleTabbedWidget`, `EditableTabBar`, and buttons);
- `Fooyin::PlaylistBox` (combo box and popup descendants);
- `Fooyin::PlaylistManagerWidget` (view/viewport and inline editor);
- `Fooyin::PlaylistOrganiser` (tree/viewport).

The nearest recognized surface wins. An unrelated `Fooyin::FyWidget` forms a
boundary, preventing tab-header rules from spilling into library/search/third-party
content nested inside a tab container. Unknown widgets receive no restrictions.
Qt's `ScrollLeftButton`/`ScrollRightButton` object names identify tab scroll arrows.
No translated menu labels are used for identification.

This supplies the playlist prerequisite from ticket 006. Discovery/rules for queue,
library, seeking and optional plugin widgets, and the broader browsing checks,
remain in tickets 006–009. Playlist-content context menus, selection, keyboard
navigation, scrolling and unrelated text entry remain available. Direct playback
gestures and browsing-generated writes/settings still await their own guards.

`protects()` and `suppressesGeneratedWrites()` expose the active playlist policy
to ticket 009. Its adapter must suppress generated writes **and** rename/remap/
auto-switch effects for all targets, including autoplaylists; merely checking
`isLocked()` is insufficient.

## Evidence

`tests/playlistguardtest.cpp` uses the real public PlaylistHandler and file-backed
SQLite database. The real-host smoke driver verifies parent/viewport filter
ordering, configurable tab buttons, late/replaced widgets, nested content boundaries,
restoration, normal locked shutdown and hard-interruption/restart recovery.
See [verification commands and results](verification.md#ticket-005-verification).
