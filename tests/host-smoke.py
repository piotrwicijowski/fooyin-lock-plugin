#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real fooyin host with an isolated profile and check plugin teardown."""
import argparse
import json
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
    parser.add_argument("--optional-plugin", action="append", default=[], type=Path,
                        help="Load matching wavebar/spectrogram modules (supply both to verify optional widgets)")
    args = parser.parse_args()
    for path in (args.host, args.plugin, args.driver, *args.optional_plugin):
        if not path.is_file():
            parser.error(f"File does not exist: {path}")

    with tempfile.TemporaryDirectory(prefix="fooyin-lock-smoke-") as directory:
        home = Path(directory)
        plugins = home / "plugins"
        plugins.mkdir()
        # Only the requested modules are added to the host's search path.
        for module in (args.plugin, args.driver, *args.optional_plugin):
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
            "FOOYIN_LOCK_SMOKE_OPTIONAL_WIDGETS": "1" if args.optional_plugin else "0",
        })
        full_sequence = (
            "fy.sessionlock: Core services acquired",
            "fy.sessionlock: GUI services acquired",
            "Session Lock smoke: host event loop reached",
            "Session Lock smoke: queue/library/filter/search/seek discovery, rules, ordering, and teardown verified",
            "Session Lock smoke: production playback gestures, direct menu actions, queue drops, and browsing verified",
            "Session Lock smoke: protected proxies/sources, refresh, late registration, and host-state restoration verified",
            "Session Lock smoke: playlist locks, parent/viewport input ordering, late/replaced widgets, and restoration verified",
            "Session Lock smoke: registered toggle locked, unlocked, and relocked for shutdown",
            "fy.sessionlock: Shutdown complete",
        )
        for phase in ("normal", "interrupt", "recover"):
            env["FOOYIN_LOCK_SMOKE_PHASE"] = phase
            try:
                result = subprocess.run([str(args.host.resolve()), "--skip-single"],
                                        env=env, cwd=home, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, timeout=30)
            except subprocess.TimeoutExpired as error:
                print(error.stdout.decode(errors="replace") if error.stdout else "")
                raise SystemExit(f"FAIL: {phase} host did not finish within 30 seconds")
            print(result.stdout, end="")
            if result.returncode:
                raise SystemExit(f"FAIL: {phase} host exited with {result.returncode}")
            if "QSQLITE driver not loaded" in result.stdout:
                raise SystemExit("FAIL: the real host's Qt SQLite runtime dependency is missing")
            if "fy.db: Failed to execute" in result.stdout:
                raise SystemExit(f"FAIL: {phase} host reported a database statement failure")
            if phase == "interrupt":
                expected = (*full_sequence[:-1], "Session Lock smoke: interrupting without plugin or host cleanup")
                if full_sequence[-1] in result.stdout:
                    raise SystemExit("FAIL: interrupted host unexpectedly ran plugin cleanup")
            elif phase == "recover":
                expected = (*full_sequence[:3],
                            "Session Lock smoke: interrupted playlist locks recovered after restart", full_sequence[-1])
            else:
                expected = full_sequence
            if args.optional_plugin and phase != "recover":
                optional_marker = "Session Lock smoke: optional wavebar/spectrogram discovery and input roles verified"
                if result.stdout.count(optional_marker) != 1:
                    raise SystemExit("FAIL: optional widget infrastructure was not verified")
            for message in expected:
                if result.stdout.count(message) != 1:
                    raise SystemExit(f"FAIL: expected exactly one occurrence of {message!r}")
            positions = [result.stdout.index(message) for message in expected]
            if positions != sorted(positions):
                raise SystemExit("FAIL: plugin lifecycle messages were out of order")
            journals = list((home / "state").rglob("session-lock-recovery.json"))
            if len(journals) != 1:
                raise SystemExit("FAIL: expected one persistent recovery journal")
            records = json.loads(journals[0].read_text())["records"]
            if bool(records) != (phase == "interrupt"):
                raise SystemExit(f"FAIL: incorrect outstanding recovery records after {phase}")
            if any(record["kind"] != "playlist-lock" or record["original"] != {"type": "bool", "value": False}
                   or record["temporary"] != {"type": "bool", "value": True} for record in records):
                raise SystemExit("FAIL: interrupted journal did not preserve typed playlist originals")
        print("PASS: real host playlist protection, clean shutdown, interruption, and restart recovery verified")


if __name__ == "__main__":
    main()
