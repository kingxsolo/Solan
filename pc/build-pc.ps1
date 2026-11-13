# =====================================================================
#  سولان (Solan) — بناء نسخة ويندوز الأصلية
#  بناء 100% ويندوز: لا MSYS2، لا WSL، لا بيئة لينكس.
#  الناتج: solan.exe واحد ثابت (static) سريع + مُثبِّت Setup.exe
#
#  الاستعمال:
#    powershell -ExecutionPolicy Bypass -File pc\build-pc.ps1            # بناء عادي (64-بت)
#    powershell -ExecutionPolicy Bypass -File pc\build-pc.ps1 -V3        # بناء AVX2 للأجهزة الحديثة
#    powershell -ExecutionPolicy Bypass -File pc\build-pc.ps1 -Arch32    # بناء 32-بت (i686+SSE2، يعمل على 32 و64 بت)
#    powershell -ExecutionPolicy Bypass -File pc\build-pc.ps1 -Installer # بناء + إنشاء المُثبِّت
# =====================================================================
param(
  [string]$GccBin = "",           # مسار مجلد bin للمترجم (يُكتشف تلقائياً إن تُرك فارغاً)
  [switch]$V3,                    # استهداف x86-64-v3 (AVX2) — أسرع على الأجهزة الحديثة (2013+)
  [switch]$Arch32,                # بناء 32-بت (i686) — يعمل على ويندوز 32 و64 بت؛ الـJIT معطّل (مفسّر فقط)
  [switch]$Installer,             # بناء المُثبِّت أيضاً (يتطلب Inno Setup)
  [switch]$NoStrip
)
$ErrorActionPreference = "Continue"   # ملاحظة: أخطاء المترجمات تُكشف عبر $LASTEXITCODE وليس عبر استثناءات PowerShell
Set-Location (Join-Path $PSScriptRoot "..")
$rootTools = Join-Path (Split-Path $PSScriptRoot) "tools"

$VER = "1.3.2"
$fv = ($VER -split '\.')
while ($fv.Count -lt 4) { $fv += "0" }
$OUT = "dist"
New-Item -ItemType Directory -Force -Path $OUT | Out-Null

# ---------- 1) إيجاد المترجم ----------
function Find-Gcc {
  param([string]$Hint)
  $cands = @()
  if ($Hint) { $cands += $Hint }
  if ($Arch32) {
    $cands += (Join-Path $rootTools "mingw32\bin")
    $cands += (Join-Path $rootTools "winlibs\mingw32\bin")
    $cands += "C:\tools\mingw32\bin"
  } else {
    $cands += (Join-Path $rootTools "mingw64\bin")
    $cands += (Join-Path $rootTools "winlibs\mingw64\bin")
    $cands += "C:\tools\mingw64\bin"
    $cands += (Join-Path $env:LOCALAPPDATA "Programs\WinLibs\mingw64\bin")
  }
  foreach ($c in (Get-Command gcc -ErrorAction SilentlyContinue)) { $cands += $c.Source }
  foreach ($c in $cands) {
    if (-not $c) { continue }
    $p = if ($c -like "*.exe") { $c } else { Join-Path $c "gcc.exe" }
    if (Test-Path $p) { return (Split-Path $p) }
  }
  return $null
}

$gccDir = Find-Gcc $GccBin
if (-not $gccDir) {
  Write-Host "✗ لم يُعثر على مترجم GCC." -ForegroundColor Red
  Write-Host "  الحل السريع: شغّل pc\get-compiler.ps1 لتنزيل مترجم محمول تلقائياً."
  exit 1
}
$env:Path = "$gccDir;$env:Path"
$gccVer = & gcc -dumpversion
Write-Host "✓ المترجم: $(Join-Path $gccDir 'gcc.exe') (إصدار $gccVer)" -ForegroundColor Green

# ---------- 1ب) تعطيل manifest المترجم الافتراضي ----------
# gcc/ld يربطان default-manifest.o دائماً، فيتعارض مع manifest سولان ويبقى
# manifestان في الملف النهائي (تحذير .rsrc merge failure — مشكلة MSYS2 #454).
# الحل المعروف: استبداله بكائن فارغ لتبقى manifest سولان وحيدة.
$triplet = if ($Arch32) { "i686-w64-mingw32" } else { "x86_64-w64-mingw32" }
$dmPath = Join-Path (Split-Path $gccDir) "$triplet\lib\default-manifest.o"
if (Test-Path $dmPath) {
  $emptyC = Join-Path $env:TEMP "solan_dm_empty.c"
  Set-Content -Path $emptyC -Value "" -Encoding ASCII
  & gcc -c $emptyC -o "$env:TEMP\solan_dm_empty.o" 2>$null
  if ($LASTEXITCODE -eq 0 -and (Test-Path "$env:TEMP\solan_dm_empty.o")) {
    if (-not (Test-Path "$dmPath.orig")) { Copy-Item $dmPath "$dmPath.orig" -Force }
    Copy-Item "$env:TEMP\solan_dm_empty.o" $dmPath -Force
    Write-Host "✓ عُطِّل default-manifest.o المدمج في المترجم (كان يتعارض مع manifest سولان)" -ForegroundColor DarkGray
  }
}

