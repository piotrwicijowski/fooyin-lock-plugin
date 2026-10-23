# Playback gestures and queue protection

Ticket [007](../.plan/007-block-playback-and-queue-gestures.md), audited against
fooyin 0.13.1 commit `bb8d9f0371b254571fffec310dce3ee1ce005c87`, Linux/Qt 6.8.2.
The plugin uses installed public headers and Qt runtime identities from the
[widget matrix](widget-input.md).

## Route inventory

| Origin | Host effect | Protection while locked |
| --- | --- | --- |
| Main/detached playlist double-click or middle press | Configured TrackAction: play, send/append/replace playlist, add/queue-next/replace queue | Consume activation before the view signal and `executeClickAction`; all modifiers covered. |
| Playlist root/view Enter/Return | `startPlayback`, including playlist current-index changes before controller dispatch | Consume shortcut override, press and release before receiver filters/handlers. |
| Playlist find bar Return / Ctrl+Return | Play current match / queue current match | Consume Enter phases on out-of-view playlist QLineEdit; text, navigation, copy and Escape remain available. Inline metadata editors are excluded. |
| Library tree double/middle click, root Enter | Direct library playback or configured TrackAction | Consume activation; preserve configured double-click expansion using public QTreeView index/expansion APIs, without emitting `doubleClicked`. Single-click and keyboard expansion remain host-controlled. |
| Filter root Enter or view double/middle click | FilterController configured play/send/queue operation | Consume activation before FilterWidget/ExpandedTreeView signals, including queued middle-click submission. |
| Queue double-click | Queue-source: play item; override mode: dequeue earlier tracks and call next | Consume before the signal, so neither the dequeue nor playback call executes. Enter/middle equivalents are also consumed. |
| Playlist/library Play; playlist stop-after/crop; queue Play now/Queue next/Stop after | Unregistered direct QAction callbacks | Disabled action groups suppress underlying sources, including menu use and host re-enable attempts. |
| Selection add/queue-next/replace/remove, queue Delete/Clear, reverse/randomise/sort presets | Registered context/source actions and direct selection execution | ActionGuard protects proxies and all audited context actions; input guard prevents configured gesture execution. |
| Internal queue reorder and playlist/library/filter-to-queue drops | QueueViewerModel signals call move/insert queue operations | Reject drag enter, move and drop regardless of MIME type or Copy/Move action. |
| Internal/external drops into playlist content/tabs/switchers | Reorder, insert, import or create playlists | PlaylistGuard rejects destination input; PlaybackGuard also covers playlist content. |

QueueViewer accepts its internal queue-row format, QueueTracks, Tracks and TrackIds;
plain external file URLs have no queue import handler on this baseline. Rejection
is independent of payload and also covers future payload variants on these
audited destinations. External URLs accepted by playlist surfaces are rejected
before asynchronous imports start.

Library/filter/search text submission that creates result playlists, and
playback-follows-cursor selection scheduling, are ticket 009's separate side-effect
guard. The input rules here preserve selection; they do not yet suppress those
effects. Directory Browser and other unnamed widgets have not joined the audited
surface set. Settings-page configuration changes remain in the configuration/
transition follow-up; these checks establish ordinary interaction/action routes.

## Direct-action identity and lifetime

At entry, ActionGuard inspects actions owned **directly** by PlaylistWidget,
LibraryTreeWidget and QueueViewer. It excludes registered proxy/source actions
using public Command and root WidgetContext identities; their normal action
policy still applies. In the pinned source, every remaining root-owned action is
a playback, playlist-content or queue mutation. Safe display/configuration menu
actions are owned by menus, and copy/search/select-all sources are registered.
No translated label, status tip, object name or child ordinal identifies an action.
This ownership invariant must be re-audited when the supported host changes.

The same inspection runs before activation/context-menu input to these roots and
before input to any QMenu. Thus first input to late widgets and global Edit menus
retaining late playlist actions is covered. Anonymous actions are inspected only
after construction/at input, rather than during partial action registration where
safe sources may not yet be registered. Disabled groups and weak restoration are
shared with [registered-action protection](registered-actions.md). Menus remain
usable for safe commands; no blanket context-menu suppression is added here.

