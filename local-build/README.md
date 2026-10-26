# Building Better MPC-HC

This folder describes how the `bin\Better-MPC-HC.2.2.1.x64.exe` installer is built on a current
Windows 10/11 machine. The original MPC-HC 1.7.13 build expects tools that can no longer be installed
(Visual Studio 2015, the Windows 7.1A/8.1 SDKs, the 2017 MinGW toolchain), so these scripts adapt the
original `build.bat` to a modern toolchain.

## Quick start

```bat
local-build\build_installer.cmd
```

Result: `bin\Better-MPC-HC.2.2.1.x64.exe` (x64, Release, 42 languages, unsigned).
To check the environment only:

```bat
powershell -ExecutionPolicy Bypass -File local-build\check_env.ps1
```

## Contents of this folder

| File | Purpose |
|---|---|
| `build_installer.cmd` | Applies the submodule patches, then runs the full x64 build and creates the installer (`build.bat Build x64 MPCHC Release Installer`). |
| `build_env.cmd` | Runs any command in the build environment from the repository root (see the workarounds below). |
| `check_env.ps1` | Checks that every tool is in place. |
| `patches\` | Fixes for the LAV Filters submodules (FFmpeg, QuickSync decoder), see `patches\README.md`. |
| `msbuild_forwarder\msbuild_forwarder.c` | Source of the `MSBuild.exe` forwarder. |
| `msbuild_forwarder\make_forwarder.cmd` | Builds the forwarder; `build_env.cmd` calls it when `MSBuild.exe` is missing. |

`Directory.Build.rsp` in the repository root passes `PlatformToolset=v140` and
`WindowsTargetPlatformVersion=10.0.19041.0` to every MSBuild call. The project files expect the
`v140_xp` toolset and the Windows 8.1 SDK (`src\platform.props`), which the VS 2019 Build Tools can no
longer install. As a consequence this build doesn't support Windows XP.

`build.user.bat` (ignored by git) is MPC-HC's own local settings file with the MSYS2, MinGW, Git and
Python paths, see `docs\Compilation.md`.

## Required tools

| Tool | Location | Source |
|---|---|---|
| Visual Studio 2019 Build Tools: C++ build tools, MSVC v140, MSVC v142 + ATL/MFC, Windows 10 SDK 10.0.19041 | `C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools`, v140 in `...\Microsoft Visual Studio 14.0` | aka.ms/vs/16/release/vs_buildtools.exe |
| Inno Setup 5.6.1 (Unicode) | `C:\Program Files (x86)\Inno Setup 5` | files.jrsoftware.org/is/5/ |
| MSYS2 + `make`, `pkgconf`, `diffutils`, `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-binutils` | `C:\msys64` | github.com/msys2/msys2-installer (base tar.xz) + pacman |
| DirectX SDK (June 2010), **extracted, not installed** | `C:\DXSDK_Jun10` | download.microsoft.com, `DXSDK_Jun10.exe` |
| Python 2.7.18, **extracted, not installed** (`msiexec /a`) | `C:\Python27` | python.org, `python-2.7.18.amd64.msi` |
| yasm or nasm (for the FFmpeg assembly) | on the PATH | |
| Git | on the PATH | |

### One-time setup on a new machine

1. **Source:** some submodules of LAV Filters moved, so clone with URL rewrites:

   ```bat
   git -c core.longpaths=true -c url.https://gitea.1f0.de/LAV/.insteadOf=git://git.1f0.de/ -c url.https://code.videolan.org/videolan/.insteadOf=git://git.videolan.org/ clone --recurse-submodules https://github.com/PPtechk/Better-MPC-HC.git
   ```

   The path must not contain spaces (FFmpeg's build script refuses it).

2. **MSYS2:** extract the base archive to `C:\` (`tar -xf msys2-base-x86_64-*.tar.xz -C C:\`), then:

   ```bat
   C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
   C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make pkgconf diffutils mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils"
   ```

   FFmpeg expects binutils with the `x86_64-w64-mingw32-` prefix, MSYS2 only ships them without it,
   so prefixed copies have to be made in `C:\msys64\mingw64\bin`:
   `ar, nm, ranlib, strip, windres, dlltool, objcopy, objdump, ld, as` → `x86_64-w64-mingw32-<name>.exe`.

3. **DirectX SDK:** extract it with the Windows `tar` instead of installing it (its installer needs .NET 3.5):

   ```bat
   mkdir C:\DXSDK_Jun10
   tar -xf DXSDK_Jun10.exe -C C:\DXSDK_Jun10 --strip-components=1 DXSDK/Include DXSDK/Lib DXSDK/Redist DXSDK/Utilities
   ```

   Then move the 39 headers of `C:\DXSDK_Jun10\Include` that the Windows 10 SDK also has
   (`DXGI*.h`, `D3D10*.h`, `D3D11*.h`, `d3d9*.h`, `D3Dcompiler.h`, `XAudio2*.h`, `DWrite.h`, `D2D1*.h`, ...)
   to `C:\DXSDK_Jun10\Include_replaced_by_WinSDK`. The project files put the DirectX SDK include folder
   before the Windows SDK, and the new `wincodec.h` doesn't compile with the old DXGI headers.
   The D3DX headers, which are the reason the DirectX SDK is needed, stay in place.

4. **Python 2.7:** `msiexec /a python-2.7.18.amd64.msi /qn TARGETDIR=C:\Python27` (no registration,
   no PATH change). `src\mpc-hc\mpcresources\sync.py` (Python 2) generates the translated `.rc` files
   from the `.po` files during the build.

## Workarounds in `build_env.cmd`

| Setting | Reason |
|---|---|
| `VS140COMNTOOLS` read from the registry | `build.bat` finds `vcvarsall.bat` with it; a shell started before the installation doesn't have it. |
| `DXSDK_DIR=C:\DXSDK_Jun10\` | The extracted, cleaned-up DirectX SDK; used even if a DirectX SDK is installed. |
| `NoDefaultCurrentDirectoryInExePath` cleared | The build scripts call their helper files from the current directory (e.g. `build.user.bat`). |
| `CFLAGS=-Wno-error=...` | GCC 14+ turned some old warnings into errors, the 2017 FFmpeg sources rely on them. FFmpeg's `configure` starts from `CFLAGS`. |
| VS 2019 MSBuild on the PATH | The v140 `vcvarsall` looks for MSBuild 14, which isn't installed. |
| Windows 10 SDK `bin\10.0.19041.0\x64` on the PATH | v140 looks for `rc.exe`/`mt.exe` in the old, unversioned SDK folder. |
| `MSBuild.exe` forwarder in the repository root and in `src\thirdparty\LAVFilters\src` | The v140 `vcvarsall` puts the old .NET 4.0 `MSBuild.exe` first in the PATH. The scripts call `MSBuild.exe` from their current folder, where the forwarder is found first and passes every argument to the VS 2019 MSBuild. `build_env.cmd` builds and copies it when needed. |

Before the build, `build_installer.cmd` copies `zlib1.dll`, `libiconv-2.dll` and `libwinpthread-1.dll`
from `C:\msys64\mingw64\bin` to `bin\mpc-hc_x64\LAVFilters64` and checks them after the build. The
FFmpeg DLLs built with MSYS2 depend on them (the original 2017 MinGW environment had no zlib and iconv);
without them the internal LAV filters don't load (error 126/127) and no file handled by LAV can be
opened. MPC-HC loads LAV with `LOAD_WITH_ALTERED_SEARCH_PATH` (`src\DSUtil\DSUtil.cpp`,
`LoadExternalObject`), so the DLLs next to the filters are found first; the installer packs every
`*.dll` of that folder (`distrib\mpc-hc_setup.iss`).

## Differences from the official MPC-HC 1.7.13 build

- `v140` toolset + Windows 10 SDK (instead of `v140_xp` + 8.1 SDK): no Windows XP support.
- FFmpeg is compiled with GCC 16 (originally GCC 7) with relaxed error levels.
- Because of the Windows 10 SDK headers the shader compiler loads `d3dcompiler_47.dll` (part of
  Windows 10/11); `D3DCompiler_43.dll` is still packaged.
- The installer and the binaries are unsigned (no `signinfo.txt`).
- The About box shows `2.2.1 (<commit hash>)`, taken from `git describe` by `version.sh`.
