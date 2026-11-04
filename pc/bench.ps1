# قياس أداء سولان ويندوز مقابل بايثون وNode.js
# الاستعمال: powershell -File pc\bench.ps1 [مسار solan.exe]
param([string]$SolanExe = "")
$ErrorActionPreference = "Continue"
$scriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
if (-not $SolanExe) { $SolanExe = Join-Path $scriptDir "..\dist\solan.exe" }
$solan = (Resolve-Path $SolanExe -ErrorAction Stop).Path
Write-Host "=== قياس الأداء: $solan ===" -ForegroundColor Cyan

# --- 1) حلقة حسابية كثيفة (JIT) ---
$solFib = @'
دالة فيب(ن) {
  اذا ن < 2 { ارجع ن }
  ارجع فيب(ن - 1) + فيب(ن - 2)
}
اطبع(فيب(27))
'@
$f = "$env:TEMP\bench_fib.سو"
[IO.File]::WriteAllText($f, $solFib, [Text.UTF8Encoding]::new($false))

$t = [System.Diagnostics.Stopwatch]::StartNew()
$r = (& $solan $f 2>&1 | Out-String).Trim()
$t.Stop()
$solMs = $t.Elapsed.TotalMilliseconds
Write-Output ("سولان  فيب(27) = {0}  في {1} م.ث" -f $r, [math]::Round($solMs, 1))

# بايثون
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($py) {
  $pyCode = "import sys`ndef fib(n):`n    return n if n < 2 else fib(n-1)+fib(n-2)`nprint(fib(27))"
  $pf = "$env:TEMP\bench_fib.py"
  [IO.File]::WriteAllText($pf, $pyCode)
  $t = [System.Diagnostics.Stopwatch]::StartNew()
  $r = & $py $pf
  $t.Stop()
  $pyMs = $t.Elapsed.TotalMilliseconds
  Write-Output ("بايثون فيب(27) = {0}  في {1} م.ث" -f $r, [math]::Round($pyMs, 1))
  Write-Output ("النسبة: سولان أسرع ×{0}" -f [math]::Round($pyMs / $solMs, 1)) -ForegroundColor Green
}

# Node
$nd = (Get-Command node -ErrorAction SilentlyContinue).Source
if ($nd) {
  $nf = "$env:TEMP\bench_fib.js"
  [IO.File]::WriteAllText($nf, "function fib(n){return n<2?n:fib(n-1)+fib(n-2)};console.log(fib(27))")
  $t = [System.Diagnostics.Stopwatch]::StartNew()
  $r = & $nd $nf
  $t.Stop()
  $ndMs = $t.Elapsed.TotalMilliseconds
  Write-Output ("Node    فيب(27) = {0}  في {1} م.ث" -f $r, [math]::Round($ndMs, 1))
  Write-Output ("النسبة: سولان أسرع ×{0}" -f [math]::Round($ndMs / $solMs, 1)) -ForegroundColor Green
}

# --- 2) حلقة تراكمية بسيطة (startup + JIT loop) ---
$solLoop = @'
خل م = 0
لكل ي في مدى(0, 10000000) { م += ي }
اطبع(م)
'@
$f2 = "$env:TEMP\bench_loop.سو"
[IO.File]::WriteAllText($f2, $solLoop, [Text.UTF8Encoding]::new($false))
$t = [System.Diagnostics.Stopwatch]::StartNew()
$r = (& $solan $f2 2>&1 | Out-String).Trim()
$t.Stop()
Write-Output ("سولان  حلقة 10م = {0}  في {1} م.ث" -f $r, [math]::Round($t.Elapsed.TotalMilliseconds, 1))

# --- 3) زمن الإقلاع الصافي ---
$times = @()
1..5 | ForEach-Object {
  $t = [System.Diagnostics.Stopwatch]::StartNew()
  & $solan -e 'اطبع(1)' | Out-Null
  $t.Stop()
  $times += $t.Elapsed.TotalMilliseconds
}
Write-Output ("سولان  إقلاع متوسط = {0} م.ث" -f [math]::Round(($times | Measure-Object -Average).Average, 1))
