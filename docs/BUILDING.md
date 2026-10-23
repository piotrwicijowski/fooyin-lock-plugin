# Building and installing Session Lock

[Back to the README](../README.md)

For release ZIP installation through fooyin's settings dialog, follow the
[README installation steps](../README.md#installation). This guide covers source
builds, manual installation, testing, and developer references.

## Requirements

- Linux with a C++23 compiler and CMake 3.19 or newer.
- Ninja for the commands below.
- Qt 6.4 or newer: Core, Widgets, Sql, Concurrent, and Network development modules.
  Qt Test is additionally required when `BUILD_TESTING=ON` (the default).
- An **installed fooyin SDK and matching runtime**, including `FooyinConfig.cmake`,
  public headers, and Core/Gui/Utils shared libraries. Configure fooyin with
  `INSTALL_HEADERS=ON` to produce the SDK. The plugin does not fall back to source
  or private headers.
- Python 3.8 or newer for the optional real-host smoke test, plus fooyin's Qt SQLite
  driver and other runtime dependencies.

### fooyin compatibility

The automated test baseline is fooyin **0.13.1**, commit
`bb8d9f0371b254571fffec310dce3ee1ce005c87`. Use the same Qt/compiler ABI for the host
and plugin. A release compatibility range is not yet established.

User-reported manual drag/drop and rendered Wavebar input checks also passed on
fooyin **0.14.0**, up-to-date Arch Linux/Wayland and Qt **6.12.0**, using Session
Lock built from latest `master` at test time. The full automated suite was not
reported as rerun on that setup.

See [verification evidence](verification.md) for the actual toolchain and commands
used, and the [tested widget/plugin matrix and limitations](compatibility.md)
before relying on a particular integration. Changing the host baseline requires
rechecking the runtime widget and action identification as well as compilation.

## Build the fooyin SDK

If you already have the matching installed SDK and runtime, skip to
[Build and install the plugin](#build-and-install-the-plugin).

Use a checkout of the fooyin baseline above, with its submodules initialized, and
install fooyin's own build dependencies. These include Qt/Svg, ICU, TagLib, zlib,
FFmpeg, and libarchive development packages; see fooyin's build documentation for
the full requirements.

```sh
cmake -S /path/to/fooyin -B /path/to/fooyin-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/fooyin-sdk \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DINSTALL_HEADERS=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF \
  -DBUILD_PLUGINS=OFF \
  -DBUILD_TRANSLATIONS=OFF
cmake --build /path/to/fooyin-build --parallel 4
cmake --install /path/to/fooyin-build
```

This installs the matching runtime alongside the SDK. `BUILD_PLUGINS=OFF` omits
fooyin's bundled plugins; enable and select any optional host plugins separately
if you need to test their integration.

## Build and install the plugin

With the SDK installed in `/path/to/fooyin-sdk`, run from this repository:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/fooyin-sdk \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

To build only the production plugin, configure with `-DBUILD_TESTING=OFF` and skip
the CTest command. Use `-DCMAKE_BUILD_TYPE=Release` for an optimized build.

### Installation paths

The commands above install
`$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so`, which fooyin discovers on its
next startup. The plugin is listed as **Session Lock** in **Settings → Plugins**
(identifier `fooyin-lock-plugin.sessionlock`). Restart the matching fooyin runtime
after installing.

- `CMAKE_INSTALL_PREFIX` chooses the install prefix.
- `CMAKE_INSTALL_LIBDIR` defaults to the platform's library directory; the example
  sets it to `lib` for a predictable user-local path.
- `LOCK_PLUGIN_INSTALL_DIR` overrides the prefix-relative plugin destination if
  needed.
- `FOOYIN_PLUGIN_PATH=/absolute/path/to/build` lets the matching runtime discover
  a build without installing it. For staged installs, point it at the staged
  plugin directory instead.

The test driver and test executables are never installed. The installed module
uses the host's library layout and keeps external SDK library search paths where
needed. The SDK/runtime used at launch must remain available and match the build.

## Real-host smoke test

Enable the additional CTest entry by configuring with a matching host executable:

```sh
cmake -S . -B build \
  -DLOCK_FOOYIN_EXECUTABLE=/path/to/fooyin-sdk/bin/fooyin
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

This assumes the build above was configured with `BUILD_TESTING=ON`. The smoke
runner creates a disposable HOME/XDG profile and uses Qt's offscreen platform.
The default run needs no audio device or display server, but does need Qt's
SQLite runtime driver.

It loads the actual plugin and a separate test driver, reaches the host event
loop, waits for playlists to populate, and exits through normal application
shutdown while locked. It checks:

- Core/GUI initialization and lock/unlock/relock transitions.
- Registered actions, direct protected actions, host refreshes, late registration,
  and current-state restoration.
- Playlist locks, parent/viewport input filtering, and late or replaced widgets.
- Library/filter models, colliding result names, live search, search modes, Quick
  Search show-all, and saved-layout recovery.
- Repeated interrupted-session recovery: hard exit while locked, another exit
  during startup recovery, then restoration and journal acknowledgement using the
  same profile.
- Disabling the plugin through the actual Plugins settings page while locked,
  restarting disabled with original playlist states restored, and re-enabling
  for an unlocked startup and successful lock/unlock.

The lifecycle test separately exercises actual `QPluginLoader` unload with Qt's
default unload-prevention hint cleared. The optional
[X11 run](compatibility.md#reproduce-the-automated-verification) adds native Global
Hotkeys delivery with `--platform xcb --global-hotkeys /path/to/module.so` and
matching optional host plugins.

### Verify the installed artifact

```sh
python3 tests/host-smoke.py \
  --host /path/to/fooyin-sdk/bin/fooyin \
  --plugin "$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so" \
  --driver build/tests/hostdriver/fyplugin_locksmokedriver.so
```

### Debug logging and recovery state

Launch fooyin with `QT_LOGGING_RULES='fy.sessionlock.debug=true'` for debug
lifecycle messages.

The durable `session-lock-recovery.json` journal in fooyin's state directory
records temporary persistent changes. Startup recovery restores original
playlist locks, cursor-follow and result-generation settings, and saved-layout
overrides. If recovery is unresolved, the menu shows **Recovery required — Retry**
with details in its status/tooltip text and the application log. Activating the
action retries recovery before allowing a new lock.

## Development references

- [Implementation tickets](../.plan/INDEX.md) and [research findings](../FINDINGS.md)
- [Approved behavior contract](../.plan/001-decide-lock-policy.md#approved-behavior-contract)
- [Runtime ownership and test extension points](architecture.md)
- [Protected commands and remaining direct-call routes](registered-actions.md)
- [Playlist locks, GUI routes and durable recovery](playlist-protection.md)
- [Widget identification, input rules and dynamic discovery](widget-input.md)
- [Playback gestures, direct menu actions and queue drops](playback-protection.md)
- [Seeking controls, preview cancellation and supported matrix](seeking-protection.md)
- [Safe browsing, generated playlists, search modes and settings recovery](browsing-protection.md)
- [Transition ordering, in-flight work, repeated recovery and teardown](transitions.md)
- [Build and runtime verification evidence](verification.md)
- [Tested compatibility matrix, boundaries and manual sign-off](compatibility.md)

Keep project includes restricted to fooyin's installed public headers. Add focused
coordinator/action/filter tests to `tests/` when extending protection. The intended
protection boundary is ordinary interactions on audited GUI routes; CLI, MPRIS,
and arbitrary direct controller calls remain outside that boundary.
