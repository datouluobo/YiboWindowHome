param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Capture', 'Baseline', 'Changed', 'MixedDpi', 'NegativeLayout', 'Disconnected', 'Reconnected', 'Resolution')]
    [string]$Phase,
    [Parameter(Mandatory = $true)][string]$Session,
    [ValidateRange(0, 63)][int]$ScreenIndex = 0,
    [ValidateSet('normal', 'maximized', 'spanning', 'spanning-biased', 'workarea')][string]$WindowState = 'normal'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testExecutable = Join-Path $projectRoot 'build\Release\ManualDisplayTests.exe'
if (-not (Test-Path -LiteralPath $testExecutable)) { throw 'Build ManualDisplayTests first: cmake --build build --config Release --target ManualDisplayTests' }
$sessionDirectory = [IO.Path]::GetFullPath($Session)
[IO.Directory]::CreateDirectory($sessionDirectory) | Out-Null
$phaseNames = @{ Baseline = 'baseline'; Changed = 'changed'; MixedDpi = 'mixed'; NegativeLayout = 'negative'; Disconnected = 'disconnected'; Reconnected = 'reconnected'; Resolution = 'resolution' }
$testArguments = if ($Phase -eq 'Capture') { @('--capture', $sessionDirectory, $ScreenIndex, $WindowState) } else { @('--restore', $sessionDirectory, $ScreenIndex, $phaseNames[$Phase]) }
$output = & $testExecutable @testArguments
$testExit = $LASTEXITCODE
if (-not $output) { throw "Test tool returned no report (exit $testExit)." }
$report = ($output -join [Environment]::NewLine) | ConvertFrom-Json
$reportPath = Join-Path $sessionDirectory ($Phase + '-' + [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss-fff') + '.json')
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
$report | Format-List
Write-Output "Evidence: $reportPath"
if ($testExit -eq 3) { Write-Warning 'Prerequisites not met; this phase is NOT a passing hardware test.' }
elseif ($testExit -ne 0) { Write-Warning 'Test failed; preserve the report and isolated capture for diagnosis.' }
exit $testExit
