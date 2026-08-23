# Bundle the built xviewer.exe + xcodec.dll + Qt/FFmpeg/SDL2 runtime DLLs into
# one self-contained folder and zip it.
# Usage: package-windows.ps1 <build-dir> <ffmpeg-win64-dir> <output-zip>
param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$FfmpegWin64Dir,
    [Parameter(Mandatory=$true)][string]$OutputZip
)
$ErrorActionPreference = "Stop"

$Exe = Join-Path $BuildDir "src\xviewer\Release\xviewer.exe"
$XcodecDll = Join-Path $BuildDir "src\xcodec\Release\xcodec.dll"
if (-not (Test-Path $Exe)) { throw "$Exe not found" }
if (-not (Test-Path $XcodecDll)) { throw "$XcodecDll not found" }

$StageDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
$AppDir = Join-Path $StageDir "XPlayer"
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null

Copy-Item $Exe $AppDir
Copy-Item $XcodecDll $AppDir
Copy-Item (Join-Path $FfmpegWin64Dir "bin\*.dll") $AppDir

# Bundles Qt's own DLLs + plugins next to the exe. windeployqt is on PATH
# after jurplel/install-qt-action runs.
windeployqt.exe --release (Join-Path $AppDir "xviewer.exe")

Compress-Archive -Path $AppDir -DestinationPath $OutputZip -Force
Remove-Item -Recurse -Force $StageDir

Write-Host "packaged: $OutputZip"
