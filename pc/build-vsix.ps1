# =====================================================================
#  بناء حزمة إضافة VS Code لسولان (.vsix) — بلا أدوات خارجية
#  الاستخدام: powershell -ExecutionPolicy Bypass -File pc\build-vsix.ps1
#  الناتج: dist\Solan-<الإصدار>.vsix
#  التثبيت: code --install-extension dist\Solan-<الإصدار>.vsix
# =====================================================================
param(
  [string]$Out = "dist"   # مجلد الناتج نسبة إلى جذر المشروع
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "editor\vscode"
$pkg  = Get-Content (Join-Path $src "package.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$ver  = $pkg.version
$outFile = Join-Path (Join-Path $root $Out) ("Solan-" + $ver + ".vsix")

if (-not (Test-Path (Join-Path $root $Out))) {
  New-Item -ItemType Directory -Force -Path (Join-Path $root $Out) | Out-Null
}
if (Test-Path $outFile) { Remove-Item $outFile -Force }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# [Content_Types].xml — أنواع محتوى حزمة OPC
$contentTypes = @'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json"/>
  <Default Extension="vsixmanifest" ContentType="text/xml"/>
  <Default Extension="png" ContentType="image/png"/>
</Types>
'@

# extension.vsixmanifest — بيان الحزمة القياسي
$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($pkg.name)" Version="$ver" Publisher="$($pkg.publisher)"/>
    <DisplayName>$($pkg.displayName)</DisplayName>
    <Description xml:space="preserve">$($pkg.description)</Description>
    <Tags>solan,arabic,عربية,سولان</Tags>
    <Categories>$($pkg.categories -join ',')</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($pkg.engines.vscode)"/>
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value=""/>
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value=""/>
      <Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value=""/>
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
"@

# الملفات التي تدخل الحزمة داخل مجلد extension/
$files = @(
  "package.json",
  "extension.js",
  "icon.png",
  "language-configuration.json",
  "syntaxes/solan.tmLanguage.json",
  "snippets/solan.json"
)

$utf8 = New-Object System.Text.UTF8Encoding($false)  # بلا BOM
$fs = [System.IO.File]::Create($outFile)
try {
  $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    $add = {
      param($entryPath, $bytes)
      $e = $zip.CreateEntry($entryPath, [System.IO.Compression.CompressionLevel]::Optimal)
      $s = $e.Open()
      $s.Write($bytes, 0, $bytes.Length)
      $s.Dispose()
    }
    & $add "[Content_Types].xml" $utf8.GetBytes($contentTypes)
    & $add "extension.vsixmanifest" $utf8.GetBytes($manifest)
    foreach ($f in $files) {
      $local = $f.Replace("/", "\")
      $bytes = [System.IO.File]::ReadAllBytes((Join-Path $src $local))
      & $add ("extension/" + $f) $bytes
    }
  } finally { $zip.Dispose() }
} finally { $fs.Dispose() }

$size = (Get-Item $outFile).Length
Write-Host "✓ الحزمة: $outFile ($size بايت)"
Write-Host "  للتثبيت: code --install-extension $outFile"
