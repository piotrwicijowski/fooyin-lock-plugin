#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real fooyin host with an isolated profile and check plugin teardown."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--driver", required=True, type=Path)
    args = parser.parse_args()
    for path in (args.host, args.plugin, args.driver):
        if not path.is_file():
            parser.error(f"File does not exist: {path}")

    with tempfile.TemporaryDirectory(prefix="fooyin-lock-smoke-") as directory:
        home = Path(directory)
        plugins = home / "plugins"
        plugins.mkdir()
        # Only the requested modules are added to the host's search path.
        for module in (args.plugin, args.driver):
            shutil.copy2(module, plugins / module.name)
        runtime = home / "runtime"
        runtime.mkdir(mode=0o700)
        env = os.environ.copy()
        env.update({
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_DATA_HOME": str(home / "data"),
            "XDG_STATE_HOME": str(home / "state"),
            "XDG_CACHE_HOME": str(home / "cache"),
            "XDG_RUNTIME_DIR": str(runtime),
            "QT_QPA_PLATFORM": "offscreen",
            "QT_LOGGING_RULES": "fy.sessionlock.debug=true",
            "QT_MESSAGE_PATTERN": "%{category}: %{message}",
            "FOOYIN_PLUGIN_PATH": str(plugins),
            "FOOYIN_LOCK_SMOKE_TEST": "1",
        })
        try:
            result = subprocess.run([str(args.host.resolve()), "--skip-single"],
                                    env=env, cwd=home, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            print(error.stdout.decode(errors="replace") if error.stdout else "")
            raise SystemExit("FAIL: host did not complete clean shutdown within 30 seconds")
        print(result.stdout, end="")
        expected = (
            "fy.sessionlock: Core services acquired",
            "fy.sessionlock: GUI services acquired",
            "Session Lock smoke: host event loop reached",
            "Session Lock smoke: registered toggle locked, unlocked, and relocked for shutdown",
            "fy.sessionlock: Shutdown complete",
        )
        if result.returncode:
            raise SystemExit(f"FAIL: host exited with {result.returncode}")
        if "QSQLITE driver not loaded" in result.stdout:
            raise SystemExit("FAIL: the real host's Qt SQLite runtime dependency is missing")
        for message in expected:
            if result.stdout.count(message) != 1:
                raise SystemExit(f"FAIL: expected exactly one occurrence of {message!r}")
        positions = [result.stdout.index(message) for message in expected]
        if positions != sorted(positions):
            raise SystemExit("FAIL: plugin lifecycle messages were out of order")
        print("PASS: real host initialised Session Lock, entered its event loop, and shut down cleanly")


if __name__ == "__main__":
    main()
