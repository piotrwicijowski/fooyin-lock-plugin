# Tested compatibility and interaction coverage

Automated evidence recorded on **2026-10-23** for fooyin **0.13.1**, commit
`bb8d9f0371b254571fffec310dce3ee1ce005c87`. This is accidental GUI interaction
prevention on the routes below. User-reported manual drag/drop and rendered
Wavebar checks passed on **fooyin 0.14.0, Arch Linux/Wayland, Qt 6.12.0** with
Session Lock built from the latest `master` at test time. Ticket 011 is complete
within the recorded interaction scope.

## Automated test setup

| Component | Setup |
| --- | --- |
| Host / SDK | Unmodified pinned commit; Release host and installed public headers/libraries |
| Session Lock | Debug build, including the installed production module |
| Platform / compiler | Debian 13 x86_64; GCC 14.2.0, C++23 |
| Qt / CMake / Ninja | Qt and Qt Test 6.8.2; CMake 3.31.6; Ninja 1.12.1 |
| Base GUI test | Qt `offscreen`, disposable HOME/XDG profile |
| Additional GUI test | Qt `xcb`, Xvfb 21.1.16, XTest/libXtst 1.2.5 |
| Optional widgets | Wavebar and Spectrogram from the same pinned host checkout |
| Input-handling plugin | Global Hotkeys from the same commit, actual X11 backend |

## Manual test setup

Confirmed by the user on **2026-10-23**:

| Component | Setup |
| --- | --- |
| fooyin | 0.14.0 |
| Session Lock | Built from latest `master` at test time; exact binary commit not supplied |
| Platform / desktop session | Arch Linux, up to date at test time; Wayland |
| Qt | 6.12.0 |
| Wavebar | Rendered waveform displayed during the input checks |

The compositor, exact Qt platform plugin and decoder/output modules were not
specified. The reported passes cover the interactions described below on this
setup; they do not establish a full automated matrix on fooyin 0.14.0.

The required gesture boundary was clarified on **2026-10-23**: protection covers
actions and gestures started after **Locked** is shown, while the lock remains on.
Finish or cancel existing gestures before locking. Pre-lock gestures and their
continuations are outside the required guarantee; the additional cancellation
checks below describe best-effort handling, not a native mid-gesture lock promise.

Other setups and third-party input plugins (including Vim Motions) have no tested
compatibility claim here. Wayland/portal global-hotkey dispatch was not part of the
manual report. The automated checks need no audio device or decoder: progression
uses public engine-notification APIs, not audible playback.

## Reproduce the automated verification

Build the public SDK and plugin using the [build and installation guide](BUILDING.md).
Configure `LOCK_FOOYIN_EXECUTABLE` to enable the real-host CTest entry, then run:

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build
```

The host and database-backed tests require Qt's SQLite runtime driver. In this
container it is an extracted package, so the actual CTest invocation was:

```sh
LANG=C.UTF-8 \
QT_PLUGIN_PATH=/tmp/opencode/qt-sqlite/usr/lib/x86_64-linux-gnu/qt6/plugins \
  ctest --test-dir build --output-on-failure
```

**Result: 9/9 CTest entries passed, zero failures** (34.38 seconds). Focused Qt Test
totals, including setup/cleanup: lifecycle 13, coordinator 26, actions **27**,
playlists 21, widget input 7, playback input 5, seek input 4, browsing 11. The six
additional action rows cover Qt media play, pause, stop, next, previous and toggle
keys through assigned registered shortcuts, before/during/after lock. They deliver
`QKeyEvent`s through Qt's shortcut machinery, independently of a hardware backend.

For the optional-widget/native-hotkey run, build matching host modules:

```sh
cmake -S /path/to/pinned-fooyin -B /path/to/host-build \
  -DBUILD_PLUGINS=ON -DPLUGIN_SELECTION=wavebar,spectrogram,globalhotkeys
cmake --build /path/to/host-build --target wavebar spectrogram globalhotkeys --parallel 2
```

On an isolated X11 display with XTest enabled and `libX11.so.6`/`libXtst.so.6`
available, run with the installed module and matching host:

```sh
xvfb-run -a python3 tests/host-smoke.py \
  --host /path/to/fooyin-sdk/bin/fooyin \
  --plugin /path/to/plugin-install/lib/fooyin/plugins/fyplugin_lock.so \
  --driver build/tests/hostdriver/fyplugin_locksmokedriver.so \
  --platform xcb \
  --global-hotkeys /path/to/host-build/run/lib/fooyin/plugins/fyplugin_globalhotkeys.so \
  --optional-plugin /path/to/host-build/run/lib/fooyin/plugins/fyplugin_wavebar.so \
  --optional-plugin /path/to/host-build/run/lib/fooyin/plugins/fyplugin_spectrogram.so
