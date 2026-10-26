# Checks the prerequisites of the local Better MPC-HC x64 installer build (see README.md)
# Usage: powershell -ExecutionPolicy Bypass -File check_env.ps1
$ErrorActionPreference = 'Continue'
$clone = Split-Path $PSScriptRoot -Parent
$vs    = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools'
$v14   = 'C:\Program Files (x86)\Microsoft Visual Studio 14.0'
$sdk   = 'C:\Program Files (x86)\Windows Kits\10'
$msys  = 'C:\msys64'
$dx    = 'C:\DXSDK_Jun10'
$results = New-Object System.Collections.Generic.List[object]

function Add-Result($group, $name, [bool]$ok, $detail = '') {
    $results.Add([pscustomobject]@{ Group = $group; Item = $name; OK = $(if ($ok) { 'OK' } else { 'MISSING' }); Detail = $detail })
}

# --- sources
Add-Result 'Source' 'Git' ([bool](Get-Command git -ErrorAction SilentlyContinue)) ''
Add-Result 'Source' 'Clone path without spaces' ($clone -notmatch '\s') $clone
Push-Location $clone
$sub = @(& git submodule status --recursive 2>$null)
$bad = @($sub | Where-Object { $_ -match '^[-U]' })
Add-Result 'Source' 'Submodules checked out' (($sub.Count -gt 0) -and ($bad.Count -eq 0)) ("{0} submodules, {1} missing" -f $sub.Count, $bad.Count)
Pop-Location
Add-Result 'Source' 'build.user.bat' (Test-Path "$clone\build.user.bat") ''
Add-Result 'Source' 'Directory.Build.rsp' (Test-Path "$clone\Directory.Build.rsp") ''
Add-Result 'Source' 'MSBuild forwarder (binary or MinGW GCC to build it)' ((Test-Path "$PSScriptRoot\msbuild_forwarder\MSBuild.exe") -or (Test-Path 'C:\msys64\mingw64\bin\gcc.exe')) ''

# --- Visual Studio
$msbuild = "$vs\MSBuild\Current\Bin\MSBuild.exe"
Add-Result 'Visual Studio' 'MSBuild 16' (Test-Path $msbuild) ''
Add-Result 'Visual Studio' 'v140 vcvarsall.bat' (Test-Path "$v14\VC\vcvarsall.bat") ''
Add-Result 'Visual Studio' 'v140 cl.exe (x86_amd64)' (Test-Path "$v14\VC\bin\x86_amd64\cl.exe") ''
Add-Result 'Visual Studio' 'v140 MFC (mfc140u.lib x64)' (Test-Path "$v14\VC\atlmfc\lib\amd64\mfc140u.lib") ''
Add-Result 'Visual Studio' 'VS140COMNTOOLS (machine)' ([bool][Environment]::GetEnvironmentVariable('VS140COMNTOOLS', 'Machine')) ''
Add-Result 'Visual Studio' 'Windows 10 SDK 10.0.19041' (Test-Path "$sdk\Include\10.0.19041.0\um\windows.h") ''
Add-Result 'Visual Studio' 'Windows 10 SDK rc.exe' (Test-Path "$sdk\bin\10.0.19041.0\x64\rc.exe") ''

# --- DirectX SDK (extracted)
Add-Result 'DirectX SDK' 'd3dx9.h + x64 d3dx9.lib' ((Test-Path "$dx\Include\d3dx9.h") -and (Test-Path "$dx\Lib\x64\d3dx9.lib")) $dx
Add-Result 'DirectX SDK' 'Duplicated headers removed (no DXGI.h)' (-not (Test-Path "$dx\Include\DXGI.h")) ''
Add-Result 'DirectX SDK' 'Redist cabs (x64)' ((Test-Path "$dx\Redist\Jun2010_d3dx9_43_x64.cab") -and (Test-Path "$dx\Redist\Jun2010_D3DCompiler_43_x64.cab")) ''

# --- MSYS2 / MinGW-w64
$env:PATH = "$msys\usr\bin;$msys\bin;$msys\mingw64\bin;$env:PATH"
foreach ($t in 'make', 'pkg-config', 'sh') {
    Add-Result 'MSYS2' $t ([bool](Get-Command "$t.exe" -ErrorAction SilentlyContinue)) ''
}
foreach ($t in 'gcc', 'ar', 'nm', 'ranlib', 'strip', 'windres', 'dlltool') {
    Add-Result 'MSYS2' "x86_64-w64-mingw32-$t" (Test-Path "$msys\mingw64\bin\x86_64-w64-mingw32-$t.exe") ''
}
Add-Result 'MSYS2' 'yasm or nasm' ([bool](Get-Command yasm.exe, nasm.exe -ErrorAction SilentlyContinue)) ''

# --- Python 2.7 (translations)
$py = 'C:\Python27\python.exe'
Add-Result 'Python' 'Python 2.7' (Test-Path $py) $(if (Test-Path $py) { (& $py -c "import sys; print(sys.version.split()[0])") })

# --- Inno Setup 5
$inno = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 5_is1' -ErrorAction SilentlyContinue).'Inno Setup: App Path'
Add-Result 'Inno Setup' 'Inno Setup 5' ($inno -and (Test-Path "$inno\ISCC.exe")) $inno

$results | Format-Table -AutoSize -Wrap | Out-String -Width 200
