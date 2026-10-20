# sober-gentoo

Roblox on a minimal Gentoo laptop through [Sober](https://sober.vinegarhq.org/), without Portage's Flatpak. Sober is only distributed on Flathub and refuses to start outside a real Flatpak 1.10 or newer, and `sys-apps/flatpak` would add 59 packages to a 155-package system. This repo builds Flatpak 1.18.4 and the libraries Gentoo doesn't have into `~/.local/opt/flatpak`, so Portage's package count stays the same, and adds launchers for Sober and the [Voidstrap](https://github.com/KloBraticc/Voidstrap) launcher plus the settings that made Roblox fast and sharp on this laptop.

**Status:** in daily use on an HP OmniBook 7 (Ryzen AI 9 HX 475, Radeon 890M, 2880x1800 120 Hz OLED) with Gentoo, Xorg at depth 30 and dwm. Sober 1.8.0 runs Roblox through it, launched either directly or from Voidstrap 1.1.3.7.

## Features

- **Flatpak 1.18.4 from source, no Portage packages:** ostree 2026.4, GnuPG 2.5 with gpgme 2.2 (Flathub signatures are checked), AppStream 1.2.1, libseccomp, libarchive, json-glib, libxmlb and libfyaml go into one prefix with an rpath, so nothing needs `LD_LIBRARY_PATH`. bubblewrap and xdg-dbus-proxy come from Flatpak's own subprojects. Every tarball is checked against its upstream sha256.
- **Build-only tools stay out of the system:** m4, bison, gperf and pyparsing are built into a scratch directory, and glib's `glib-mkenums`, `glib-genmarshal` and `gdbus-codegen` (separate packages on Gentoo) are taken from the matching glib tarball.
- **`flatpak` launcher:** puts the prefix on `PATH` (gpgme finds `gpg` there) and starts the sound server before any `flatpak run` of Sober, which crashes at start without one. Every way of starting Sober goes through it, Voidstrap included.
- **`sober-pointer`:** Sober's HiDPI mode doesn't work on X11 (its own troubleshooting page says so): Roblox is drawn at the display scale, but pointer positions arrive in real pixels and are scaled again, so the cursor moves twice as fast as the desktop's and runs off the right and bottom. While Sober's game window has focus, `sober-pointer` scales pointer motion by 1/scale (each pointer's transformation matrix) and keeps the pointer in the part of the window Sober maps onto the whole of it (XFixes pointer barriers); when focus leaves it restores both and puts the pointer where Roblox's cursor was. The `flatpak` launcher starts it for every Sober run.
- **`voidstrap` launcher:** Voidstrap draws through wgpu, which presents nothing into X's 10-bit windows at depth 30, so its settings window stayed black. The launcher preloads [xcb8bit](https://github.com/r3clusionn/scripts) so Voidstrap alone gets 8-bit windows.
- **A FastFlag preset for the most frames** (`voidstrap/ClientAppSettings.json`), using only flags on Roblox's allowlist.

## How to install

Requires a C/C++ compiler, make, meson, ninja, python3, git, curl, and glib, libcurl, OpenSSL, libxml2, xz, zlib, zstd, gdk-pixbuf, libcap and libXau with their headers (all already on a typical desktop Gentoo).

```sh
git clone https://github.com/r3clusionn/sober-gentoo
cd sober-gentoo
./install.sh                                   # launchers + build scripts
~/.local/opt/flatpak/tools/build.sh            # fetch, check and build everything
flatpak --user remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak --user install flathub org.vinegarhq.Sober
```

`build.sh` takes single steps too (`fetch tools glibtools gnupg libs appstream ostree flatpak trim`). `FLATPAK_WORK` moves the sources and build trees (default `~/.cache/flatpak-local`), and `OPT_CFLAGS` replaces the default flags, which are this laptop's `make.conf` (`-march=native -O3 -flto`, no hardening).

Voidstrap: download `Voidstrap_<version>_linux-x64.tar.gz` from its releases, check the sha256 listed there, put the `Voidstrap` binary in `~/.local/opt/voidstrap/` and copy `voidstrap/ClientAppSettings.json` to `~/.local/share/voidstrap/VoidstrapMods/ClientSettings/` if you want the preset.

## How to use

```sh
sober                       # Roblox through Sober
voidstrap                   # Voidstrap's menu: Launch Roblox, Settings
voidstrap -player           # straight into Roblox through Voidstrap
flatpak --user update       # Sober and its runtime
```

Settings that made the difference on this laptop:

| Setting | Where | Why |
|---|---|---|
| `"enable_hidpi": true` | `~/.var/app/org.vinegarhq.Sober/config/sober/config.json` | the UI was tiny at 2x scaling (`Xft.dpi: 192`); `sober-pointer` keeps the mouse right |
| Maximum Frame Rate 120, or `FramerateCap` 120 | Roblox's in-game settings, or `GlobalBasicSettings_13.xml` in Sober's `appData` (with Voidstrap's FPS unlocker off, or it sets 9999 again) | Roblox caps at 60 by default; the panel shows at most 120 (below) |
| `"graphics_optimization_mode": "performance"` | Sober's `config.json` (Voidstrap: Sober page) | lower quality, more frames |
| auto fullscreen | Voidstrap's Sober page, or F11 in Sober | full-screen windows are flipped straight to the panel (below) |

The FastFlag preset: graphics quality pinned at level 1 of 21 (`DFIntDebugFRMQualityLevelOverride`; raise the number for more detail and render distance), no MSAA, lighting updates paused (`DFFlagDebugPauseVoxelizer`), no grass, lowest-detail CSG meshes at every distance. Textures stay at Roblox's own quality.

## How it works

- Sober ships only on Flathub, checks that it runs inside Flatpak 1.10 or newer and stops with "Sober couldn't load" otherwise. Its licence forbids repackaging or working around its checks, so this repo installs the real thing from Flathub with a real Flatpak and contains no Sober files.
- The build patches four things, all for features this setup doesn't use: AppStream's `po/`, `data/` and `tests/` are skipped (they need gettext, itstool and the AppStream CLI); ostree's configure gets an empty libe2p and a three-line `ext2fs/ext2_fs.h` with the `FS_IOC_*` names (the only thing it takes from e2fsprogs); Flatpak's FUSE dependency is made optional and `revokefs` is only built with the system helper, which is off (user installations only). The trim step then drops headers, pkg-config files, docs and the GnuPG tools Flatpak never calls: 17 MB stay.
- Present (the X extension games show frames through) puts a full-screen window's buffer straight on the panel, a page flip, with no copy; a window that isn't full-screen is copied at each refresh. With the amdgpu driver's TearFree on (this laptop's setting), flips wait for the next refresh, and frames that arrive in between are replaced by newer ones, so the newest frame is always shown and nothing tears. TearFree off is no faster here (measured in a game rendering about 1000 frames/s):

  | TearFree | variable refresh | frames shown | panel |
  |---|---|---|---|
  | on | on | 120/s | 120 Hz |
  | off | on | 40/s | 40 Hz: flips wait for a refresh that only comes early when a flip is queued |
  | off | off | 120/s | 120 Hz, still no tearing |

  Uncapped, Roblox renders about 1000 frames/s and 880 of them are thrown away, so a 120 cap shows the same frames for an eighth of the GPU work.

## Measurements

| | |
|---|---|
| Portage packages | 155 before and after (Portage's Flatpak: +59) |
| `~/.local/opt/flatpak` | 17 MB |
| `~/.local/share/flatpak` (user installation) | 1.7 GB: GNOME 51 runtime 1.1 GB, Mesa 26.2.2 (`GL.default` and `-extra`) 538 MB, codecs 44 MB, Sober 18 MB |
| Roblox, default cap, windowed | 60 frames/s copied at 120 Hz |
| Roblox, uncapped, full-screen | 460 to 1060 frames/s rendered, 120 flips/s, the rest replaced before they're shown |
| Voidstrap settings window | no frame in 8 s (black) without xcb8bit, first frame after 330 ms with it |

Frame counts come from Present's CompleteNotify events on Sober's window (frames, copy or flip, and the panel's refresh count).

## Verification

- Sober 1.8.0 passes its Flatpak check, logs in, joins games and renders with Vulkan on RADV (Radeon 890M, Mesa 26.2.2 from the Flathub extension).
- Flathub's GPG-signed summary is verified with the GnuPG built here.
- Voidstrap 1.1.3.7 finds Sober through `flatpak info --user`, starts it with `flatpak run`, applies the FastFlags to Sober's `config.json` and draws all its windows with the launcher.
- The sound server is started for Sober from both `sober` and Voidstrap.
- `sober-pointer`, with a test window named like Sober's (1582x1764 at 1,35): the pointer was moved to half its offset on focus, relative motion stopped at 791,916 and 1,35 (the box Sober maps onto the window), the touchpad's matrix read 0.5 while focused and 1.0 after, and it exited 5 s after the window closed.

## Limits

- Tested on one laptop. `build.sh` reads glib's `.pc` files from `/usr/lib64/pkgconfig` (Gentoo's layout).
- User installations only: no system helper, so no system-wide installs and no `revokefs`.
- Sober's HiDPI mode on X11 also puts the text being typed into a box at half its position and size; `sober-pointer` only fixes the mouse. Camera turning (first person, right button) may still be faster than the desktop: games usually read raw relative motion, which the matrix doesn't scale (not measured); Roblox's camera sensitivity setting covers it.
- No xdg-desktop-portal here: GameMode can't register through it (a warning only); opening links from Sober is untested.
- `flatpak` doesn't update itself; run `build.sh` again after changing the versions in `fetch.sh`.

## License

MIT (see `LICENSE`). Flatpak, the libraries it builds, Sober and Voidstrap have their own licences.
