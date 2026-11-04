# قياس أداء موسع: سولان مقابل بايثون وNode.js — best-of-5
# الاستعمال: powershell -File pc\bench2.ps1
$ErrorActionPreference = "Continue"
# أولوية High لهذه العملية وكل الأبناء — لتقليل تأثير الأحمال الخلفية على دقة القياس
try { (Get-Process -Id $PID).PriorityClass = 'High' } catch {}
$load = 0
try { $cs = (Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 1).CounterSamples | Where-Object InstanceName -eq '_total'; $load = [math]::Round($cs.CookedValue) } catch {}
$scriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$root = Split-Path $scriptDir -Parent
$solan = (Resolve-Path (Join-Path $root "dist\solan.exe")).Path
$enc = [Text.UTF8Encoding]::new($false)
$tmp = "$env:TEMP\solan_bench2"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host "=== قياس موسع: سولان مقابل بايثون وNode ===" -ForegroundColor Cyan
Write-Host "الحمل الخلفي للمعالج: $load% (أولوية القياس: High)"
Write-Host "سولان: $solan"

# ---------- نظيرات بايثون ----------
$py = [ordered]@{
"1_فيب"     = "import time`ndef fib(n):`n    return n if n < 2 else fib(n-1)+fib(n-2)`nt=time.perf_counter()`nr=fib(27)`nd=(time.perf_counter()-t)*1000`nprint(f'fib={r} ms={d}')"
"2_حلقة"    = "import time`nt=time.perf_counter()`nm=0`nfor i in range(10000000): m+=i`nd=(time.perf_counter()-t)*1000`nprint(f'sum={m} ms={d}')"
"3_نصوص"    = "import time`nt=time.perf_counter()`ns=''`nfor i in range(100000): s+='x'+str(i)`nd=(time.perf_counter()-t)*1000`nprint(f'len={len(s)} ms={d}')"
"4_قوائم"   = "import time`nt=time.perf_counter()`nl=[]`nfor i in range(1000000): l.append(i)`nm=sum(l)`nd=(time.perf_counter()-t)*1000`nprint(f'sum={m} ms={d}')"
"5_قواميس"  = "import time`nt=time.perf_counter()`nd={}`nfor i in range(200000): d['k'+str(i)]=i`nm=0`nfor i in range(200000): m+=d['k'+str(i)]`nd2=(time.perf_counter()-t)*1000`nprint(f'sum={m} ms={d2}')"
"6_جيسون"   = "import json,time`nitems=[{'id':i,'name':'item'+str(i),'val':i*1.5} for i in range(20000)]`nt=time.perf_counter()`nfor _ in range(10): s=json.dumps(items); json.loads(s)`nd=(time.perf_counter()-t)*1000`nprint(f'len={len(s)} ms={d}')"
"7_استدعاء" = "import time`ndef add(a,b): return a+b`nt=time.perf_counter()`nm=0`nfor i in range(2000000): m=add(m,1)`nd=(time.perf_counter()-t)*1000`nprint(f'sum={m} ms={d}')"
}

# ---------- نظيرات Node ----------
$js = [ordered]@{
"1_فيب"     = "function fib(n){return n<2?n:fib(n-1)+fib(n-2)}let t=performance.now();let r=fib(27);let d=performance.now()-t;console.log('fib='+r+' ms='+d);"
"2_حلقة"    = "let t=performance.now();let m=0;for(let i=0;i<10000000;i++)m+=i;let d=performance.now()-t;console.log('sum='+m+' ms='+d);"
"3_نصوص"    = "let t=performance.now();let s='';for(let i=0;i<100000;i++)s+='x'+i;let d=performance.now()-t;console.log('len='+s.length+' ms='+d);"
"4_قوائم"   = "let t=performance.now();let l=[];for(let i=0;i<1000000;i++)l.push(i);let m=l.reduce((a,b)=>a+b,0);let d=performance.now()-t;console.log('sum='+m+' ms='+d);"
"5_قواميس"  = "let t=performance.now();let o={};for(let i=0;i<200000;i++)o['k'+i]=i;let m=0;for(let i=0;i<200000;i++)m+=o['k'+i];let d=performance.now()-t;console.log('sum='+m+' ms='+d);"
"6_جيسون"   = "let items=[];for(let i=0;i<20000;i++)items.push({id:i,name:'item'+i,val:i*1.5});let t=performance.now();let s;for(let k=0;k<10;k++){s=JSON.stringify(items);JSON.parse(s)}let d=performance.now()-t;console.log('len='+s.length+' ms='+d);"
"7_استدعاء" = "function add(a,b){return a+b}let t=performance.now();let m=0;for(let i=0;i<2000000;i++)m=add(m,1);let d=performance.now()-t;console.log('sum='+m+' ms='+d);"
}

