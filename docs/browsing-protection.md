# Browsing without playback or generated-playlist side effects

Ticket 009 targets fooyin `bb8d9f0371b254571fffec310dce3ee1ce005c87`
(0.13.1), using installed public SDK headers and audited Qt/layout protocols.

## Supported behavior

- Playlist selection, navigation, scrolling and switching the **viewed** playlist
  remain available. Playback-follows-cursor is temporarily disabled, including
  attempts to enable it from settings while locked. Both initial values restore.
- A track already scheduled before entry remains scheduled. The guard never
  clears the generic scheduled-track slot or restores a playback/index snapshot.
- Library-tree and filter selections still update their own selection, track
  context and downstream filtering. Their selection-generated playlist writes are
  suspended for every destination, including separate result playlists.
- Library-tree embedded search and connected search bars retain live searching.
  Unconnected search bars retain text editing but do not generate result
  playlists. Plain Enter is safe; modifier+Enter result-generation overrides are
  consumed. Quick Search's immediate show-all path is also protected.
- Unlock restores configuration without emitting a saved selection or replaying
  suppressed input. Subsequent input resumes normal host behavior.

## Source-sensitive route audit

| Route | Host side effect | Protection |
| --- | --- | --- |
| `EditablePlaylistSession::updateSelectionState` | Calls `scheduleNextTrack` when playback-follows-cursor is enabled, including selection published by current-playlist filters. | Public SettingsManager key `Playlist/PlaybackFollowsCursor` is held false. Existing scheduled playback is untouched. |
| `LibraryTreeWidget::applySelectedTracks` → `syncSelectionPlaylist` | Before the content write, KeepAlive may move tracks/remap the playback source or rename it to the playback-result name. Creation/replacement can reset the destination index to -1 and automatically switch the viewed playlist. | Per-instance public layout field `PlaylistEnabled` is held false. Locks alone cannot prevent these lifecycle/index side effects. |
| `FilterControllerPrivate::handleSelectionChanged` | Library-source filters call `TrackSelectionController::executeAction(SendNewPlaylist)` with preservation and auto-switch options. Current-playlist filters instead publish matching selection. | Hold `PlaylistEnabled` false while leaving the selection and recomputation pipeline intact. Cursor suppression covers matching playlist selection. |
| `SearchWidget::searchChanged` | Unconnected autosearch/Enter runs an asynchronous query and can create, replace, rename or switch to a search playlist. Prefix matching and appended query names can collide with an active source. | Use the public layout's `SearchMode=2` (`PlaylistFilter`). Its result handler only publishes search to connected widgets; it never enters `findOrAddPlaylist`. Connected searches branch to their receivers before this mode is consulted. |
| `SearchWidget::keyPressEvent` | Ctrl/Alt/Shift+Enter can force a generating mode independently of the saved mode. | Consume these submissions before the receiver; leave text editing and plain Enter available. |
| Unconnected search's `QLineEdit::textChanged` | Arms a debounce timer which could otherwise generate results after an immediate unlock. | Temporarily mask this unconnected editor's notifications, preserving its original signal-block state. Text edits still occur; no debounce is armed or replayed. Embedded/connected search notifications remain available. |
| `SearchWidget::showEvent` in Quick Search | Loads `Searching/QuickSearchState` and may immediately search when ShowAll is enabled. | Journal and temporarily set the state's SearchMode to 2 before show-event delivery. Restore only the mode field, preserving the rest of the state. |

SearchMode values at this baseline are Library=0, Playlist=1, PlaylistFilter=2 and
AllPlaylists=3. Concrete library/filter/search headers are private and are not
included. Runtime identification uses Qt class names; configuration uses public
`FyWidget::saveLayoutData`/`loadLayoutData`.

## Overlay lifetime and persistence

`BrowsingGuard` is installed after ActionGuard and before PlaylistGuard. It records
originals before changing registered settings, per-instance flags or search modes.
Library/filter `configChanged` notifications synchronously reapply the override.
Dispatch-time discovery covers complete late widgets before input, show events,
queued callbacks and model timers. Construction-time child notifications are not
used for discovery. Recreated widgets with the same persisted ID reuse the first
journal original rather than recording the temporary value as a new original.

Library/filter configuration overlays preserve current presentation and search
configuration. They omit model/group/header restoration, and briefly block only
the affected root's synchronous configuration publications while applying the
overlay. They do not block player, playlist-controller, model or selection signals.
The library tree's embedded search bar needs no mode override: its audited direct
target always bypasses result-playlist creation.

Each widget record identifies a layout type and exact public widget ID. Library
trees and filters already persist searchable IDs. Standalone SearchBar widgets
temporarily gain `PersistId` so any layout save containing their temporary mode
has a recoverable identity; their original feature flag restores on cleanup.

