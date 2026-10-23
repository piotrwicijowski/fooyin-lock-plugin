# fooyin-lock-plugin

Session Lock for [fooyin](https://github.com/fooyin/fooyin). Prevent accidental
playback, seeking, playlist, and queue changes while you browse your music.

- [Installation](#installation)
- [Getting started](#getting-started)
- [Features](#features)
- [Recovery after an unexpected exit](#recovery-after-an-unexpected-exit)
- [Further documentation](#further-documentation)

## Installation

1. Download the Linux x86-64 **`.zip`** for **Ubuntu 24.04**, **Ubuntu 26.04**, or
   **Arch Linux** from the [release assets](../../releases). Choose the build
   matching your distribution and fooyin version.
2. In fooyin, open **Settings → Plugins → Install** and select the downloaded
   `.zip` file.
3. Restart fooyin when prompted. The plugin appears as **Session Lock** in
   **Settings → Plugins**.

For installation from source, see the [build and installation guide](docs/BUILDING.md).

## Getting started

1. Start playing the music and arrange your playlist or queue.
2. Open **Playback → Session Lock** to turn the lock on. Wait for the menu item to
   show **Locked** with a check mark.
3. Any accidental playback changes, seeking or playlist reordering is prevented.
4. Choose **Playback → Session Lock** again to unlock.

You can assign a keyboard shortcut to **Session Lock** in fooyin's shortcut
settings. The session starts unlocked on fooyin start.

## Features

| While locked | What happens |
|---|---|
| Playback controls | Play, pause, stop, next/previous track, and playback-order changes are blocked through supported fooyin controls and shortcuts |
| Seeking | Jumping to another position in a track is blocked in Seekbar, Wavebar, and full-track Spectrogram |
| Playlist and queue edits | Adding, removing, sorting, rearranging, renaming, and deleting items is blocked on supported views and menus |
| Browsing | Selecting tracks, scrolling, copying, searching, expanding library folders, and switching the viewed playlist remain available |
| Ongoing playback | Tracks advance normally and the existing queue continues to play |

Some editing menus are unavailable while locked. Search results still update in
their own views, but searches that normally create or replace a playlist won't
change that playlist until you unlock. Playlists you had already locked stay
locked after you turn Session Lock off.

The lock covers supported fooyin controls. Commands from outside fooyin, such as
remote-control apps, and other plugins that control playback directly can still
make changes. Background library updates, automatic playlists, and work already
started before locking can also continue.

## Recovery after an unexpected exit

Session Lock restores the playlist locks and settings it temporarily changed
when you unlock or close fooyin normally. If fooyin closes unexpectedly, the
plugin tries to restore them the next time it starts.

If the menu shows **Recovery required — Retry**, choose it to try again. Hover
over the menu item for details; further information is available in fooyin's
application log. A new lock can only start after recovery succeeds.

## Further documentation

- [Building and installing](docs/BUILDING.md) — requirements, source builds, installation paths, testing, and developer references.
- [Compatibility and known limitations](docs/compatibility.md) — tested fooyin setups, supported controls, and verification evidence.
- [How the lock works](docs/architecture.md) — technical overview for contributors.
