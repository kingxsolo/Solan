# =====================================================================
#  سولان — التثبيت على ويندوز (بناء أصلي 100% — لا MSYS2 ولا WSL)
#  الاستعمال:  powershell -ExecutionPolicy Bypass -File install.ps1
#  يبني solan.exe ويثبته في %LOCALAPPDATA%\solan\bin ويضيفه إلى PATH
# =====================================================================
param([string]$Prefix = "$env:LOCALAPPDATA\solan", [switch]$NoInstaller)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "سولان — بناء نسخة ويندوز الأصلية" -ForegroundColor Cyan

# 1) المترجم المحمول (مرة واحدة فقط)
$gcc = "C:\Solan-PC\tools\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
  & powershell -ExecutionPolicy Bypass -File "pc\get-compiler.ps1"
  if (-not (Test-Path $gcc)) { throw "تعذّر تجهيز المترجم" }
}

# 2) البناء
$args = @()
if ($NoInstaller) { $args += "-NoStrip" }
& powershell -ExecutionPolicy Bypass -File "pc\build-pc.ps1" @args
if (-not (Test-Path "dist\solan.exe")) { throw "فشل البناء" }

# 3) التثبيت
New-Item -ItemType Directory -Force -Path "$Prefix\bin" | Out-Null
Copy-Item "dist\solan.exe" "$Prefix\bin\solan.exe" -Force
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$Prefix\bin*") {
  [Environment]::SetEnvironmentVariable("Path", "$userPath;$Prefix\bin", "User")
  Write-Host "أُضيف $Prefix\bin إلى PATH — افتح نافذة طرفية جديدة" -ForegroundColor Yellow
}

# 4) المُثبِّت الجاهز للتوزيع
if (Test-Path "dist\Solan-1.2.0-Setup-x64.exe") {
  Write-Host "المُثبِّت جاهز للتوزيع: dist\Solan-1.2.0-Setup-x64.exe" -ForegroundColor Green
}

Write-Host "تم! جرّب:  solan --version" -ForegroundColor Green
