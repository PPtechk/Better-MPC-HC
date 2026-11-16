# Builds the installer and saves it as "Beta<N> MPC-HC.exe" in the folder above the repository,
# for quick testing. Without -Number the next free number is used, an existing file is never overwritten.
# Usage: powershell -ExecutionPolicy Bypass -File local-build\make_beta.ps1 [-Number 2] [-SkipBuild]
#   -SkipBuild copies the installer that is already in bin\ (check its time stamp in the output)
param(
    [int]$Number = 0,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$target = Split-Path $repo -Parent

if ($Number -le 0) {
    $max = 0
    foreach ($file in Get-ChildItem $target -Filter 'Beta* MPC-HC.exe') {
        if ($file.Name -match '^Beta(\d+) MPC-HC\.exe$') {
            $max = [Math]::Max($max, [int]$Matches[1])
        }
    }
    $Number = $max + 1
}

$destination = Join-Path $target ("Beta{0} MPC-HC.exe" -f $Number)
if (Test-Path -LiteralPath $destination) {
    throw "$destination already exists"
}

if (-not $SkipBuild) {
    & cmd /c "`"$PSScriptRoot\build_installer.cmd`""
    if ($LASTEXITCODE -ne 0) {
        throw "The build failed (exit code $LASTEXITCODE)"
    }
}

$installer = Get-ChildItem "$repo\bin" -Filter 'Better-MPC-HC.*.x64.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $installer) {
    throw 'There is no installer in the bin folder'
}

Copy-Item $installer.FullName $destination
$saved = Get-Item -LiteralPath $destination
'{0} -> {1} ({2:N1} MB, installer built {3})' -f $installer.Name, $saved.FullName, ($saved.Length / 1MB), $installer.LastWriteTime
