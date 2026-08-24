$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

$cmakeCommand = $null
$cmakeInPath = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeInPath) {
    $cmakeCommand = $cmakeInPath.Source
}

if (-not $cmakeCommand) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstallPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
        $vsCMake = Join-Path $vsInstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $vsCMake) {
            $cmakeCommand = $vsCMake
        }
    }
}

if (-not $cmakeCommand) {
    throw "CMake is required. Install Visual Studio Build Tools 2022 with 'Desktop development with C++' and CMake."
}

& $cmakeCommand -S $root -B $build -G "Visual Studio 17 2022" -A x64
& $cmakeCommand --build $build --config Release

$exe = Join-Path $build "Release\Liberty.exe"
if (-not (Test-Path $exe)) { throw "Build completed without Liberty.exe." }
Copy-Item $exe (Join-Path $root "Liberty.exe") -Force
Write-Host "Built: $(Join-Path $root 'Liberty.exe')"

