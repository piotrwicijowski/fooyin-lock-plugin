# Seeking protection

Ticket [008](../.plan/008-block-seeking.md), audited against fooyin 0.13.1 commit
`bb8d9f0371b254571fffec310dce3ee1ce005c87` on Linux/Qt 6.8.2.

## Supported interaction matrix

`SeekGuard` consumes `WidgetInputFilter::isSeekInput()` candidates before receiver
filters and handlers. `ActionGuard` separately protects the proxy and underlying
sources of `SeekForwardSmall`, `SeekForwardLarge`, `SeekBackwardSmall` and
`SeekBackwardLarge`, including registered shortcuts and host refreshes.

| Surface | Mouse route | Wheel / keyboard route | Preserved behavior |
| --- | --- | --- | --- |
| Built-in Seekbar, `Fooyin::TrackSlider` | Left press starts a preview; held move updates it; left release emits `sliderDropped` to `PlayerController::seek`. QSlider double-click can enter its press handler. Block all these phases. | Wheel and arrows emit forward/backward seek signals. Also consume Home/End/PageUp/PageDown inherited QSlider value changes, in all key phases and with modifiers/auto-repeat. | Horizontal/vertical orientation, progress and played marker, labels, display menus, focus traversal, right-button cancellation. |
| Matching Wavebar, `Fooyin::WaveBar::WaveSeekBar` | With waveform data, left press/move previews; release emits `sliderMoved` connected to `seek`. QWidget double-click delegates to press. Block all these phases. | Wheel and arrows emit forward/backward seek signals. Consume wheel and all arrow key phases/modifiers/auto-repeat. | Waveform generation/rendering, position and threshold markers, display menus, focus traversal, cancellation. |
| Matching Spectrogram full-track mode, `Fooyin::Spectrogram::SpectrogramSeekBar` | Left press within plot previews; held move updates it; left release directly calls `seek` if displaying the current seekable track. Block all these phases, including double-click. | Vertical wheel and arrows call seek-step methods directly. Consume wheel and all arrow key phases/modifiers/auto-repeat. | Analysis/rendering, axes, position overlays, focus traversal, Escape/right-button cancellation. |
| Spectrogram scrolling/stationary modes | `LiveSpectrogramView` has no seeking handler. | No seek restriction applied. | Live visualisation and ordinary input remain deliverable. |
| Labels, unrelated sliders/widgets, other optional plugins | Outside these identified controls. | No seeking policy inferred from appearance or object name. | Volume and ordinary unrelated input remain available. Other optional seeking widgets are unsupported until audited. |

The controls are identified by audited metaobject names and ancestry, using installed
public headers and Qt APIs only. New controls, reparenting and Spectrogram mode
replacement are resolved on the first input without a queued scan.

## A–B repeat

On hosts providing A–B repeat, Session Lock also blocks marker and loop changes:

- `ActionGuard` inhibits `Playback.ABRepeat.SetStart`, `.SetEnd`, `.Enable` and
  `.Clear`, including their source actions and registered shortcuts. Stable string
  IDs retain build compatibility with older installed SDKs.
- `SeekGuard` consumes middle-button press, release, double-click and held moves
  on the same three seek controls, including Shift/Ctrl combinations. These
  gestures call the player directly rather than triggering registered actions.
  Existing left-button filtering covers marker dragging, and `UngrabMouse` also
  cancels an active A–B marker drag without committing its preview.
- Seekbar context menus create independent **Set A here / Set B here** actions.
  Their mutation-only submenu is identified through the Enable/Clear proxies'
  public source-action links, without matching translated labels or positions.
  `ActionGuard` inhibits the submenu's actions at lock entry, on menu show
  (including child menus), and before menu input. Newly opened and already-open
  menus are covered; display settings remain available.

An existing loop continues playing while locked. Unlock restores action eligibility
using the host's latest state. No upstream changes or private headers are needed.
Regression coverage includes all four registered commands, existing/late context
menus, host re-enabling attempts, unlock restoration, and modified middle-clicks.

## Entry, cancellation and cleanup

Required protection covers gestures started after **Locked** is shown, while the
lock remains active. The cancellation behavior below is additional best-effort
handling for pre-lock gestures, not a requirement to lock during a held preview.

Each of the three controls handles `UngrabMouse`, `WindowDeactivate` and `Hide` by
clearing its seek preview and tooltip without submitting a seek. At entry,
`SeekGuard` first installs its filter, then sends `UngrabMouse` to existing named
controls. An actual mouse grab belonging to the control is released too. The
built-in slider also clears its slider-down state and restores its displayed
current position through that event handler. This is synchronous even while
paused, when no position tick would otherwise clean up the preview.

Protected input also cancels the containing control before being consumed. This
is a backstop for a control reparented into a supported surface mid-gesture. A
synthetic mouse release is never used for cancellation: release is the commit
route. The cancelled preview cannot revive if unlock happens before release.

