# fooyin Session Lock plugin

A plugin for preventing accidental playback, seeking, playlist, and queue changes
while keeping fooyin's playlist and library browsing usable.

**Current status: actions, playlists, playback/queue gestures, seeking, and safe browsing.** The session toggle suppresses
audited transport, seek, playback-order, queue and playlist-edit commands, including
their registered shortcuts and underlying actions. Unlock restores current
host-controlled action state. See the [command inventory](docs/registered-actions.md).
Regular playlists are temporarily content-locked with durable original-state
recovery. Targeted guards cover playlist deletion/unlock, rename, tab buttons,
sorting and drops; see the [playlist protection audit](docs/playlist-protection.md).
Shared [widget discovery and input rules](docs/widget-input.md) cover queue,
library/filter/search and seek surfaces, including Wavebar and Spectrogram.
Direct [playback and queue protection](docs/playback-protection.md) blocks
playlist/library/filter/queue activation, direct playback menu commands and
mutation drops while preserving selection, scrolling and library-tree expansion.
Natural queue consumption continues in both modes. [Seeking protection](docs/seeking-protection.md)
blocks built-in Seekbar, Wavebar and full-track Spectrogram seek input and cancels
active previews at lock entry while preserving progress and track capability.
[Browsing protection](docs/browsing-protection.md) suppresses cursor-follow scheduling
and selection/search-generated playlist writes while retaining selection, live
library/filter searching and viewed-playlist switching. Temporary settings and
saved layout overrides have interrupted-session recovery.
[Coordinated transitions](docs/transitions.md) retain input/action protection through
restoration and recover safely after repeated interruption. The
[tested compatibility matrix](docs/compatibility.md) records assembled automated
checks, native X11 Global Hotkeys dispatch and host disable/re-enable across
restarts. Native desktop drag/grab behavior and decoded Wavebar mouse previews
still need sign-off in [ticket 011](.plan/011-validate-supported-interactions.md);
this development version is not yet a complete session lock.
The approved scope is recorded in the
[behavior contract](.plan/001-decide-lock-policy.md#approved-behavior-contract).

## Requirements

- Linux with a C++23 compiler and CMake 3.19 or newer; Ninja is used below.
- Qt 6.4 or newer: Core, Widgets, Sql, Concurrent, and Network development modules.
  Qt Test is additionally required when `BUILD_TESTING=ON` (the default).
- An **installed fooyin SDK and matching runtime**, including `FooyinConfig.cmake`,
  public headers, and Core/Gui/Utils shared libraries. Configure fooyin with
  `INSTALL_HEADERS=ON` to produce the SDK. The plugin does not fall back to source
  or private headers.
- Python 3.8 or newer for the optional real-host smoke test, plus fooyin's Qt SQLite
  driver and other runtime dependencies.

The initial target is fooyin commit
`bb8d9f0371b254571fffec310dce3ee1ce005c87` (0.13.1). Use the same Qt/compiler ABI
for the host and plugin. See [verification evidence](docs/verification.md) for the
actual toolchain and commands used; a release compatibility range is not yet
established. See the [tested widget/plugin matrix and limitations](docs/compatibility.md)
before relying on a particular integration.

## Build and install

With the SDK installed in `/path/to/fooyin-sdk`, from this repository:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/fooyin-sdk \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

The standard Linux install produces
`$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so`, which fooyin discovers on its
next startup. The plugin is listed as **Session Lock** in fooyin's plugin settings
(identifier `fooyin-lock-plugin.sessionlock`).

`LOCK_PLUGIN_INSTALL_DIR` overrides the prefix-relative plugin destination if
needed. The test driver and test executables are never installed. Build-only
deployments can use `FOOYIN_PLUGIN_PATH=/absolute/path/to/build` when launching the
matching fooyin runtime. For staged installs, point it at the staged plugin
directory instead. The installed module uses the host's library layout and keeps
external SDK library search paths where needed.

To build only the production plugin, configure with `-DBUILD_TESTING=OFF`.

## Session control

Open **Playback → Session Lock** to toggle the runtime session state. The menu text
shows **Unlocked**, transition/recovery status, or **Locked**; only a successfully
completed lock transition is checked. Activate the same action to unlock. Assign
a shortcut in fooyin's shortcut settings if desired; there is no default binding.

The session starts unlocked after startup recovery. The playlist guard uses the durable
`session-lock-recovery.json` journal in fooyin's state directory to restore their
temporary persistent changes after interruption. If recovery is unresolved, the
menu shows **Recovery required — Retry** with details in its status/tooltip text
and the application log. Activating it retries recovery before allowing a new
lock. Pre-existing playlist locks survive unlock and clean shutdown. Playlist
switcher context menus are suppressed while locked because they contain direct
mutation commands; selection, scrolling and switching remain available. Autoplaylist
regeneration continues. Cursor-follow and per-widget result-generation settings
restore on unlock, clean shutdown and interrupted-session recovery. Existing
scheduled playback is retained. Unconnected search bars keep text editing available
while their result-playlist generation is suspended; embedded/connected searches
continue updating their own views.

## Real-host smoke test

Enable the additional CTest entry by configuring with a matching host executable:

```sh
cmake -S . -B build \
  -DLOCK_FOOYIN_EXECUTABLE=/path/to/fooyin-sdk/bin/fooyin
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The smoke runner creates a disposable HOME/XDG profile, uses Qt's offscreen
platform, loads the actual plugin and a separate test driver, reaches the host
event loop, waits for playlists to populate, and exits through normal application
shutdown while locked. It verifies core/GUI initialisation, registered-proxy
lock/unlock/relock transitions, protected proxy/source actions, host refreshes,
late registration, current-state restoration, concrete playlist locks, targeted
parent/viewport input filtering, and late/replaced widgets. It then hard-exits a
second host while locked, interrupts a third during startup recovery, and restarts
the same profile to verify repeatable restoration and journal acknowledgement.
The browsing checks also exercise populated library/filter models, colliding result
names, live search, search modes and Quick Search show-all, and saved-layout recovery.
Three further processes exercise the actual Plugins settings page: disable while
locked, restart disabled with original playlist states restored, and re-enable for
another unlocked startup and successful lock/unlock. The optional
[X11 run](docs/compatibility.md#reproduce-the-automated-verification) adds native
Global Hotkeys delivery with `--platform xcb --global-hotkeys /path/to/module.so`.
The default offscreen run needs no audio device or display server.
The lifecycle test separately exercises actual `QPluginLoader`
unload with Qt's default unload-prevention hint cleared.

To verify the **installed** artifact explicitly:

```sh
python3 tests/host-smoke.py \
  --host /path/to/fooyin-sdk/bin/fooyin \
  --plugin "$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so" \
  --driver build/tests/hostdriver/fyplugin_locksmokedriver.so
```

Debug lifecycle messages are available with
`QT_LOGGING_RULES='fy.sessionlock.debug=true'`.

## Development

- [Implementation tickets](.plan/INDEX.md) and [research findings](FINDINGS.md)
- [Runtime ownership and test extension points](docs/architecture.md)
- [Protected commands and remaining direct-call routes](docs/registered-actions.md)
- [Playlist locks, GUI routes and durable recovery](docs/playlist-protection.md)
- [Widget identification, input rules and dynamic discovery](docs/widget-input.md)
- [Playback gestures, direct menu actions and queue drops](docs/playback-protection.md)
- [Seeking controls, preview cancellation and supported matrix](docs/seeking-protection.md)
- [Safe browsing, generated playlists, search modes and settings recovery](docs/browsing-protection.md)
- [Transition ordering, in-flight work, repeated recovery and teardown](docs/transitions.md)
- [Build and runtime verification evidence](docs/verification.md)
- [Tested compatibility matrix, boundaries and remaining manual sign-off](docs/compatibility.md)

Keep project includes restricted to fooyin's installed public headers. Add focused
coordinator/action/filter tests to `tests/` as the corresponding tickets land.
The intended protection boundary is ordinary interactions on audited GUI routes;
CLI, MPRIS, and arbitrary direct controller calls remain outside that boundary.

Source files are licensed under
[GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html), as indicated by their
SPDX headers.
