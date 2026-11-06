# Better MPC-HC on Linux

Better MPC-HC is a Windows program (it is built on MFC, DirectShow and Direct3D), so the Linux version is
an **AppImage** that contains the Windows build together with a portable [Wine](https://www.winehq.org).
You don't need to install Wine or anything else.

## Install and run

1. Download `Better-MPC-HC-<version>-x86_64.AppImage` from the
   [latest release](https://github.com/PPtechk/Better-MPC-HC/releases/latest).
2. Make sure **FUSE 2** is installed. Every AppImage needs it to mount itself, and the recent
   distributions ship only FUSE 3, so on those it has to be added once:

   ```sh
   sudo apt install libfuse2t64     # Ubuntu 24.04 and newer, Debian 13 and newer
   sudo apt install libfuse2        # Ubuntu 22.04 and older, Debian 12
   sudo dnf install fuse-libs       # Fedora
   sudo pacman -S fuse2             # Arch, Manjaro
   sudo zypper install libfuse2     # openSUSE
   ```

   If you can't or don't want to install it, see *If it doesn't start* below: the AppImage can also run
   without FUSE.
3. Make it executable: in the file manager *Properties → Permissions → Allow executing file as program*,
   or in a terminal:

   ```sh
   chmod +x Better-MPC-HC-*-x86_64.AppImage
   ```

4. Double-click it. The first start takes a few seconds while the player's environment is prepared.

On the first start the AppImage adds **Better MPC-HC** to the application menu and registers it for the
common video and audio file types, so you can also choose it in *Open With*. If you move the AppImage,
start it once from the new place and the menu entry follows it.

Requirements: a 64-bit (x86_64) Linux desktop (X11 or Wayland with XWayland) and FUSE 2 (step 2).
Everything else the player needs is inside the AppImage.

## If it doesn't start

Start it from a terminal, that is the only place where the reason shows up:

```sh
./Better-MPC-HC-*-x86_64.AppImage
```

- **`dlopen(): error loading libfuse.so.2`**, *Cannot mount AppImage* or nothing at all on double-click —
  FUSE 2 is missing, install it as in step 2. If you'd rather not install anything, this runs the AppImage
  without FUSE by unpacking it to a temporary folder on every start (slower, it is about 160 MB):

  ```sh
  ./Better-MPC-HC-*-x86_64.AppImage --appimage-extract-and-run
  ```

- **`Permission denied`** — the file isn't executable yet, see step 3.
- **A black or distorted picture** — check that the renderer is *VMR-9 (renderless)* in
  *Options → Playback → Output*, see the next section.

## Where things are

| What | Where |
|---|---|
| Settings and the Wine environment | `~/.local/share/better-mpc-hc/` |
| Menu entry | `~/.local/share/applications/better-mpc-hc.desktop` |

To remove Better MPC-HC:

```sh
./Better-MPC-HC-*-x86_64.AppImage --remove-integration   # removes the menu entry and the file associations
./Better-MPC-HC-*-x86_64.AppImage --reset                # deletes the settings
```

then delete the AppImage file.

## Differences from the Windows version

The player, the subtitles, the Skip Intro/Outro button and the volume up to 200% work the same way.
Because it runs in Wine:

- the video renderer is *VMR-9 (renderless)* by default (*Options → Playback → Output*). EVR Custom
  Presenter, the Windows default, distorts the picture under Wine,
- hardware video decoding (DXVA) isn't available, videos are decoded by the CPU,
- the madVR renderer and the Direct3D exclusive fullscreen mode can't be used,
- the file dialogs show the Linux files on drive `Z:` (your home folder is `Z:\home\<user>`).

## Building the AppImage

On Linux (or WSL), with `wget`, `xz` and ImageMagick installed:

```sh
linux/build-appimage.sh path/to/Better-MPC-HC.2.2.1.x64.exe
```

The script downloads the portable Wine build and `appimagetool` (cached in `linux/cache`), installs the
Windows build into a temporary Wine environment exactly like on Windows, and packs everything into
`bin/Better-MPC-HC-2.2.1-x86_64.AppImage`. See `THIRD-PARTY.txt` for the bundled components.
