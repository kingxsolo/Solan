# تنزيل مترجم GCC محمول (WinLibs UCRT) — بدون MSYS2، بدون تثبيت على النظام
$ErrorActionPreference = "Stop"
$dest = "C:\Solan-PC\tools"
$zip = "$dest\winlibs.zip"
$url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-ucrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip"

if (Test-Path "$dest\winlibs\mingw64\bin\gcc.exe") {
  Write-Host "✓ المترجم موجود مسبقاً في $dest\winlibs"
  exit 0
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
if (-not (Test-Path $zip) -or (Get-Item $zip).Length -lt 100MB) {
  Write-Host "▸ تنزيل المترجم (~264 MB، مرة واحدة فقط)..."
  curl.exe -L --retry 3 -o $zip $url
}
Write-Host "▸ فك الضغط..."
Expand-Archive -Path $zip -DestinationPath $dest -Force
Remove-Item $zip -ErrorAction SilentlyContinue
if (Test-Path "$dest\winlibs\mingw64\bin\gcc.exe") { Write-Host "✓ جاهز: $dest\winlibs\mingw64\bin\gcc.exe" }
else { throw "فشل فك الضغط" }
