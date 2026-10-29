param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [string]$Label = 'idle',
    [int]$Seconds = 15
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if ($Seconds -lt 2 -or $Seconds -gt 300) { throw 'Seconds must be between 2 and 300.' }
if (-not ('YiboHomeMetrics' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class YiboHomeMetrics {
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("kernel32.dll")] static extern bool K32QueryWorkingSet(IntPtr process, IntPtr buffer, uint size);
    public static long PrivateWorkingSet(IntPtr process) {
        int size = 16 * 1024 * 1024;
        IntPtr buffer = Marshal.AllocHGlobal(size);
        try {
            if (!K32QueryWorkingSet(process, buffer, (uint)size)) return -1;
            long count = Marshal.ReadIntPtr(buffer).ToInt64(), pages = 0;
            for (long i = 0; i < count; i++) {
                long entry = Marshal.ReadIntPtr(buffer, checked((int)((i + 1) * IntPtr.Size))).ToInt64();
                if ((entry & (1L << 8)) == 0) pages++;
            }
            return pages * Environment.SystemPageSize;
        } finally { Marshal.FreeHGlobal(buffer); }
    }
}
'@
}
$samples = @()
$process = Get-Process -Id $ProcessId
$cpuStart = $process.TotalProcessorTime.TotalMilliseconds
$watch = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $Seconds; $i++) {
    $process.Refresh()
    $samples += [pscustomobject]@{
        Time = [DateTimeOffset]::Now.ToString('o')
        PrivateBytes = $process.PrivateMemorySize64
        PrivateWorkingSet = [YiboHomeMetrics]::PrivateWorkingSet($process.Handle)
        WorkingSet = $process.WorkingSet64
        Handles = $process.HandleCount
        GdiObjects = [YiboHomeMetrics]::GetGuiResources($process.Handle, 0)
        UserObjects = [YiboHomeMetrics]::GetGuiResources($process.Handle, 1)
    }
    Start-Sleep -Seconds 1
}
$process.Refresh()
$cpuMs = $process.TotalProcessorTime.TotalMilliseconds - $cpuStart
$result = [pscustomobject]@{
    Label = $Label; ProcessId = $ProcessId; SampleSeconds = $watch.Elapsed.TotalSeconds
    LogicalProcessors = [Environment]::ProcessorCount
    CpuMilliseconds = $cpuMs
    CpuPercentOfMachine = 100 * $cpuMs / $watch.Elapsed.TotalMilliseconds / [Environment]::ProcessorCount
    PrivateBytesMin = ($samples.PrivateBytes | Measure-Object -Minimum).Minimum
    PrivateBytesMax = ($samples.PrivateBytes | Measure-Object -Maximum).Maximum
    PrivateWorkingSetMax = ($samples.PrivateWorkingSet | Measure-Object -Maximum).Maximum
    WorkingSetMax = ($samples.WorkingSet | Measure-Object -Maximum).Maximum
    GdiObjectsMin = ($samples.GdiObjects | Measure-Object -Minimum).Minimum
    GdiObjectsMax = ($samples.GdiObjects | Measure-Object -Maximum).Maximum
    UserObjectsMax = ($samples.UserObjects | Measure-Object -Maximum).Maximum
    Samples = $samples
}
$artifactPath = Join-Path $projectRoot 'artifacts'
[IO.Directory]::CreateDirectory($artifactPath) | Out-Null
$safeLabel = $Label -replace '[^a-zA-Z0-9_-]', '_'
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifactPath ($safeLabel + '.json')) -Encoding utf8
$result | Select-Object * -ExcludeProperty Samples | Format-List