foreach ($k in $py.Keys) { [IO.File]::WriteAllText("$tmp\$k.py", $py[$k], $enc) }
foreach ($k in $js.Keys)  { [IO.File]::WriteAllText("$tmp\$k.js", $js[$k], $enc) }

# ---------- أدوات القياس ----------
function Get-Num([string]$out) {
  if ($out -match 'ms=([0-9.eE+\-]+)') { return [double]$Matches[1] }
  if ($out -match 'مللي=([0-9.eE+\-]+)') { return [double]$Matches[1] }
  return -1
}

function Run-Best([string]$exe, [string]$arg, [int]$runs = 5) {
  $best = [double]::PositiveInfinity
  $lastOut = ""
  for ($i = 0; $i -lt $runs; $i++) {
    $out = (& $exe $arg 2>&1 | Out-String).Trim()
    $v = Get-Num $out
    if ($v -lt 0) { return @{ ms = -1; out = $out } }
    $lastOut = $out
    if ($v -lt $best) { $best = $v }
  }
  return @{ ms = $best; out = $lastOut }
}

$pyExe = (Get-Command python -ErrorAction SilentlyContinue).Source
$nodeExe = (Get-Command node -ErrorAction SilentlyContinue).Source
Write-Host "بايثون: $pyExe"
Write-Host "Node:   $nodeExe"
Write-Host ""

# ---------- قياس الإقلاع (عملية كاملة) ----------
function Startup-Ms([string]$exe, [string[]]$argList, [int]$runs = 7) {
  $best = [double]::PositiveInfinity
  for ($i = 0; $i -lt $runs; $i++) {
    $t = [System.Diagnostics.Stopwatch]::StartNew()
    $null = (& $exe @argList 2>&1 | Out-String)
    $t.Stop()
    if ($t.Elapsed.TotalMilliseconds -lt $best) { $best = $t.Elapsed.TotalMilliseconds }
  }
  return $best
}

$solStart = Startup-Ms $solan @("$root\pc\قياسات\0_فارغ.سو")
Write-Host ("إقلاع العملية: سولان {0} م.ث" -f [math]::Round($solStart, 1))
if ($pyExe)   { $pyStart = Startup-Ms $pyExe @('-c','pass'); Write-Host ("               بايثون {0} م.ث" -f [math]::Round($pyStart, 1)) }
if ($nodeExe) { $jsStart = Startup-Ms $nodeExe @('-e','');     Write-Host ("               Node   {0} م.ث" -f [math]::Round($jsStart, 1)) }
Write-Host ""

# ---------- القياسات السبعة ----------
$names = @("1_فيب","2_حلقة","3_نصوص","4_قوائم","5_قواميس","6_جيسون","7_استدعاء")
$labels = [ordered]@{
  "1_فيب"="فيب(27) تعاودي"; "2_حلقة"="حلقة 10م جمع"; "3_نصوص"="نصوص 100ألف دمج";
  "4_قوائم"="قوائم 1م إضافة+جمع"; "5_قواميس"="قواميس 200ألف كتابة+قراءة";
  "6_جيسون"="جيسون 20ألف ×10"; "7_استدعاء"="استدعاءات 2م"
}

$results = @{}
foreach ($n in $names) {
  $s = Run-Best $solan "$root\pc\قياسات\$n.سو"
  $p = @{ ms = -1 }; $j = @{ ms = -1 }
  if ($pyExe)   { $p = Run-Best $pyExe "$tmp\$n.py" }
  if ($nodeExe) { $j = Run-Best $nodeExe "$tmp\$n.js" }
  $results[$n] = @{ solan = $s.ms; py = $p.ms; js = $j.ms }
  Write-Host ("{0}: سولان {1} | بايثون {2} | Node {3}" -f $n, [math]::Round($s.ms,1), [math]::Round($p.ms,1), [math]::Round($j.ms,1))
}

# ---------- الجدول ----------
Write-Host ""
$hdr = "{0,-24} {1,10} {2,10} {3,10} {4,12} {5,12}" -f "المهمة","سولان","بايثون","Node","س/بايثون","س/Node"
Write-Host $hdr
Write-Host ("-" * 84)
foreach ($n in $names) {
  $r = $results[$n]
  $rp = 0; $rj = 0
  if ($r.py -gt 0 -and $r.solan -gt 0) { $rp = [math]::Round($r.py / $r.solan, 2) }
  if ($r.js -gt 0 -and $r.solan -gt 0) { $rj = [math]::Round($r.js / $r.solan, 2) }
  $line = "{0,-24} {1,10:N1} {2,10:N1} {3,10:N1} {4,12:N2} {5,12:N2}" -f $labels[$n], $r.solan, $r.py, $r.js, $rp, $rj
  Write-Host $line
}
Write-Host ""
Write-Host "الأرقام بالمللي ثانية (أفضل 5 تشغيلات). النسبة = وقت المنافس ÷ وقت سولان (أعلى = سولان أسرع)."
