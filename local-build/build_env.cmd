@ECHO OFF
REM Runs a command in the local MPC-HC build environment, from the repository root.
REM Usage: build_env.cmd <command> [arguments]
REM        e.g. build_env.cmd build.bat Build x64 MPCHC Release Installer Silent Nocolors
REM See README.md in this folder for the environment this script expects.
SETLOCAL

SET "LOCAL_BUILD_DIR=%~dp0"
SET "MSBUILD16_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin"
SET "WIN10SDK_BIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64"

REM Machine-level variables set by the installers are read from the registry,
REM a shell started before the installation doesn't have them yet
FOR /F "tokens=2*" %%A IN ('REG QUERY "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v VS140COMNTOOLS 2^>NUL ^| FIND "REG_"') DO SET "VS140COMNTOOLS=%%B"

REM DirectX SDK extracted from DXSDK_Jun10.exe, without the headers the Windows 10 SDK replaces.
REM Preferred over an installed DirectX SDK, whose old DXGI headers break the Windows 10 SDK.
IF EXIST "C:\DXSDK_Jun10\Include\d3dx9.h" (
  SET "DXSDK_DIR=C:\DXSDK_Jun10\"
) ELSE (
  FOR /F "tokens=2*" %%A IN ('REG QUERY "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v DXSDK_DIR 2^>NUL ^| FIND "REG_"') DO SET "DXSDK_DIR=%%B"
)

REM The MPC-HC scripts call their helper batch files from the current directory
SET "NoDefaultCurrentDirectoryInExePath="

REM GCC 14+ turned these old warnings into errors, the 2017 ffmpeg sources predate that.
REM ffmpeg's configure starts from the CFLAGS environment variable.
SET "CFLAGS=-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type"

REM vcvarsall of the v140 toolset looks for MSBuild 14, which isn't installed: use the VS 2019 one
SET "PATH=%MSBUILD16_DIR%;%PATH%"

REM v140 looks for the Windows 10 SDK tools (rc.exe, mt.exe) in the old unversioned bin folder
SET "PATH=%PATH%;%WIN10SDK_BIN%"

CD /D "%LOCAL_BUILD_DIR%.."

REM vcvarsall puts the .NET 4.0 MSBuild first in the PATH, the build scripts call "MSBuild.exe"
REM from these two folders, where the forwarder to the VS 2019 MSBuild has to be found first
IF NOT EXIST "%LOCAL_BUILD_DIR%msbuild_forwarder\MSBuild.exe" (
  CALL "%LOCAL_BUILD_DIR%msbuild_forwarder\make_forwarder.cmd" || (ECHO [build_env] Cannot build the MSBuild.exe forwarder & EXIT /B 1)
)
FOR %%D IN (. src\thirdparty\LAVFilters\src) DO (
  IF NOT EXIST "%%D\MSBuild.exe" COPY /Y "%LOCAL_BUILD_DIR%msbuild_forwarder\MSBuild.exe" "%%D\MSBuild.exe" >NUL
)

ECHO [build_env] VS140COMNTOOLS=%VS140COMNTOOLS%
ECHO [build_env] DXSDK_DIR=%DXSDK_DIR%
ECHO [build_env] %*
CALL %*
SET "RC=%ERRORLEVEL%"
ECHO [build_env] exit code %RC%
ENDLOCAL & EXIT /B %RC%
