# =====================================================================
#  توليد أيقونات سولان من البانر المصدر — بخلفية شفافة
#  الاستخدام: powershell -ExecutionPolicy Bypass -File assets\make-icons.ps1
#  النواتج:
#    assets\solan-logo.png        الشعار بخلفية شفافة (512×512)
#    editor\vscode\icon.png       أيقونة إضافة VS Code (256×256)
#    pc\solan.ico                 أيقونة ويندوز متعددة الأحجام (256/64/48/32/16)
# =====================================================================
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$srcPath = Join-Path $root "assets\solan-banner.jpg"

# ---------- 1) تحميل المصدر كـ ARGB ----------
$src = New-Object System.Drawing.Bitmap($srcPath)
$out = New-Object System.Drawing.Bitmap 512, 512, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.DrawImage($src, 0, 0, 512, 512)
$g.Dispose()
$src.Dispose()

$w = 512; $h = 512
$rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
$bd = $out.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $bd.Stride
$buf = New-Object byte[] ($stride * $h)
[System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $buf.Length)

# ---------- 2) إزالة الخلفية: تعبئة بالفيضان من الحواف ----------
# الأسود (أو القريب منه) المتصل بحدود الصورة يصبح شفافاً؛ العناصر الداخلية تبقى
$thresh = 45
$visited = New-Object bool[] ($w * $h)
$q = New-Object int[] (8 * $w * $h + 8192)
$qh = 0; $qt = 0
for ($x = 0; $x -lt $w; $x++) {
  $q[$qt++] = $x; $q[$qt++] = 0
  $q[$qt++] = $x; $q[$qt++] = ($h - 1)
}
for ($y = 1; $y -lt $h - 1; $y++) {
  $q[$qt++] = 0; $q[$qt++] = $y
  $q[$qt++] = ($w - 1); $q[$qt++] = $y
}
while ($qh -lt $qt) {
  $x = $q[$qh++]; $y = $q[$qh++]
  $idx = $y * $w + $x
  if ($visited[$idx]) { continue }
  $visited[$idx] = $true
  $o = $y * $stride + $x * 4
  $lum = [int]($buf[$o + 2] * 0.3 + $buf[$o + 1] * 0.6 + $buf[$o] * 0.1)
  if ($lum -ge $thresh) { continue }
  $buf[$o + 3] = 0
  if ($x -gt 0)     { $q[$qt++] = $x - 1; $q[$qt++] = $y }
  if ($x -lt $w - 1){ $q[$qt++] = $x + 1; $q[$qt++] = $y }
  if ($y -gt 0)     { $q[$qt++] = $x; $q[$qt++] = $y - 1 }
  if ($y -lt $h - 1){ $q[$qt++] = $x; $q[$qt++] = $y + 1 }
}

# ---------- 3) تنعيم الحواف: بكسلات محيطية داكنة تصبح شبه شفافة ----------
for ($y = 0; $y -lt $h; $y++) {
  for ($x = 0; $x -lt $w; $x++) {
    $o = $y * $stride + $x * 4
    if ($buf[$o + 3] -eq 0) { continue }
    $edge = $false
    if ($x -gt 0 -and $buf[$o - 4 + 3] -eq 0) { $edge = $true }
    if (-not $edge -and $x -lt $w - 1 -and $buf[$o + 4 + 3] -eq 0) { $edge = $true }
    if (-not $edge -and $y -gt 0 -and $buf[$o - $stride + 3] -eq 0) { $edge = $true }
    if (-not $edge -and $y -lt $h - 1 -and $buf[$o + $stride + 3] -eq 0) { $edge = $true }
    if ($edge) {
      $lum = [int]($buf[$o + 2] * 0.3 + $buf[$o + 1] * 0.6 + $buf[$o] * 0.1)
      $a = [int](($lum - 15) * 255 / 85)
      if ($a -lt 0) { $a = 0 }
      if ($a -gt 255) { $a = 255 }
      $buf[$o + 3] = [byte]$a
    }
  }
}

[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $bd.Scan0, $buf.Length)
$out.UnlockBits($bd)

# ---------- 4) حفظ الشعار الشفاف ----------
$logoPath = Join-Path $root "assets\solan-logo.png"
$out.Save($logoPath, [System.Drawing.Imaging.ImageFormat]::Png)

# ---------- 5) أيقونة إضافة VS Code (256×256) ----------
$vs = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($vs)
$g.InterpolationMode = 'HighQualityBicubic'
$g.SmoothingMode = 'HighQuality'
$g.PixelOffsetMode = 'HighQuality'
$g.DrawImage($out, 0, 0, 256, 256)
$g.Dispose()
$vs.Save((Join-Path $root "editor\vscode\icon.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$vs.Dispose()

# ---------- 6) أيقونة ويندوز متعددة الأحجام (.ico) ----------
$sizes = @(256, 64, 48, 32, 16)
$pngs = @()
foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap $s, $s
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = 'HighQualityBicubic'
  $g.SmoothingMode = 'HighQuality'
  $g.PixelOffsetMode = 'HighQuality'
  $g.DrawImage($out, 0, 0, $s, $s)
  $g.Dispose()
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $pngs += ,@($s, $ms.ToArray())
  $bmp.Dispose(); $ms.Dispose()
}
$ms2 = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms2)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($e in $pngs) {
  $s = $e[0]; $wByte = 0; if ($s -lt 256) { $wByte = $s }
  $bw.Write([byte]$wByte); $bw.Write([byte]$wByte); $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([uint16]1); $bw.Write([uint16]32)
  $bw.Write([uint32]$e[1].Length); $bw.Write([uint32]$offset)
  $offset += $e[1].Length
}
foreach ($e in $pngs) { $bw.Write($e[1]) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $root "pc\solan.ico"), $ms2.ToArray())
$bw.Dispose(); $ms2.Dispose()
$out.Dispose()

Write-Host "✓ assets\solan-logo.png (شفاف)"
Write-Host "✓ editor\vscode\icon.png"
Write-Host "✓ pc\solan.ico"
