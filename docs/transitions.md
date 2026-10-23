# Coordinated transitions and in-flight work

Ticket 010 completes transition hardening at fooyin
`bb8d9f0371b254571fffec310dce3ee1ce005c87` (0.13.1), Linux/Qt. The
[approved submission boundary](../.plan/001-decide-lock-policy.md#6-lock-entry-boundary-and-in-flight-work)
remains authoritative: locking prevents new protected GUI requests as each guard
becomes active; it does not undo work already submitted to the host.

## Entry and restoration ordering

Entry is synchronous, without pumping the event loop or waiting for workers:

1. `ActionGuard` masks registered and audited direct actions.
2. `PlaybackGuard` filters activation/drop phases and inhibits new view drags.
3. `SeekGuard` filters seeking and cancels existing previews through UngrabMouse.
4. `BrowsingGuard` records/suppresses cursor-follow and result-generation settings.
5. `PlaylistGuard` installs playlist input rules and records/applies content locks.

Only successful entry of every guard produces Locked. A synchronous
`protectionFailed` raised from a setter/late-target callback during entry is
latched and rolls back, even if the enclosing guard returns success. A failure
reported by a synchronous Locked observer is handled before the transition call
returns. Reentrant lock/unlock/recovery calls are rejected. A coordinator shutdown
request during a transition is retained and runs after the outer operation; it
cannot be lost merely because the coordinator is busy.

Restoration has three passes, each in reverse order:

1. `prepareRecovery()` detaches persistent enforcement. Browsing disconnects
   subscriptions and removes its overlay discovery. Playlist protection disconnects
   playlist-added callbacks but retains its input filter and editor restrictions.
   Action, playback and seek input barriers remain installed.
2. Replay persistent originals in reverse journal order, acknowledging each only
   after durable restoration. A record belonging to a guard whose preparation
   failed is deferred. Host setters may emit synchronous callbacks here: protected
   actions, seeks, activation, playlist menus and drops remain unavailable.
3. `finishRecovery()` releases playlist/editor, seek, playback/drag and finally
   action restrictions. Action restoration uses current host state. Finish failures
   remain tracked for retry; persistent failures retain their records. Successfully
   prepared barriers are released even when a record fails, and the visible state
   becomes Recovery required rather than continuing to claim Locked.

`leave()` remains the idempotent full ephemeral cleanup used by guard destructors
and standalone tests. The default recovery hooks call leave during preparation;
guards with input barriers explicitly split the two phases. Startup recovery has
no applied ephemeral guards and never resumes the interrupted session lock.

## In-flight request audit

| Work at lock entry | Result and evidence |
| --- | --- |
| Built-in/Spectrogram seek preview | UngrabMouse cancels synchronously. Unlock-before-release cannot revive it, including while paused. Actual-host checks cover the controls; Wavebar mouse cancellation retains the protocol-fixture/source-audit boundary documented in [seeking protection](seeking-protection.md). |
| Accepted drag or held activation input | Remaining protected enter/move/drop, middle-button and activation-key phases are consumed. DragLeave remains deliverable for the host's hover/indicator cleanup. New drags are inhibited through public view properties. The real-host test seeds an accepted queue drag alongside active seek previews and probes input during cursor-setting entry/restoration callbacks. |
| Native drag loop and grab UX | No application-wide `QDrag::cancel()` is issued: the public API does not identify a globally active drag's ownership reliably. An existing native loop/cursor may remain until release/Escape; protected drop delivery is rejected. Offscreen checks establish event ordering, not compositor/native grab or drop-indicator appearance. Ticket 011 owns interactive platform validation. |
| Delayed middle-click callback | Baseline `ExpandedTreeView::mousePressEvent()` queues `middleClicked`. A pre-filter press may execute its callback after locking. New presses are consumed before submission. This is source-audited; no arbitrary queued metacalls are discarded. |
| Cursor-follow schedule | An already scheduled track is preserved and may play naturally. New selection scheduling is suppressed. Actual tree/filter browsing checks verify the schedule/source/index boundary. |
| Playback request waiting for the engine | May commit after Locked. The real public controller test queues engine-boundary commits before entry and verifies that the current track is not restored to an obsolete snapshot. |
| Pending playlist import | May complete under host rules. A queued append to a newly content-locked regular playlist is rejected by the real PlaylistHandler; imports targeting other host routes are not generically cancelled. |
| Submitted queue sort or search worker | Baseline QueueViewer's `Utils::asyncExec` continuation checks its sort token, current item and original queue IDs before reordering. SearchWidget likewise validates its request token/text. Already submitted work may finish under those checks. New protected actions/submissions are blocked; no worker internals or queue snapshot is changed. These continuation details are source audit, not deterministic worker-race tests. |
| Natural source/track progression | Controller/engine notifications are untouched. Both queue modes advance during transitions and reach natural stop during shutdown, without restoring queue history, source, position or current track. |

The audit uses the detached baseline checkout documented in
[verification](verification.md). No upstream changes, private headers, controller
signal blocking or post-change playback undo are used.

## Settings notifications and compatibility boundary

The host registers cursor-follow as the typed public enum
`Settings::Gui::PlaybackFollowsCursor`. That entry emits `settingChangedBool`,
whereas string-only settings emit `settingChangedVariant`. Browsing subscribes to
both public routes; only the matching signal fires. Unsubscribe by persistent key
detaches both before restoration. Typed enabled/disabled defaults and an actual
host settings edit while locked are regression-tested.

Playback/Playback Queue settings pages also contain direct Apply/Reset writes to
stop-after and queue-mode settings. Those configuration pages are not among ticket
001's named protected surfaces; registered playback-menu actions remain covered.
Their direct configuration routes are not advertised as locked. The
[compatibility matrix](compatibility.md) records this boundary explicitly. There is no
after-the-fact resetting of queue mode, stop-after, or natural playback state.

## Recovery and teardown evidence

`tests/host-smoke.py` now uses four processes on one disposable profile: clean
locked shutdown, hard interruption while locked, a second hard interruption from
the cursor-restoration setter during startup recovery, and a final successful
restart. The third process has already acknowledged later playlist/layout records;
the runner requires a smaller nonempty journal, restored saved-layout values and
no clean-shutdown marker. The fourth restores the outstanding setting, preserves
pre-existing playlist locks, acknowledges the remaining records and starts unlocked.

Focused tests cover repeated toggles, synchronous failures during entry/Locked
notification, partial entry after all production guards, restoration callbacks,
deferred coordinator shutdown, natural progression and end-of-playback in both
queue modes, plus actual QPluginLoader unload while locked. The unload check
verifies playlist/settings restoration, toggle destruction/unregistration and
cancellation of receiver-scoped queued work. Native GUI input and synchronous
module unloading from inside the module's own active callback stack are not
established by these tests. See [commands and results](verification.md#ticket-010-verification).

## Ticket 010 verification — 2026-10-23

Verified with the installed pinned SDK/runtime, GCC 14.2.0, Qt/Qt Test 6.8.2,
Debian 13 x86_64 and Qt's offscreen platform. From this repository:

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

- Public-SDK build succeeded without compiler warnings; only the production module
  is installed. All source-sensitive findings above refer to the pinned baseline.
- Full CTest: **9/9 passed**, zero failures (33.33 seconds). Qt Test totals with
  setup/cleanup: lifecycle **13**, coordinator **26**, actions **21**, playlists **21**,
  widget input **7**, playback input **5**, seek input **4**, browsing **11** passed.
- Installed-artifact four-process smoke with both optional modules passed, including
  mixed seek/accepted-drag transition checks and interruption before cursor-setting
  acknowledgement. Final restart preserved prelocked playlists and acknowledged
  all outstanding originals.
- `git diff --check` passed. No upstream modifications or private includes.

The new actual-host checks caught the typed/Variant cursor subscription mismatch
fixed above. Fixture corrections used the registered `PlaybackQueue` factory key,
drop-enabled receivers, GUI-time subscription setup and the actual module settings
path in the isolated unload profile. These are fixture prerequisites, not extra
production host workarounds. Ticket 011 now records a
[compatibility matrix](compatibility.md), including actual host settings-page
disable/re-enable across restarts and native X11 Global Hotkeys verification.
Desktop drag/grab UX and decoded Wavebar mouse-preview sign-off remain open.