Unlock, partial-entry rollback, shutdown and startup recovery use the same adapter:

1. Detach settings subscriptions, filters and scoped editor/widget properties.
2. Restore settings in memory and on disk, verify the result, and fsync the file
   and ancestor directories before journal acknowledgement.
3. Inspect managed `.fyl` files under `Gui::layoutsPath`, including inactive
   layouts, and merge back originals by exact widget ID. Use QSaveFile and durable
   sync; update LayoutProvider's corresponding in-memory layouts too.
4. Restore matching live widget overrides. Missing/deleted identities are never
   guessed from names, grouping, positions or playlist names. Other valid saved
   values are preserved when they contain no plugin-owned temporary value; invalid
   settings/layouts retain unresolved recovery records.

Standalone close handlers can save widget configuration without a persisted ID.
The guard dispatches close with original configuration in scope, then reapplies
protection if the widget survives, including an ignored close. Quick Search's
separately persisted mode has its own typed recovery record.

This is GUI-origin protection, not controller interception. Already-submitted
asynchronous requests may finish under the approved entry boundary. Broader
coordinated/in-flight transition hardening remains ticket 010.

## Verification

The focused `lock.browsing` suite covers both cursor originals, settings-page
reenablement, clean shutdown, configuration reload, late widgets, recreation with
the same ID, ignored standalone close, exact-ID saved-layout repair, unrelated
saved values, invalid layouts, failed persistence and recovery retry. The real-host
suite uses actual database-backed library tracks and populated tree/filter models.

The host matrix exercises both initial cursor settings, KeepAlive on/off and
separate/colliding result names, with automatic switching enabled. It verifies
selection and live embedded searching, unchanged source identity/name/contents/
index and scheduled playback, viewed-playlist switching, configuration reenable
attempts, no unlock replay, and resumed generation on subsequent input. Search
checks cover all three generating modes, ordinary/forced submissions, immediate
unlock before debounce, Quick Search show-all, close/save and reopening.
An actual layout replacement also checks a connected SearchBar/library-tree pair
while locked, including live queries, unchanged source/index and restored settings.

The three-process smoke check additionally saves real tree and SearchBar
overrides, exits normally while locked, hard-exits without cleanup, then restarts
the same profile. It verifies cursor-follow, quick-search mode and both saved
widget properties recover durably before a new session is allowed. See the
[recorded commands and results](verification.md#ticket-009-verification).

### Ticket 009 verification — 2026-10-23

From the plugin repository, using the pinned installed SDK/runtime described above:

```sh
cmake --build build --parallel 2
QT_PLUGIN_PATH=/tmp/opencode/qt-sqlite/usr/lib/x86_64-linux-gnu/qt6/plugins \
  ctest --test-dir build --output-on-failure
cmake --install build
QT_PLUGIN_PATH=/tmp/opencode/qt-sqlite/usr/lib/x86_64-linux-gnu/qt6/plugins \
  python3 tests/host-smoke.py \
    --host /tmp/opencode/lock-fooyin-sdk/bin/fooyin \
    --plugin /tmp/opencode/lock-plugin-install/lib/fooyin/plugins/fyplugin_lock.so \
    --driver build/tests/hostdriver/fyplugin_locksmokedriver.so \
    --optional-plugin /tmp/opencode/lock-baseline-build/run/lib/fooyin/plugins/fyplugin_wavebar.so \
    --optional-plugin /tmp/opencode/lock-baseline-build/run/lib/fooyin/plugins/fyplugin_spectrogram.so
git diff --check
```

- Public-SDK build succeeds without compiler warnings; production code includes
  no private upstream headers. GCC 14.2.0, Qt 6.8.2 and Linux/offscreen are the
  tested toolchain/runtime.
- CTest: **9/9 entries pass**, including **9 focused browsing checks** with Qt Test
  setup/cleanup. The previous lifecycle, action, playlist, playback and seeking
  suites remain passing. The final full run completed in **27.03 seconds**.
- Actual-host and installed-artifact checks cover normal locked shutdown, hard
  interruption and restart recovery, also with the matching optional widgets.
- The test driver's explicit library reload initially reentered its population
  callback; it now starts once. Its runtime-only queue fixtures are created after
  the longer browsing checks so host periodic persistence never sees fabricated
  track references. Connection tests use the public layout-switch operation,
  including the host's normal searchable-widget registration.
- The expanded host matrix has a 45-second per-process timeout and 150-second
  CTest timeout. One complete SDK rebuild exceeded the shell's initial 120-second
  limit; resuming the build completed successfully.

Required new host markers:

```text
Session Lock smoke: autosearch, forced search submission, quick-search show-all, and search-mode restoration verified
Session Lock smoke: cursor scheduling, generated-playlist collisions, search, switching, and restoration verified
Session Lock smoke: interrupted browsing settings recovered after restart
```
