param([string]$Prefix = "$env:LOCALAPPDATA\solan", [switch]$NoSqlite)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$msys = "C:\msys64"
if (-not (Test-Path "$msys\usr\bin\bash.exe")) {
  if (Get-Command winget -ErrorAction SilentlyContinue) {
    Write-Host "+ winget install MSYS2.MSYS2"
    winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
  } else {
    throw "ثبّت MSYS2 من https://www.msys2.org ثم أعد تشغيل هذا السكربت (أو ثبّت winget)."
  }
}
$bash = "$msys\usr\bin\bash.exe"
$here = (Get-Location).Path -replace '\\','/'
$sq = if ($NoSqlite) { "--no-sqlite" } else { "" }
$script = @"
set -e
export MSYSTEM=UCRT64
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc curl unzip
cd "`$(cygpath '$here')"
sh install.sh --no-deps --build-only --target windows $sq
"@
& $bash -lc $script
if (-not (Test-Path ".\solan.exe")) { throw "فشل البناء: solan.exe غير موجود" }

New-Item -ItemType Directory -Force -Path "$Prefix\bin" | Out-Null
Copy-Item ".\solan.exe" "$Prefix\bin\solan.exe" -Force
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$Prefix\bin*") {
  [Environment]::SetEnvironmentVariable("Path", "$userPath;$Prefix\bin", "User")
  Write-Host "أُضيف $Prefix\bin إلى PATH — افتح نافذة PowerShell جديدة."
}
Write-Host "https/wss تحتاج OpenSSL 3 (winget install ShiningLight.OpenSSL.Light) أو libssl-3-x64.dll بجوار solan.exe"
Write-Host "تم! جرّب: solan --version"
