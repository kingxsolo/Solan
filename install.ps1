# =====================================================================
#  سولان — التثبيت على ويندوز (بناء أصلي 100% — لا MSYS2 ولا WSL)
#  الاستعمال:  powershell -ExecutionPolicy Bypass -File install.ps1
#  يبني solan.exe ويثبته في %LOCALAPPDATA%\solan\bin ويضيفه إلى PATH
# =====================================================================
param([string]$Prefix = "$env:LOCALAPPDATA\solan", [switch]$NoInstaller)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "سولان — بناء نسخة ويندوز الأصلية" -ForegroundColor Cyan

# 1) المترجم المحمول (مرة واحدة فقط) — نسبة إلى مجلد المستودع نفسه
$gcc = Join-Path $PSScriptRoot "tools\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
  & powershell -ExecutionPolicy Bypass -File "pc\get-compiler.ps1"
  if (-not (Test-Path $gcc)) { throw "تعذّر تجهيز المترجم" }
}

# 2) البناء
$buildArgs = @()
if ($NoInstaller) { $buildArgs += "-NoStrip" }
& powershell -ExecutionPolicy Bypass -File "pc\build-pc.ps1" @buildArgs
if (-not (Test-Path "dist\solan.exe")) { throw "فشل البناء" }

# 3) التثبيت
New-Item -ItemType Directory -Force -Path "$Prefix\bin" | Out-Null
Copy-Item "dist\solan.exe" "$Prefix\bin\solan.exe" -Force
$binDir = "$Prefix\bin"
$userPath = [string][Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ';') -notcontains $binDir) {
  $newPath = $binDir
  if ($userPath) { $newPath = "$userPath;$binDir" }
  [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
  Write-Host "أُضيف $binDir إلى PATH — افتح نافذة طرفية جديدة" -ForegroundColor Yellow
}

# 4) المُثبِّت الجاهز للتوزيع
$setup = Get-ChildItem "dist\Solan-*-Setup-x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($setup) { Write-Host "المُثبِّت جاهز للتوزيع: $($setup.FullName)" -ForegroundColor Green }

Write-Host "تم! جرّب:  solan --version" -ForegroundColor Green