```

**Result: all seven host processes passed with the installed production module**.
The container used an extracted Xvfb on display `:97` in place of `xvfb-run`;
missing `xkbcomp` was supplied through a PRoot bind. This affects only the test
display setup. The local log is `/tmp/opencode/lock-x11-validation.log`. Required
output includes:

```text
Session Lock smoke: native X11 Global Hotkeys media-key dispatch, suppression, and restoration verified
Session Lock smoke: settings-page disable applied while locked; awaiting restart
Session Lock smoke: disabled host has restored originals and re-enabled the plugin for restart
Session Lock smoke: re-enabled host starts unlocked and locks/unlocks successfully
PASS: real host protection, repeated recovery, and settings-page disable/re-enable across restarts verified
```

The X11 check registers a late protected sort command and an allowed fence command
with global Media Play/Stop bindings. Server-delivered XTest keys activate them
through the actual Global Hotkeys module. The protected source fires before entry,
stays suppressed despite re-enabling attempts while locked, and fires after unlock;
the allowed source continues throughout. Assertions reject fallback to application
shortcuts. This isolates dispatch from audio playback. Use a disposable display
because the check temporarily grabs those two keys.

## Interaction matrix

Automated **Pass** entries were exercised on the pinned setup. **User-reported**
results refer to the separate fooyin 0.14.0 manual setup and report below.
CTest names below are repeatable entry points; real-host checks live under
`tests/hostdriver/`. “Synthetic” means events delivered to actual host receivers,
with receiver-side probes, rather than a native pointer/drag session.

| Surface / interaction | Result and evidence | Reproduce |
| --- | --- | --- |
| Playback buttons, menus, registered shortcuts and retained actions | **Pass.** Proxy/source suppression, actual Qt shortcut dispatch, button/menu activation, refreshes, context changes and current host-state restoration. | `lock.actions`; host `smokedriver.cpp` |
| Registered media keys; Global Hotkeys X11 | **Pass.** Six Qt media-key rows; actual native global dispatch with the matching module. Direct-controller media routes remain excluded. | `lock.actions`; X11 command above, `compatibilitychecks.cpp` |
| Playlist/library/filter/queue double-click, Enter, middle-click | **Pass (synthetic).** Production filters precede receiver probes; actual queue activation also runs without a probe, preserving current/upcoming/queue state. | `lock.playback-input`; host `playbackchecks.cpp` |
| Queue add/next/send/replace, remove, clear, sort and reorder | **Pass.** Registered and direct menu sources resist refreshes. Internal queue-item, track, track-ID and URL payloads, Copy/Move, enter/move/drop phases and previously accepted drags are rejected synthetically. | `lock.actions`, `lock.playback-input`; host `playbackchecks.cpp` |
| Playlist content edits, sort/reorder, rename/delete/create/restore, host unlock | **Pass.** Database-backed content locks plus action/tab/button/header/drop guards; late regular playlists are locked. Pre-existing locks survive restoration. | `lock.playlists`, `lock.actions`; host `smokedriver.cpp` |
| Built-in Seekbar and full-track Spectrogram | **Pass (synthetic).** Mouse, wheel, key input; real preview commits before lock, synchronous cancellation, unlock-before-release, progress/capability updates and recreated controls. Live Spectrogram modes stay interactive. | `lock.seek-input`; host `seekchecks.cpp`, `widgetinputchecks.cpp` with optional modules |
| Wavebar seek input | **Pass (automated + user-reported).** Actual input ordering and wheel/key connections pass; isolated tests cover preview/cancel protocols. With a rendered waveform on the manual setup, mouse clicking, scrolling and keyboard seeking are prevented while locked. | `lock.seek-input`; optional-widget host run; manual report below |
| Selection, scrolling, copy/search/text entry, playlist switching, tree expansion | **Pass.** Populated tree/filter/search models and source/index/schedule assertions; selection/wheel/navigation/editor delivery and actual QTreeView expansion. | `lock.widget-input`, `lock.browsing`; host `browsingchecks.cpp`, `playbackchecks.cpp` |
| Cursor-follow and generated result playlists | **Pass.** Typed/string settings, KeepAlive variants, colliding/separate result names, auto-switch, live search, standalone/connected modes, forced submissions, Quick Search show-all and restoration. | `lock.browsing`; host `browsingchecks.cpp` |
| Natural progression, both queue modes, natural stop | **Pass (controller/engine-notification fixture).** Queue consumption/history, pre-entry commits/imports and advancement through transitions; natural stop without queue/position rollback. Not an audio-device test. | `lock.playlists` |
| Late/replaced widgets/layouts; focus/action refreshes | **Pass.** First-input protection, replaced viewports/Spectrogram modes, saved/connected search layouts, late registration, context changes and host refreshes. | `lock.actions`, `lock.widget-input`, `lock.browsing`; host widget/search checks |
| Unlock, failed entry, shutdown and module unload | **Pass.** Composed rollback, restoration barriers, current action state, original settings/locks, genuinely unseekable next tracks and actual locked QPluginLoader unload. | `lock.coordinator`, `lock.lifecycle`, `lock.seek-input`; host `seekchecks.cpp` |
| Interrupted recovery | **Pass.** Hard exit while locked, another hard exit during partial acknowledgement, final unlocked recovery with empty journal and restored layouts. | `lock.host-smoke`: `interrupt`, `recover-interrupt`, `recover` |
| Host disable/re-enable | **Pass.** Actual Plugins checkbox and Apply/restart prompt; current lock remains until clean exit, disabled restart has no plugin action/initialisation and preserves originals, re-enabled restart locks/unlocks normally. | `lock.host-smoke`: `disable`, `disabled`, `reenabled` |
| Native drag/drop attempts started while already locked | **Pass (user-reported).** On the manual Wayland setup, internal elements do not start dragging; file-browser drops show a prohibited cursor and do nothing. No edits occurred. | Manual report and procedure below |

## Boundaries and known limitations

- CLI, MPRIS, OS media integrations and arbitrary plugins calling controllers
  directly bypass GUI filtering. Protection follows the delivery route, not the
  label on a key. Other input plugins may intercept events first or call services
  directly; their presence is not implicitly compatible.
- Playback/Playback Queue settings-page Apply/Reset writes to queue mode and
  stop-after are outside the named protected action/input surfaces. Cursor-follow
  is separately suppressed through the actual typed host setting.
- Locking does not cancel already submitted engine requests, imports, delayed
  callbacks, search or sort workers. Host-controlled continuations may run.
  Natural queue consumption, existing schedules, background library changes and
  autoplaylist regeneration continue. No playback/queue snapshot is restored.
- Gestures started before Locked are outside the required guarantee, even if their
  release/drop happens later. Lock shortcuts need not work during native drag loops.
  Mid-gesture cancellation/cleanup is best effort, not an acceptance requirement.
- The manual Wayland checks establish drag/drop and rendered Wavebar input
  prevention on the reported setup, not universal compositor behavior or
  Wayland/portal global-hotkey compatibility.
- Disable/re-enable takes effect on host restart. Clean exit while disabled restores
  originals. If interrupted after disabling the plugin, recovery cannot run until
  the plugin is enabled again.
- Unknown widgets/actions and third-party integrations are unaudited. Changing the
  host baseline requires re-auditing class names, widget structure, settings and
  action identifiers. Public SDK use does not make runtime identification stable.

## Manual sign-off

The two outstanding core manual checks are signed off by the user on the setup
recorded above. No further manual confirmation is required to close ticket 011.

### User-reported results — 2026-10-23

- **Native drag/drop: pass for the reported attempts.** No edits occurred. Internal
  elements did not allow dragging to start. Dragging from a file browser showed a
  slashed-circle/prohibited cursor; dropping did nothing.
- **Wavebar input: pass for the reported attempts.** Mouse clicking, scrolling and
  keyboard seeking were prevented while locked, with a rendered waveform displayed.
- **Setup confirmed:** fooyin 0.14.0, Session Lock built from latest `master`,
  up-to-date Arch Linux, Wayland, Qt 6.12.0. These manual results are separate from
  the pinned automated setup.

The report confirms the core prevention checks. It does not separately describe
every procedure variant below, such as widget recreation or an unseekable stream;
existing automated evidence for those cases retains its documented scope.

### Reproduction procedures

These procedures remain available for repeat runs. No interactive desktop test was
performed in this container. The earlier bare-Xvfb drag fixture did not reach an
accepted target before lock and is not counted as protection evidence.

1. **Native drag/drop while already locked.** On a normal Linux desktop, populate
   playlists, library/filter views and a queue. First verify the relevant reorder,
   copy/move and external file-drop routes work unlocked, then finish or cancel each
   gesture. Enable Session Lock and wait for **Locked** before starting new attempts.
   Attempt internal reorder, cross-widget copy/move and external file drags over
   each playlist/queue destination. Internal drag initiation may be prevented
   entirely; that is a valid result. Release or cancel with Escape and verify no
   protected edit/activation occurs and no cursor, indicator or grab remains stuck.
   Confirm selection and scrolling remain usable. Finish all attempts before
   unlocking, then confirm normal dragging resumes. Repeat on each desktop backend
   to be advertised. No lock/unlock during a held drag is required.
2. **Wavebar seeking while already locked, with a decoded waveform.** Load seekable
   local audio with a matching decoder and output, wait for a rendered waveform,
   pause, and verify unlocked mouse-preview seeking commits. Finish that gesture,
   enable Session Lock and wait for **Locked**. Attempt new click/drag, wheel and
   keyboard seeks; verify no seek commits or stuck preview/grab state. Repeat with a
   recreated Wavebar while locked and confirm progress/track updates continue.
   Advance to an unseekable stream before unlocking: its true capability must remain
   unchanged. Finish all gestures before unlocking, then confirm normal seeking on
   seekable audio resumes. Record position/seek evidence and visible cleanup.