The guard owns no controller capability, enabled-state override, position snapshot,
timer, widget pointer registry, signal blocker or persistent recovery record.
Track changes and engine seekability updates remain authoritative. Progress,
analysis, painting, timers and host notifications bypass the input rule. Unlock,
rollback, shutdown and destruction detach the scoped filter idempotently.

All audited seek submissions are synchronous from release/key/wheel handlers.
There is no queued seek-preview commit to cancel. A seek already submitted before
guard installation may finish under ticket 001's approved boundary; no attempt is
made to reverse it. Waveform/spectrogram workers and throttlers update display data
or displayed-track selection, not seek submissions, and continue normally.
`setCurrentTrackSeekable(false)` is unnecessary and is never used by production
code. External controller/CLI/MPRIS seeks remain outside the supported boundary.

## Source audit

Inspected in the detached pinned checkout:

- `src/gui/controls/seekbar.cpp`: TrackSlider press/move/release, QSlider key
  fallback, wheel, cancellation events, current-position and track/seekability
  updates, and SeekBar's display-only context menu.
- `src/plugins/wavebar/{wavebarwidget,waveseekbar}.cpp`: preview/data prerequisites,
  cancellation events, direct seek signal connections, position updates and
  throttled displayed-track refresh.
- `src/plugins/spectrogram/{spectrogramwidget,spectrogramseekbar}.cpp`: live versus
  full-track replacement, preview/commit paths, seek-step calls, cancellation,
  track-selection throttling and asynchronous image generation.
- Registered seek commands are covered by the existing
  [action inventory](registered-actions.md).

## Verification evidence

`lock.seek-input` links Qt only and uses isolated metaobject/protocol fixtures.
It exercises the production guard against all three control identities: pre-entry
preview cancellation without a position tick, remaining releases during/after
lock, normal mouse/key interaction after unlock, unchanged unseekable capability,
key phases/auto-repeat, accepted overrides, drag/double-click, preserved safe input,
first-input replacement/reparenting, duplicate entry/cleanup and locked destruction.

The actual-host driver uses the production plugin and registered toggle, not a
test-only rule consumer. It models engine notifications using the public
PlayerController API while paused, with no decoder or audio output required:

- Built-in and full-track Spectrogram mouse previews first demonstrably submit a
  seek unlocked, then are cancelled at lock entry. Unlock-before-release verifies
  entry cancellation independently of the per-input backstop.
- Actual controls reject mouse/key/wheel input before receiver probes, and reject
  real unprobed handlers while locked. Arrow and wheel seek connections work again
  after unlock on all three controls.
- Built-in progress and duration update during lock; a new unseekable track stays
  unseekable across unlock; subsequent engine capability updates work normally.
- Fresh controls and Spectrogram mode replacement receive protection immediately,
  including after track/capability changes. Existing infrastructure checks preserve
  both live Spectrogram modes and unrelated input.

The decoder-free Wavebar fixture has no waveform data, so its mouse preview is
verified by handler audit plus isolated protocol fixtures and actual-host event
ordering; the host test does not claim a decoded-waveform drag or pixel comparison.
The same release/cancellation protocol is explicit in its audited handlers.
The user subsequently reported Wavebar mouse clicking, scrolling and keyboard seeking
prevented while already locked, with a rendered waveform on fooyin 0.14.0, latest
Session Lock `master`, Arch Linux/Wayland and Qt 6.12.0. This manual sign-off completes
the outstanding Wavebar check in 011. Mid-preview locking/unlocking is not required.

### Ticket 008 verification — 2026-10-23

Using the existing Debug configuration, installed public SDK and pinned matching
host described in the [build and installation guide](BUILDING.md), from the plugin
repository:

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

- GCC 14.2.0 / C++23, CMake 3.31.6 / Ninja 1.12.1, Qt 6.8.2, Debian 13 x86_64,
  Qt `offscreen`, Python 3.13.5. Build succeeded without compiler warnings and used
  installed public fooyin headers only.
- **8/8 CTest entries passed**, zero failures. New `lock.seek-input`: **4 passed**,
  including setup/cleanup; no failed or skipped checks. Existing action, playlist,
  playback, lifecycle, coordinator and discovery suites passed too.
- Installed-artifact smoke test passed with matching optional Wavebar/Spectrogram
  modules, including clean locked shutdown, hard interruption and restart recovery.
  The production module remains the only installed artifact. Required marker:

```text
Session Lock smoke: production seeking, preview cancellation, progress, track capability, and recreation verified
```

The initial hover probe exposed Qt's no-button-move gating on non-mouse-tracking
sliders. The fixture now temporarily enables mouse tracking for that receiver
assertion and restores it. No production workaround was needed.
