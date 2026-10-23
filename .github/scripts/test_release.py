"""Check version validation, distribution packaging, and GitHub CLI publishing."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from release import distribution_target, package, publish, validate_version


class VersionValidationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        (self.source / "CMakeLists.txt").write_text("project(FooyinLock VERSION 0.1.0 LANGUAGES CXX)\n")

    def test_stable_prerelease_and_build_metadata(self):
        for tag in ("v0.1.0", "v0.1.0-rc.1", "v0.1.0-0", "v0.1.0-alpha-beta",
                    "v0.1.0+build.01", "v0.1.0-rc.1+build.123"):
            with self.subTest(tag=tag):
                self.assertEqual(validate_version(self.source, f"refs/tags/{tag}"), (tag, "0.1.0"))

    def test_rejects_ambiguous_or_malformed_versions(self):
        for tag in ("0.1.0", "v0.1", "v00.1.0", "v0.01.0", "v0.1.00", "v0.1.0-01",
                    "v0.1.0-rc.01", "v0.1.0-", "v0.1.0+", "v0.1.0+build..1",
                    "v0.1.0/extra", "v0.1.0\n", "v0.1.0;touch-file"):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "Invalid version tag"):
                validate_version(self.source, f"refs/tags/{tag}")

    def test_manual_dispatch_requires_a_tag(self):
        for ref in ("refs/heads/main", "refs/heads/v0.1.0", "v0.1.0"):
            with self.subTest(ref=ref), self.assertRaisesRegex(ValueError, "existing version tag"):
                validate_version(self.source, ref)

    def test_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, "CMakeLists.txt declares 0.1.0"):
            validate_version(self.source, "refs/tags/v0.2.0-rc.1")

    def test_cmake_comments_and_multiline_project(self):
        (self.source / "CMakeLists.txt").write_text(
            "# project(FooyinLock VERSION 9.9.9)\n"
            "project(\n  FooyinLock\n  VERSION 0.1.0\n  LANGUAGES CXX\n)\n"
        )
        self.assertEqual(validate_version(self.source, "refs/tags/v0.1.0"), ("v0.1.0", "0.1.0"))

    def test_missing_project_version(self):
        (self.source / "CMakeLists.txt").write_text("project(FooyinLock LANGUAGES CXX)\n")
        with self.assertRaisesRegex(ValueError, "Cannot read"):
            validate_version(self.source, "refs/tags/v0.1.0")


class DistributionPackagingTest(unittest.TestCase):
    distributions = (
        ({"ID": "ubuntu", "VERSION_ID": "24.04", "PRETTY_NAME": "Ubuntu 24.04 LTS"}, "ubuntu24.04"),
        ({"ID": "ubuntu", "VERSION_ID": "26.04", "PRETTY_NAME": "Ubuntu 26.04 LTS"}, "ubuntu26.04"),
        ({"ID": "arch", "PRETTY_NAME": "Arch Linux"}, "archlinux"),
    )

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "CMakeLists.txt").write_text("project(FooyinLock VERSION 0.1.0)\n")
        (self.root / "README.md").write_text("Session Lock documentation\n")
        self.build = self.root / "build"
        self.build.mkdir()
        self.args = argparse.Namespace(
            source=self.root, build=self.build, fooyin_source=self.root, fooyin_build=self.build,
            fooyin_prefix=self.root / "sdk", tag="v0.1.0", output=self.root / "dist",
        )
        self.cache = {
            "CMAKE_PROJECT_VERSION": "0.1.0", "CMAKE_BUILD_TYPE": "Release",
            "CMAKE_CXX_COMPILER": "/usr/bin/g++",
        }
        self.enterContext(patch("release.cache_value", side_effect=lambda build, name: self.cache[name]))
        self.runner = self.enterContext(patch("release.run", side_effect=self.fake_run))
        self.commands = self.enterContext(patch("release.subprocess.run", side_effect=self.fake_subprocess))
        self.enterContext(patch("release.platform.machine", return_value="x86_64"))
        self.enterContext(patch("release.platform.freedesktop_os_release", return_value=self.distributions[0][0]))
        self.outputs = self.enterContext(patch("release.outputs"))

    def fake_run(self, *command, **kwargs):
        if command[0] == "git":
            return "bb8d9f0371b254571fffec310dce3ee1ce005c87"
        if command[0] == "ldd":
            self.assertEqual(kwargs["env"]["LD_LIBRARY_PATH"], str(self.args.fooyin_prefix / "lib/fooyin"))
            return "libQt6Core.so.6 => /usr/lib/libQt6Core.so.6"
        if command[0] == "readelf":
            return "(RUNPATH) Library runpath: [$ORIGIN/..]"
        return "test version"

    def fake_subprocess(self, command, **kwargs):
        if command[0] == "cmake":
            stage = Path(command[command.index("--prefix") + 1])
            plugin = stage / "lib/fooyin/plugins/fyplugin_lock.so"
            plugin.parent.mkdir(parents=True)
            plugin.write_bytes(b"test plugin")
        else:
            if command[0] == sys.executable:
                self.assertEqual(command[1], str(self.root / "tests/host-smoke.py"))
                self.assertEqual(command[command.index("--host") + 1], str(self.args.fooyin_prefix / "bin/fooyin"))
                self.assertEqual(command[command.index("--driver") + 1],
                                 str(self.build / "tests/hostdriver/fyplugin_locksmokedriver.so"))
                plugin = Path(command[command.index("--plugin") + 1])
            else:
                self.assertEqual(command[0], str(self.build / "tests/lock_lifecycle_test"))
                plugin = Path(command[1])
            self.assertEqual(plugin.read_bytes(), b"test plugin")
            self.assertIn("extracted", plugin.parts)
            self.assertEqual(kwargs["env"]["QT_QPA_PLATFORM"], "offscreen")
            self.assertTrue(kwargs["check"])

    def test_rejects_unsupported_distribution_or_architecture(self):
        for os_release, architecture in (
            ({"ID": "ubuntu", "VERSION_ID": "22.04"}, "x86_64"),
            ({"ID": "ubuntu"}, "x86_64"),
            ({"ID": "debian", "VERSION_ID": "13"}, "x86_64"),
            ({"ID": "arch"}, "aarch64"),
        ):
            with self.subTest(os_release=os_release, architecture=architecture):
                with self.assertRaisesRegex(ValueError, "Package targets require"):
                    distribution_target(os_release, architecture)

    def test_packages_distribution_specific_archives_and_checks_payload(self):
        for os_release, target in self.distributions:
            with self.subTest(target=target), \
                    patch("release.platform.freedesktop_os_release", return_value=os_release):
                package(self.args)
                metadata = json.loads((self.args.output / "build-metadata.json").read_text())
                self.assertEqual(metadata["distribution"], target)
                self.assertEqual(metadata["os"], os_release["PRETTY_NAME"])
                self.assertIn(f"-linux-x86_64-{target}-fooyin-", metadata["artifact"])
                archive = self.args.output / f"{metadata['artifact']}.zip"
                checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
                self.assertEqual(
                    Path(str(archive) + ".sha256").read_text(), f"{checksum}  {archive.name}\n"
                )
                with zipfile.ZipFile(archive) as zipped:
                    self.assertEqual(set(zipped.namelist()), {
                        "linux-x64/fyplugin_lock.so", "README.md", "INSTALL.txt",
                        "build-metadata.json", "dependencies.txt", "elf-dynamic.txt",
                    })
                    self.assertIsNone(zipped.testzip())
                    self.assertEqual(zipped.read("linux-x64/fyplugin_lock.so"), b"test plugin")
                    self.assertEqual(json.loads(zipped.read("build-metadata.json")), metadata)
                    notes = zipped.read("INSTALL.txt").decode()
                self.assertIn(f"Built for {os_release['PRETTY_NAME']} x86-64", notes)
                self.assertIn("Settings > Plugins > Install", notes)
                self.assertIn(f"{metadata['artifact']}.zip.sha256", notes)
        self.assertEqual(self.commands.call_count, 9)  # Install, lifecycle, host smoke per target.

    def test_rejects_wrong_version_and_non_release_builds(self):
        for name, value, message in (
            ("CMAKE_PROJECT_VERSION", "0.2.0", "Configured plugin version"),
            ("CMAKE_BUILD_TYPE", "Debug", "CMAKE_BUILD_TYPE=Release"),
        ):
            with self.subTest(name=name), patch.dict(self.cache, {name: value}):
                with self.assertRaisesRegex(ValueError, message):
                    package(self.args)
        self.commands.assert_not_called()

    def test_rejects_unresolved_dependencies_and_workspace_rpaths(self):
        for command_name, output, message in (
            ("ldd", "libfooyin_core.so => not found", "Unresolved plugin dependencies"),
            ("readelf", f"(RUNPATH) Library runpath: [{self.args.fooyin_prefix}/lib/fooyin]",
             "leaks a workspace library path"),
        ):
            with self.subTest(command=command_name):
                self.runner.side_effect = lambda *cmd, **kw: (
                    output if cmd[0] == command_name else self.fake_run(*cmd, **kw)
                )
                with self.assertRaisesRegex(ValueError, message):
                    package(self.args)
        self.outputs.assert_not_called()

    def test_failed_payload_check_does_not_produce_release_manifest(self):
        for failing_command in (str(self.build / "tests/lock_lifecycle_test"), sys.executable):
            with self.subTest(command=failing_command):
                def fail_check(command, **kwargs):
                    if command[0] == failing_command:
                        raise subprocess.CalledProcessError(1, command)
                    return self.fake_subprocess(command, **kwargs)

                self.commands.side_effect = fail_check
                with self.assertRaises(subprocess.CalledProcessError):
                    package(self.args)
                self.assertFalse((self.args.output / "build-metadata.json").exists())
                self.assertEqual(list(self.args.output.glob("*.sha256")), [])
        self.outputs.assert_not_called()


class ReleasePublishingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        (self.directory / "CMakeLists.txt").write_text("project(FooyinLock VERSION 0.1.0)\n")
        self.args = argparse.Namespace(
            source=self.directory, output=self.directory / "dist", repo="example/lock", tag="v0.1.0"
        )
        self.assets = []
        for target in ("ubuntu24.04", "ubuntu26.04", "archlinux"):
            directory = self.args.output / f"release-linux-{target}"
            directory.mkdir(parents=True)
            artifact = f"lock-{target}"
            (directory / "build-metadata.json").write_text(
                json.dumps({"tag": self.args.tag, "artifact": artifact})
            )
            for suffix in (".zip", ".zip.sha256"):
                asset = directory / f"{artifact}{suffix}"
                asset.write_bytes(b"test asset")
                self.assets.append(asset)
        self.gh = self.enterContext(patch("release.subprocess.run"))
        self.gh.return_value = subprocess.CompletedProcess([], 1)
        self.url = self.enterContext(patch("release.run", return_value="https://example.com/release"))
        self.outputs = self.enterContext(patch("release.outputs"))

    def test_publishes_all_distribution_assets_in_one_release(self):
        publish(self.args)
        self.assertEqual(self.gh.call_count, 2)
        command = self.gh.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "create", "v0.1.0"])
        for asset in self.assets:
            self.assertEqual(command.count(str(asset)), 1)
        self.assertIn("--verify-tag", command)
        self.assertIn("--generate-notes", command)
        self.assertEqual(command[command.index("--repo") + 1], "example/lock")
        self.assertNotIn("--prerelease", command)
        self.outputs.assert_called_once_with(url="https://example.com/release")

    def test_classifies_prerelease_without_confusing_build_metadata(self):
        for tag, prerelease in (("v0.1.0-rc.1+build.2", True), ("v0.1.0+build-with-hyphens", False)):
            with self.subTest(tag=tag):
                self.args.tag = tag
                for manifest in self.args.output.rglob("build-metadata.json"):
                    metadata = json.loads(manifest.read_text())
                    metadata["tag"] = tag
                    manifest.write_text(json.dumps(metadata))
                publish(self.args)
                self.assertEqual("--prerelease" in self.gh.call_args.args[0], prerelease)

    def test_rerun_updates_existing_release_assets(self):
        self.gh.return_value = subprocess.CompletedProcess([], 0)
        publish(self.args)
        command = self.gh.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "upload", "v0.1.0"])
        self.assertIn("--clobber", command)

    def test_missing_assets_or_wrong_tag_fail_before_publishing(self):
        self.args.tag = "v0.1.0-rc.1"
        with self.assertRaisesRegex(ValueError, "does not match"):
            publish(self.args)
        self.args.tag = "v0.1.0"
        self.assets[-1].unlink()
        with self.assertRaisesRegex(ValueError, "missing or empty"):
            publish(self.args)
        self.gh.assert_not_called()

    def test_rejects_empty_output_before_publishing(self):
        self.args.output = self.directory / "empty"
        with self.assertRaisesRegex(ValueError, "No packaged builds"):
            publish(self.args)
        self.gh.assert_not_called()

    def test_failed_publish_does_not_report_a_release_url(self):
        self.gh.side_effect = [
            subprocess.CompletedProcess([], 1),
            subprocess.CalledProcessError(1, ["gh", "release", "create"]),
        ]
        with self.assertRaises(subprocess.CalledProcessError):
            publish(self.args)
        self.url.assert_not_called()
        self.outputs.assert_not_called()


if __name__ == "__main__":
    unittest.main()
