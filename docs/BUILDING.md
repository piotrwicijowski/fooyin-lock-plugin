# Building and installing Session Lock

[Back to the README](../README.md)

For release ZIP installation through fooyin's settings dialog, follow the
[README installation steps](../README.md#installation). This guide covers source
builds, manual installation, and testing.

## Requirements

- Linux with a C++23 compiler and CMake 3.19 or newer.
- Ninja for the commands below.
- Qt 6.4 or newer: Core, Widgets, Sql, Concurrent, and Network development modules.
  Qt Test is additionally required when `BUILD_TESTING=ON` (the default).
- An **installed fooyin SDK and matching runtime**, including `FooyinConfig.cmake`,
  public headers, and Core/Gui/Utils shared libraries. Configure fooyin with
  `INSTALL_HEADERS=ON` to produce the SDK.
- Python 3.8 or newer for the optional real-host smoke test, plus fooyin's Qt SQLite
  driver and other runtime dependencies.

### fooyin compatibility

The automated test baseline is fooyin **0.13.1**, commit
`bb8d9f0371b254571fffec310dce3ee1ce005c87`. Use the same Qt/compiler ABI for the host
and plugin. A release compatibility range is not yet established.

See [tested environments and known limitations](compatibility.md) before relying
on a particular integration. A successful build against another fooyin version
does not establish interaction compatibility.

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
next startup. The plugin is listed as **Session Lock** in **Settings → Plugins**.
Restart the matching fooyin runtime after installing.

- `CMAKE_INSTALL_PREFIX` chooses the install prefix.
- `CMAKE_INSTALL_LIBDIR` defaults to the platform's library directory; the example
  sets it to `lib` for a predictable user-local path.
- `LOCK_PLUGIN_INSTALL_DIR` overrides the prefix-relative plugin destination if
  needed.
- `FOOYIN_PLUGIN_PATH=/absolute/path/to/build` lets the matching runtime discover
  a build without installing it. For staged installs, point it at the staged
  plugin directory instead.

The SDK/runtime used at launch must remain available and match the build.

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

### Verify the installed artifact

```sh
python3 tests/host-smoke.py \
  --host /path/to/fooyin-sdk/bin/fooyin \
  --plugin "$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so" \
  --driver build/tests/hostdriver/fyplugin_locksmokedriver.so
```

### Optional widgets and native X11 hotkeys

Build Wavebar, Spectrogram, and Global Hotkeys from the same fooyin checkout and
build configuration as the SDK/runtime:

```sh
cmake -S /path/to/fooyin -B /path/to/fooyin-build \
  -DBUILD_PLUGINS=ON -DPLUGIN_SELECTION=wavebar,spectrogram,globalhotkeys
cmake --build /path/to/fooyin-build --target wavebar spectrogram globalhotkeys --parallel 4
```

For an offscreen optional-widget run, add both Wavebar and Spectrogram paths to
the installed-artifact command using repeated `--optional-plugin` arguments.
To also test native Global Hotkeys delivery, install Xvfb, XTest, and the
`libX11.so.6`/`libXtst.so.6` runtime libraries, then run:

```sh
xvfb-run -a python3 tests/host-smoke.py \
  --host /path/to/fooyin-sdk/bin/fooyin \
  --plugin "$HOME/.local/lib/fooyin/plugins/fyplugin_lock.so" \
  --driver build/tests/hostdriver/fyplugin_locksmokedriver.so \
  --platform xcb \
  --global-hotkeys /path/to/fooyin-build/run/lib/fooyin/plugins/fyplugin_globalhotkeys.so \
  --optional-plugin /path/to/fooyin-build/run/lib/fooyin/plugins/fyplugin_wavebar.so \
  --optional-plugin /path/to/fooyin-build/run/lib/fooyin/plugins/fyplugin_spectrogram.so
```

Use a disposable X11 display: this check temporarily grabs the Media Play and
Media Stop keys. It does not verify Wayland/portal hotkey delivery.

## Troubleshooting

Launch fooyin with `QT_LOGGING_RULES='fy.sessionlock.debug=true'` for debug
lifecycle messages.

For interrupted-session recovery, see the
[README recovery instructions](../README.md#recovery-after-an-unexpected-exit).
