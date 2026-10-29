$p=Get-CimInstance Win32_Process -Filter 'ProcessId=29792'
if ($p -and $p.Name -eq 'powershell.exe' -and $p.CommandLine -like '*resident_sample_20261029.ps1*') {
    Stop-Process -Id 29792 -ErrorAction Stop
}
