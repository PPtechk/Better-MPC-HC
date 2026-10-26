# Better MPC-HC

**Better MPC-HC** is an unofficial, modified build of
[Media Player Classic - Home Cinema (MPC-HC)](https://github.com/mpc-hc/mpc-hc) 1.7.13 for 64-bit Windows.
It keeps everything MPC-HC is known for (a light, fast, ad-free player with a huge format support)
and adds a **Skip Intro / Skip Outro button** and **volume above 100%, the way VLC does it**.

> Better MPC-HC is not affiliated with or endorsed by the MPC-HC Team.
> It is a community modification of their work, released under the same license (GPLv3).

- [Download](#download)
- [What is Better MPC-HC?](#what-is-better-mpc-hc)
- [New features](#new-features)
- [Differences from MPC-HC 1.7.13](#differences-from-mpc-hc-1713)
- [Known limitations and tips](#known-limitations-and-tips)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [License](#license)


## Download

Get `Better-MPC-HC.2.2.1.x64.exe` from the
[latest release](https://github.com/PPtechk/Better-MPC-HC/releases/latest) and run it.

- **System requirements:** 64-bit Windows 10 or 11 (older versions are not tested), a CPU with SSE2.
- **The installer is not code-signed.** Windows SmartScreen may warn about it:
  click *More info* → *Run anyway*.
- **Settings are shared with MPC-HC.** Better MPC-HC uses the same settings location
  (`HKCU\Software\MPC-HC\MPC-HC`), so your existing MPC-HC settings, shortcuts and filter configuration
  are kept.
- **It replaces an installed MPC-HC x64.** The installer uses the same installer ID as the official
  MPC-HC x64 installer, so it upgrades an existing MPC-HC x64 installation in place instead of installing
  next to it. The program file is `Better-MPC-HC64.exe`, the old `mpc-hc64.exe` is removed. After such an
  upgrade, set the file associations again in *Options → Player → Formats*. Uninstall it from
  *Settings → Apps* like any other program.


## What is Better MPC-HC?

MPC-HC is a free and open-source video and audio player for Windows. It started as
[Media Player Classic](https://sourceforge.net/projects/guliverkli/) by Gabest (Guliverkli project),
was continued by the MPC-HC Team and contributors, and its last official release, 1.7.13, was published
in July 2017. After that the official project was discontinued and its repository archived.

Better MPC-HC is based on the MPC-HC 1.7.13 source code. On top of it, it adds:

1. a button to skip intros and outros,
2. volume up to 200% with the keyboard and the mouse wheel,
3. a build setup that works with today's development tools,
4. its own name and version number (2.2.1), so it is not mixed up with the official MPC-HC.

Everything else works like in MPC-HC 1.7.13: the internal LAV Filters (0.70.2), the video renderers
(EVR-CP, madVR support, ...), subtitles, shaders, color management, the web interface, the 42 translations
and all the options.


## New features

### Skip Intro / Skip Outro button

While the playback position is inside an intro or an outro, a **Skip** button appears in the bottom right
corner of the video. Click it, or press **Enter**, to jump to the end of that part.

**How intros and outros are found.** Better MPC-HC reads the chapter names of the file (MKV, MP4 and
other formats with chapters). A chapter is an intro or an outro when its name contains one of these
words: `intro`, `opening`, `op`, `outro`, `ending`, `ed`, `credits`.

- Only whole words count, case-insensitively, so `OP`, `Opening 2` or `End Credits` match, but
  `Opera`, `Edward` or `Development` don't.
- Numbered variants like `OP1` or `ED02` match too.
- The part lasts from the start of that chapter to the start of the next chapter (or to the end of the file).
- Files without such chapter names don't get a button.

**Where the button is.** It stays inside the bottom eighth of the video, on the right. In fullscreen, and
whenever the controls hide automatically, it is placed just above the area that brings back the hidden
toolbar. So you can move the mouse onto the button without making the toolbar pop up and the button jump
away.

**Details:**
- After a skip, the button doesn't come back for that part unless you seek back into it.
- Enter keeps its meaning if you assigned plain Enter to a command yourself. Alt+Enter, Ctrl+Enter and
  Shift+Enter are not affected.
- The button is not shown in Direct3D exclusive fullscreen mode or in madVR's exclusive mode, because
  nothing can be drawn over the video there. It is only shown when playing files (not DVDs or TV capture).
- To turn the button off, set the DWORD value `ShowSkipButton` to `0` under
  `HKCU\Software\MPC-HC\MPC-HC\Settings` (there is no options page entry yet).

### Volume above 100% (like VLC)

In VLC the volume keys go above 100%, up to 200%. Better MPC-HC now does the same:

- **Up arrow** or **mouse wheel up** at 100% keeps increasing the volume up to **200%**, in steps of the
  volume step setting (5% by default). The on-screen message shows the real value, for example `Vol: 135%`.
- **Down arrow** or **mouse wheel down** first brings the volume back to 100%, then lowers it as usual.
- The volume slider stays at 100%; the part above it is the *Boost* of MPC-HC's internal audio switcher
  (*Options → Internal Filters → Audio Switcher*). That's why it needs the built-in audio switcher to be
  enabled (it is, by default), and why it doesn't work while the sound is muted.
- The boost is saved like the regular Boost setting, so it is still active after a restart.
- Above 100% the audio is amplified, so very loud passages may distort.

The behaviour follows VLC; the implementation is new, no VLC code is used.

### New name and version

The program shows **Better MPC-HC** everywhere you see a name: title bar, About box, tray icon, the
Explorer context menu (*Play with Better MPC-HC*, *Add to Better MPC-HC Playlist*), messages in all
42 languages, file properties, the installer, the install folder (`C:\Program Files\Better MPC-HC`),
Start menu and desktop shortcuts. The program file is `Better-MPC-HC64.exe` (its optional settings file
in portable mode is `Better-MPC-HC64.ini`). The version is **2.2.1**.

Intentionally unchanged, so that existing setups keep working:
- the settings location (`HKCU\Software\MPC-HC\MPC-HC`),
- the window class used by remote controls and other programs,
- the installer ID (see [Download](#download)),
- references to the original project: the copyright notice, the translation credits, the link to
  mpc-hc.org and the messages of the original update checker.

### Modern build setup

MPC-HC 1.7.13 needs Visual Studio 2015, old Windows SDKs and a 2017 MinGW toolchain that can't be
installed any more. Better MPC-HC builds with the Visual Studio 2019 Build Tools (v140 toolset), the
Windows 10 SDK and MSYS2 (GCC 16). The changes needed for that are in [`local-build`](local-build/README.md):
build scripts, two small source patches for FFmpeg and the QuickSync decoder, and the MinGW runtime DLLs
(`zlib1.dll`, `libiconv-2.dll`, `libwinpthread-1.dll`) that the newer FFmpeg build needs next to the
LAV filters.


## Differences from MPC-HC 1.7.13

| | MPC-HC 1.7.13 | Better MPC-HC 2.2.1 |
|---|---|---|
| Skip Intro / Skip Outro | — | Button based on chapter names, click or Enter |
| Volume with keys / mouse wheel | Up to 100% | Up to 200% (audio switcher boost) |
| Name shown | MPC-HC | Better MPC-HC |
| Version | 1.7.13 | 2.2.1 |
| Published builds | 32-bit and 64-bit | 64-bit |
| Windows versions | XP SP3 – 10 | Windows 10 and 11 (no XP support) |
| Compiler | Visual Studio 2015 (v140_xp), Windows 8.1 SDK | VS 2019 Build Tools (v140), Windows 10 SDK 10.0.19041 |
| FFmpeg compiler | GCC 7 (MinGW-w64) | GCC 16 (MSYS2 MinGW-w64) |
| Internal LAV Filters | 0.70.2 | 0.70.2 (unchanged) |
| Program file | `mpc-hc64.exe` | `Better-MPC-HC64.exe` |
| Settings location | `HKCU\Software\MPC-HC\MPC-HC` | Same |


## Known limitations and tips

- **AV1 and other newer formats.** The internal LAV Filters 0.70.2 (FFmpeg 3.3, 2017) can't decode AV1
  and other codecs released after 2017. Install a current standalone
  [LAV Filters](https://github.com/Nevcairiel/LAVFilters/releases) release (for example 0.83), then in
  Better MPC-HC open *Options → External Filters → Add Filter...*, add *LAV Splitter Source*,
  *LAV Splitter*, *LAV Video Decoder* and *LAV Audio Decoder*, and set each of them to *Prefer*.
- **Update check.** The built-in update checker still asks the original MPC-HC website, so it never
  reports Better MPC-HC updates. Check this repository's releases instead, and you can turn the check off
  in *Options → Miscellaneous*.
- **Crash reporter.** The crash reporting component is the one of the original project.
- **No options page entry** for the Skip button yet, see above for the registry value.


## Building from source

```bat
git -c core.longpaths=true -c url.https://gitea.1f0.de/LAV/.insteadOf=git://git.1f0.de/ -c url.https://code.videolan.org/videolan/.insteadOf=git://git.videolan.org/ clone --recurse-submodules https://github.com/PPtechk/Better-MPC-HC.git
cd Better-MPC-HC
local-build\build_installer.cmd
```

The `-c url...` options are needed because some LAV Filters submodules moved since 2017.
The installer is created as `bin\Better-MPC-HC.2.2.1.x64.exe`. The required tools and the one-time setup
are described in [`local-build/README.md`](local-build/README.md); the original MPC-HC instructions are in
[`docs/Compilation.md`](docs/Compilation.md).

### Changed source files

Compared to the `1.7.13` tag of MPC-HC:

- Skip button: `src/mpc-hc/SkipButton.*`, `SkipSegmentManager.*`, `SkipSegmentProvider.*` (new);
  `MainFrm.*`, `AppSettings.*`, `SettingsDefines.h`, `mpc-hc.vcxproj(.filters)`
- Volume above 100%: `src/mpc-hc/MainFrm.*`, `PlayerToolBar.cpp`
- Name and version: `include/version.h`, `src/mpc-hc/res/*.rc2`, `res/mpc-hc.exe.manifest.conf`,
  `mpc-hc.rc`, `mpcresources/PO/*`, `mpcresources/cfg/*`, `mpcresources/TranslationDataRC.py`,
  `mpciconlib/mpciconlib.rc`, `AboutDlg.cpp`, `MainFrm.cpp`, `Translations.cpp`, `LcdSupport.cpp`,
  `CrashReporter.cpp`, `mplayerc.cpp` (keeps the settings key independent of the name), `distrib/*.iss`
- Program file name: `src/mpc-hc/mpc-hc.vcxproj`, `CrashReporterDialog/CrashReporterDialog.cpp`,
  `src/MPCTestAPI/MPCTestAPIDlg.cpp`, `build.bat`, `distrib/mpc-hc_setup.iss`
- Build: `local-build/`, `Directory.Build.rsp`, `.gitignore`


## Credits

**Better MPC-HC** – idea, testing and releases by [PPtechk](https://github.com/PPtechk); the code changes
were written with the help of Claude Code, Anthropic's AI coding assistant.

Better MPC-HC exists thanks to the work of many people:

- **[MPC-HC](https://github.com/mpc-hc/mpc-hc)** – the MPC-HC Team and all contributors and translators,
  see [docs/Authors.txt](docs/Authors.txt). Website: [mpc-hc.org](https://mpc-hc.org/).
- **Media Player Classic** – the original player by **Gabest**
  ([Guliverkli project](https://sourceforge.net/projects/guliverkli/)), including VSFilter (DirectVobSub).
- **[LAV Filters](https://github.com/Nevcairiel/LAVFilters)** by Hendrik Leppkes (Nevcairiel) – the
  internal splitter and decoders.
- **[FFmpeg](https://ffmpeg.org/)** – the codecs behind LAV Filters.
- **[VLC media player](https://www.videolan.org/vlc/)** by VideoLAN – the model for the volume above 100%
  behaviour (no VLC code is used).
- Third-party libraries in [`src/thirdparty`](src/thirdparty) (versions in
  [`versions.txt`](src/thirdparty/versions.txt)), each under its own license:
  [libbs2b](http://bs2b.sourceforge.net/),
  [Little CMS](https://www.littlecms.com/),
  [MediaInfoLib and ZenLib](https://mediaarea.net/) by MediaArea,
  [MinHook](https://github.com/TsudaKageyu/minhook),
  [NanoSVG](https://github.com/memononen/nanosvg),
  [RapidJSON](https://github.com/Tencent/rapidjson),
  [RARFileSource](https://github.com/mpc-hc/rarfilesource),
  [sanear](https://github.com/alexmarsev/sanear),
  [SoundTouch](https://www.surina.net/soundtouch/),
  [soxr](https://sourceforge.net/projects/soxr/),
  [TinyXML-2](https://github.com/leethomason/tinyxml2),
  [UnRAR](https://www.rarlab.com/),
  [VirtualDub](https://www.virtualdub.org/),
  [XmlRpc4Win](https://github.com/drtimcooper/XmlRpc4Win),
  zita-resampler, ResizableLib, TreePropSheet, CSizingControlBar and [zlib](https://zlib.net/).
- Build tools: [Inno Setup](https://jrsoftware.org/isinfo.php) by Jordan Russell and Martijn Laan,
  [MSYS2](https://www.msys2.org/), Microsoft Visual Studio Build Tools.

Looking for an actively developed MPC-HC? The [MPC-HC fork by clsid2](https://github.com/clsid2/mpc-hc)
continues the original project with regular updates.


## License

Better MPC-HC is a modified version of MPC-HC and is licensed under the
[GNU General Public License v3](COPYING.txt), like MPC-HC itself. The source code of all changes is in this
repository. The third-party components keep their own licenses, see their folders in `src/thirdparty`.
