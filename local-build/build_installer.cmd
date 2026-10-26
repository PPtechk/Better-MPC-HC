@ECHO OFF
REM Builds Better MPC-HC x64 Release with the translations and creates the installer
REM (bin\Better-MPC-HC.2.2.1.x64.exe). See README.md in this folder.
SETLOCAL

REM The fixes needed inside the LAV Filters submodules are kept as patches (see patches\README.md)
SET "LAV_SRC_DIR=%~dp0..\src\thirdparty\LAVFilters\src"
CALL :ApplyPatch ffmpeg ffmpeg-x86-mathops.patch || EXIT /B 1
CALL :ApplyPatch qsdecoder qsdecoder-ptchar.patch || EXIT /B 1

REM The ffmpeg DLLs built with MSYS2 depend on these MinGW-w64 runtime DLLs. MPC-HC loads LAV with
REM LOAD_WITH_ALTERED_SEARCH_PATH, so they are found next to the LAV filters. They are copied before
REM the build, the installer packs every *.dll of the LAVFilters64 folder.
SET "LAV_OUT_DIR=%~dp0..\bin\mpc-hc_x64\LAVFilters64"
IF NOT EXIST "%LAV_OUT_DIR%" MD "%LAV_OUT_DIR%"
FOR %%D IN (zlib1.dll libiconv-2.dll libwinpthread-1.dll) DO (
  COPY /Y "C:\msys64\mingw64\bin\%%D" "%LAV_OUT_DIR%\%%D" >NUL || (ECHO [ERROR] Cannot copy %%D & EXIT /B 1)
)

CALL "%~dp0build_env.cmd" build.bat Build x64 MPCHC Release Installer Silent Nocolors
SET "RC=%ERRORLEVEL%"
IF %RC% NEQ 0 EXIT /B %RC%

REM The installer has to contain the runtime DLLs: check that they are still in place
FOR %%D IN (zlib1.dll libiconv-2.dll libwinpthread-1.dll) DO (
  IF NOT EXIST "%LAV_OUT_DIR%\%%D" (ECHO [ERROR] %%D is missing from %LAV_OUT_DIR% & EXIT /B 1)
)
ENDLOCAL
EXIT /B 0

:ApplyPatch
REM %1 is the submodule folder in LAVFilters\src, %2 is the patch file in the patches folder
git -C "%LAV_SRC_DIR%\%~1" apply --reverse --check "%~dp0patches\%~2" >NUL 2>&1 && EXIT /B 0
git -C "%LAV_SRC_DIR%\%~1" apply "%~dp0patches\%~2" || (ECHO [ERROR] Cannot apply %~2 & EXIT /B 1)
ECHO [INFO] %~2 applied
EXIT /B 0