PlaybackGuard independently owns its application input filter. It inhibits the
public `dragEnabled` property on audited item views at entry and before first
input to late/replaced views. Mouse moves still reach the host for ordinary range
selection, hover and scrolling. Headers, scrollbars and text editors are not
activation targets. Weak view observations exist only for temporary property
restoration; discovery itself is dispatch-time. Unlock/destruction restores
surviving owned restrictions and removes filters. No persistent settings or new
recovery records are introduced.

## Natural playback and transition boundary

Required protection covers actions and gestures started after **Locked** is shown,
while the lock remains active. Pre-lock gestures and their continuations are outside
the guarantee; the remaining-event filtering below is additional best-effort handling.

The guards never intercept controller progression, engine notifications, queue
signals, or model updates. They do not snapshot or restore transport, position,
playlist current index or queue contents. Natural-end progression consumes queue
entries in override mode and advances queue-source position/history under host
control, including across unlock/relock and shutdown.

Remaining move/drop events of a previously accepted drag are rejected; DragLeave
still reaches the host for cleanup. A middle press already handled by
ExpandedTreeView may have posted its activation callback. Likewise, a queue sort
may already be running asynchronously. Submitted requests may complete according
to the approved contract. The [010 handoff](../.plan/010-harden-lock-transitions.md#ticket-007-integration-handoff)
records the assembled transition checks still required.

## Source audit and evidence

Source inspected: `src/gui/playlist/{playlistwidget,playlistview,editableplaylistsession,playlistsearchcontroller}.cpp`,
`src/gui/librarytree/{librarytreewidget,librarytreeview}.cpp`,
`src/gui/filters/{filterwidget,filtercontroller}.cpp`,
`src/gui/queueviewer/{queueviewer,queueviewerview,queueviewermodel}.cpp`,
`src/gui/widgets/expandedtreeview.cpp`, `src/gui/trackselectioncontroller.cpp`, and
`src/core/playback/playercontroller.cpp` in the pinned checkout.

See [verification commands and results](verification.md#ticket-007-verification)
for Qt-only input/browsing tests, actual public-controller natural-end checks and
the production real-host smoke checks. Audio decoding/output is not exercised by
these input and controller-boundary tests.

### Ticket 007 verification — 2026-10-23

From the plugin repository, using the existing pinned SDK/runtime build:

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

- Public-SDK build succeeded without compiler warnings, GCC 14.2.0 / Qt 6.8.2.
- **7/7 CTest entries passed**, zero failures (5.37 seconds). New Qt-only
  `lock.playback-input`: 5 passed including setup/cleanup. The playlist suite now
  has 19 passed, including real-controller natural-end progression in both modes.
- Gesture checks cover all Enter phases/modifiers, double/middle-click, tree
  expansion/selection, find-bar versus inline-editor input, selection mouse moves,
  late/replaced views and temporary drag-property restoration on leave/destruction.
- The real-host driver verifies production filtering before receiver filters,
  actual unprobed queue activation with a populated second row, registered queue
  mutation sources, anonymous menu sources and host re-enable attempts. Queue,
  current/upcoming track, position and stop-after state stay unchanged by the
  protected interactions. Safe select-all and browsing input remain available.
- Internal/cross-widget/file-URL MIME payloads and Copy/Move actions are rejected.
  A test-only earlier filter seeds an accepted drag to verify move/drop rejection
  independently of Qt rejecting the preceding enter, modelling entry during drag.
- The installed artifact passed all three host processes (clean locked shutdown,
  hard interruption, startup recovery), also with matching Wavebar/Spectrogram
  modules loaded. The production module remains the sole install artifact.

Required new marker:

```text
Session Lock smoke: production playback gestures, direct menu actions, queue drops, and browsing verified
```

Verification exposed two fixture details: queue-source history needs an explicit
nonzero retention setting when asserting retained positions, and runtime-only
queue tracks must be removed after assertions before the real host persists them
to its foreign-keyed library database. Both fixtures are corrected; the smoke
runner now also fails on database-statement errors.
