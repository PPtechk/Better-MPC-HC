@ECHO OFF
REM Rebuilds MSBuild.exe from msbuild_forwarder.c with the MSYS2 MinGW-w64 GCC
SETLOCAL
SET "PATH=C:\msys64\mingw64\bin;%PATH%"
gcc -O2 -municode -static -Wall -o "%~dp0MSBuild.exe" "%~dp0msbuild_forwarder.c"
EXIT /B %ERRORLEVEL%
