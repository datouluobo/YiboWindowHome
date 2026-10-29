param([int]$TargetPid, [int]$Seconds = 600)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class YiboResourceSample {
 [DllImport("user32.dll")] public static extern int GetGuiResources(IntPtr process, int flag);
}
'@
$start = Get-Process -Id $TargetPid -ErrorAction Stop
$started = $start.StartTime.ToUniversalTime().ToString('o')
$output = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts/resident-sample-20261029.jsonl'
if (Test-Path -LiteralPath $output) { throw 'Sample output already exists' }
$watch=[Diagnostics.Stopwatch]::StartNew()
while ($watch.Elapsed.TotalSeconds -le $Seconds) {
 try {
  $p=Get-Process -Id $TargetPid -ErrorAction Stop
  if ($p.StartTime.ToUniversalTime().ToString('o') -ne $started) { throw 'PID was reused' }
  $sample=[pscustomobject]@{Time=[DateTimeOffset]::Now.ToString('o');Elapsed=[math]::Round($watch.Elapsed.TotalSeconds,1);Pid=$TargetPid;Handles=$p.HandleCount;Private=$p.PrivateMemorySize64;Working=$p.WorkingSet64;CpuSeconds=$p.CPU;GDI=[YiboResourceSample]::GetGuiResources($p.Handle,0);USER=[YiboResourceSample]::GetGuiResources($p.Handle,1)}
  $sample | ConvertTo-Json -Compress | Add-Content -LiteralPath $output -Encoding utf8
 } catch { [pscustomobject]@{Time=[DateTimeOffset]::Now.ToString('o');Error=$_.Exception.Message} | ConvertTo-Json -Compress | Add-Content -LiteralPath $output -Encoding utf8; exit 1 }
 if ($watch.Elapsed.TotalSeconds -ge $Seconds) { break }
 Start-Sleep -Seconds 5
}
