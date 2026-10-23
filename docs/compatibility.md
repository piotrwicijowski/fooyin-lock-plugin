# Compatibility and known limitations

[Back to the README](../README.md)

Session Lock prevents accidental interaction through supported fooyin controls.
For everyday use and features, see the [README](../README.md#features); for build
and test commands, see [Building and installing](BUILDING.md).

## Tested environments

The following results were recorded on **2026-10-23**. A release compatibility
range has not yet been established.

### Automated testing

| Component | Tested setup |
| --- | --- |
| fooyin / SDK | 0.13.1, commit `bb8d9f0371b254571fffec310dce3ee1ce005c87`; Release host |
| Session Lock | Debug build, including the installed production module |
| Platform / compiler | Debian 13 x86_64; GCC 14.2.0 |
| Qt | 6.8.2 |
| GUI backends | Qt `offscreen`; X11 `xcb` under Xvfb |
| Optional fooyin plugins | Matching Wavebar, Spectrogram, and Global Hotkeys modules |

Automated checks passed for protected controls, browsing, normal shutdown,
interrupted-session recovery, and plugin disable/re-enable across restarts.
Native X11 Global Hotkeys delivery was also verified. These checks did not use
an audio device or verify audible playback. Most interaction checks delivered
synthetic events; native desktop drag/drop and a rendered Wavebar were checked
separately below.

### Manual desktop testing

User-reported passes on fooyin **0.14.0**, **Arch Linux/Wayland**, **Qt 6.12.0**,
with Session Lock built from the latest `master` at test time:

- **Native drag/drop while locked:** internal dragging could not start; drops
  from a file browser showed a prohibited cursor and made no edits.
- **Wavebar with a rendered waveform:** mouse clicking, scrolling, and keyboard
  seeking were prevented while locked.

The exact Session Lock commit, compositor, and Qt platform plugin were not
recorded. These results cover the reported interactions, rather than the full
automated matrix on fooyin 0.14.0. Wayland/portal global hotkeys and other
third-party input plugins, including Vim Motions, have not been verified.

## Protection boundaries

- Wait for **Locked** before starting an interaction. Finish or cancel existing
  gestures first. Supported actions and gestures are protected when started after
  **Locked** is shown and while the lock remains active. Pre-lock gestures and
  already-submitted requests may still complete.
- CLI, MPRIS, remote-control apps, and plugins that call playback controllers
  directly can bypass the lock. Media-key coverage depends on its delivery route:
  registered fooyin shortcuts and the tested X11 Global Hotkeys route are covered.
- Direct Apply/Reset changes to queue mode and stop-after settings in the
  Playback/Playback Queue settings pages are outside the protected controls.
- Background library changes, automatic playlist regeneration, existing scheduled
  tracks, and natural queue consumption continue.
- Disabling or re-enabling the plugin takes effect on restart. If fooyin exits
  unexpectedly after the plugin is disabled, recovery must wait until it is
  enabled again.
- Other fooyin versions, widgets, and third-party integrations need interaction
  testing before compatibility is claimed. Matching the SDK/runtime ABI and
  compiling successfully are not sufficient.
