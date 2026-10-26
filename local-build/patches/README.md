# Patches for the LAV Filters submodules

These fixes live inside nested submodules of `src\thirdparty\LAVFilters\src`, which point to the
upstream LAV Filters / FFmpeg repositories, so they are kept here as patch files.
`local-build\build_installer.cmd` applies them automatically (a patch that is already applied is skipped).

| Patch | Applies to | Why |
|---|---|---|
| `ffmpeg-x86-mathops.patch` | `src\thirdparty\LAVFilters\src\ffmpeg` | Backport of the upstream FFmpeg fix "avcodec/x86/mathops: clip constants used with shift instructions within inline assembly". binutils newer than 2.41 rejects the original inline assembly. |
| `qsdecoder-ptchar.patch` | `src\thirdparty\LAVFilters\src\qsdecoder` | Removes `#define PTCHAR TCHAR*`. The Windows 10 SDK's `tchar.h` declares the `PTCHAR` type itself and the macro breaks that declaration. |

To apply them by hand:

```bat
git -C src\thirdparty\LAVFilters\src\ffmpeg apply ..\..\..\..\..\local-build\patches\ffmpeg-x86-mathops.patch
git -C src\thirdparty\LAVFilters\src\qsdecoder apply ..\..\..\..\..\local-build\patches\qsdecoder-ptchar.patch
```
