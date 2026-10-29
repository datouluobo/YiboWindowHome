$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Install Visual Studio C++ desktop build tools and CMake.' }
    $cmakePath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
& $cmakePath -S $projectRoot -B (Join-Path $projectRoot 'build') -A x64
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmakePath --build (Join-Path $projectRoot 'build') --config Release
if ($LASTEXITCODE) { throw 'Build failed.' }
& (Join-Path $projectRoot 'build\Release\CaptionTests.exe')
if ($LASTEXITCODE) { throw 'Caption geometry tests failed.' }
& (Join-Path $projectRoot 'build\Release\VisualCaptionTests.exe')
if ($LASTEXITCODE) { throw 'Visual caption tests failed.' }
& (Join-Path $projectRoot 'build\Release\CaptionStyleTests.exe')
if ($LASTEXITCODE) { throw 'Caption style tests failed.' }
& (Join-Path $projectRoot 'build\Release\ScreenshotCaptionTests.exe')
if ($LASTEXITCODE) { throw 'Uploaded screenshot regression tests failed.' }
& (Join-Path $projectRoot 'build\Release\DisplayTests.exe')
if ($LASTEXITCODE) { throw 'Display restoration tests failed.' }
$migrationPath = Join-Path $projectRoot ('build\state-migration-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
& (Join-Path $projectRoot 'build\Release\StateTests.exe') $migrationPath
if ($LASTEXITCODE) { throw 'State migration tests failed.' }
$rulePath = Join-Path $projectRoot ('build\application-rules-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
& (Join-Path $projectRoot 'build\Release\RuleTests.exe') $rulePath
if ($LASTEXITCODE) { throw 'Application rule tests failed.' }
$settingsPath = Join-Path $projectRoot ('build\native-settings-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
& (Join-Path $projectRoot 'build\Release\SettingsTests.exe') $settingsPath
if ($LASTEXITCODE) { throw 'Native settings tests failed.' }
& $cmakePath --install (Join-Path $projectRoot 'build') --config Release --prefix (Join-Path $projectRoot 'dist')
if ($LASTEXITCODE) { throw 'Packaging failed.' }
$testExe = Join-Path $projectRoot 'dist\YiboWindowHome.exe'
$testPath = Join-Path $projectRoot ('build\test-state-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
$testProcess = Start-Process -FilePath $testExe -ArgumentList @('--self-test', ('"' + $testPath + '"')) -WindowStyle Hidden -PassThru
if (-not $testProcess.WaitForExit(45000)) { throw 'Prototype test timed out.' }
Get-Content -LiteralPath (Join-Path $testPath 'results.txt')
if ($testProcess.ExitCode) { throw 'Prototype integration tests failed.' }
Get-Item -LiteralPath $testExe | Select-Object FullName, Length