# ---------- 2) ملف الموارد (أيقونة + معلومات الإصدار + بيان Manifest) ----------
$manifest = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity version="$VER.0" processorArchitecture="*" name="Solan.Solan" type="win32"/>
  <description>سولان — لغة برمجة عربية</description>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings xmlns:ws2005="http://schemas.microsoft.com/SMI/2005/WindowsSettings">
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
      <supportedOS Id="{1f676c76-80e1-4239-95bb-83d0f6d0da78}"/>
    </application>
  </compatibility>
</assembly>
"@
Set-Content -Path "pc\solan.manifest" -Value $manifest -Encoding UTF8

$rc = @"
1 VERSIONINFO
FILEVERSION     $($fv[0]),$($fv[1]),$($fv[2]),$($fv[3])
PRODUCTVERSION  $($fv[0]),$($fv[1]),$($fv[2]),$($fv[3])
FILEOS          0x40004L
FILETYPE        0x1L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName",      "Solan Project"
      VALUE "FileDescription",  "Solan - Arabic Programming Language (Native Windows Build)"
      VALUE "FileVersion",      "$VER"
      VALUE "InternalName",     "solan"
      VALUE "LegalCopyright",   "Solan Project"
      VALUE "OriginalFilename", "solan.exe"
      VALUE "ProductName",      "Solan"
      VALUE "ProductVersion",   "$VER"
    END
  END
END
1 ICON "solan.ico"
1 24 "solan.manifest"
"@
Set-Content -Path "pc\solan.rc" -Value $rc -Encoding ASCII

& windres "pc\solan.rc" -O coff -o "pc\solan_res.o" -I pc
if ($LASTEXITCODE -ne 0) { throw "فشل تجميع الموارد (windres)" }

# ---------- 3) التجميع بأقصى تحسين ----------
$march = if ($Arch32) { "i686" } elseif ($V3) { "x86-64-v3" } else { "x86-64-v2" }
$arch = if ($Arch32) { "i686 (SSE2)" } else { $march }
$strip = if ($NoStrip) { "" } else { "-Wl,-s" }
$exe = if ($Arch32) { "$OUT\solan32.exe" } elseif ($V3) { "$OUT\solan-v3.exe" } else { "$OUT\solan.exe" }

Write-Host "▸ تجميع سولان (استهداف $arch، ربط ثابت، LTO)..." -ForegroundColor Cyan
$sw = [System.Diagnostics.Stopwatch]::StartNew()
if ($Arch32) {
  & gcc solan.c "pc\solan_res.o" -o $exe `
    -O3 -flto=auto -fno-ident -fno-math-errno `
    -mfpmath=sse -msse2 "-march=$march" `
    -ffunction-sections -fdata-sections -fvisibility=hidden `
    -static -static-libgcc `
    "-Wl,--gc-sections" "-Wl,-O2" "-Wl,--as-needed" $strip `
    -D_WIN32_WINNT=0x0A00 `
    -lws2_32 -lshell32 -ladvapi32 -lm
} else {
  & gcc solan.c "pc\solan_res.o" -o $exe `
    -O3 -flto=auto -fno-ident -fno-math-errno `
    "-march=$march" -mtune=generic `
    -ffunction-sections -fdata-sections -fvisibility=hidden `
    -static -static-libgcc `
    "-Wl,--gc-sections" "-Wl,-O2" "-Wl,--as-needed" $strip `
    -D_WIN32_WINNT=0x0A00 `
    -lws2_32 -lshell32 -ladvapi32 -lm
}
if ($LASTEXITCODE -ne 0) { throw "فشل التجميع" }
$sw.Stop()
Write-Host "✓ اكتمل البناء في $([math]::Round($sw.Elapsed.TotalSeconds,1)) ثانية" -ForegroundColor Green

$size = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host "✓ الناتج: $exe ($size MB)" -ForegroundColor Green

# ---------- 4) فحص سريع ----------
& $exe --version
if ($LASTEXITCODE -ne 0) { throw "الملف التنفيذي لا يعمل" }
& $exe pc\هلا.سو
if ($LASTEXITCODE -ne 0) { throw "فشل تنفيذ برنامج تجريبي" }

# ---------- 5) المُثبِّت ----------
if ($Installer) {
  if ($Arch32) { Write-Host "✗ المُثبِّت غير متاح للبناء 32-بت حالياً — الناتج dist\solan32.exe جاهز للنسخ اليدوي" -ForegroundColor Yellow }
  else {
  Write-Host "▸ بناء المُثبِّت (Inno Setup)..." -ForegroundColor Cyan
  $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $iscc) {
    Write-Host "  تثبيت Inno Setup..." 
    winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements --silent
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
  }
  if (-not $iscc) { Write-Host "✗ لم أجد ISCC.exe — ثبّت Inno Setup يدوياً" -ForegroundColor Yellow; exit 1 }
  & $iscc "pc\installer.iss" "/DExeName=$(Join-Path (Get-Location) $exe)"   # مسار مطلق: ISCC يحل النسبي نسبة إلى مجلد .iss
  if ($LASTEXITCODE -ne 0) { throw "فشل بناء المُثبِّت" }
  Write-Host "✓ المُثبِّت: $OUT\Solan-Setup.exe" -ForegroundColor Green
  }
}

Write-Host ""
Write-Host "تم! جرّب:  $($exe.Replace((Get-Location).Path + '\','')) هلا.سو" -ForegroundColor Yellow
exit 0
