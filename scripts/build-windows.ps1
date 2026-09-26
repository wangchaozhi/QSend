<#
.SYNOPSIS
Build and test QSend using a Qt 6 MSVC x64 installation.
.EXAMPLE
./scripts/build-windows.ps1 -QtPath C:/Qt/6.8.3/msvc2022_64 -DeployDir ./dist
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtPath,
    [string]$BuildDir,
    [string]$DeployDir,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$qtRoot = (Resolve-Path -LiteralPath $QtPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'QtPath must identify a Qt 6 MSVC x64 installation.'
}
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot 'build' }
$buildPath = [IO.Path]::GetFullPath($BuildDir)
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Install the Visual Studio Desktop development with C++ workload first.' }
$vcvars = Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat'
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCmd) { $cmakeCmd.Source } else { Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' }
$ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
$ninjaPath = if ($ninjaCmd) { $ninjaCmd.Source } else { Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' }
foreach ($tool in @($vcvars, $cmakePath, $ninjaPath)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required build tool not found: $tool" }
}
$testSetting = if ($SkipTests) { 'OFF' } else { 'ON' }
$batchPath = Join-Path $buildPath 'build-msvc.cmd'
$batch = @"
@echo off
chcp 65001 >nul
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
"$cmakePath" -S "$projectRoot" -B "$buildPath" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qtRoot" -DCMAKE_MAKE_PROGRAM="$ninjaPath" -DBUILD_TESTING=$testSetting
if errorlevel 1 exit /b %errorlevel%
"$cmakePath" --build "$buildPath" --parallel
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($batchPath, $batch, [Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c "`"$batchPath`""
if ($LASTEXITCODE -ne 0) { throw "C++ build failed with exit code $LASTEXITCODE" }
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $qtRoot 'bin') + ';' + $previousPath
    if (-not $SkipTests) {
        $ctest = Join-Path (Split-Path $cmakePath) 'ctest.exe'
        & $ctest --test-dir $buildPath --output-on-failure --timeout 60
        if ($LASTEXITCODE -ne 0) { throw 'One or more tests failed.' }
    }
    if ($DeployDir) {
        $deployPath = [IO.Path]::GetFullPath($DeployDir)
        New-Item -ItemType Directory -Force -Path $deployPath | Out-Null
        $appPath = Join-Path $deployPath 'QSend.exe'
        Copy-Item -LiteralPath (Join-Path $buildPath 'QSend.exe') -Destination $appPath
        & (Join-Path $qtRoot 'bin/windeployqt.exe') --release --no-translations $appPath
        if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
        $redistRoot = Join-Path $vsPath 'VC/Redist/MSVC'
        if (Test-Path -LiteralPath $redistRoot) {
            $redist = Get-ChildItem -LiteralPath $redistRoot -Directory |
                Sort-Object {
                    # Hosted images may also contain an alias such as v143.
                    $parsedVersion = $null
                    if ([version]::TryParse($_.Name, [ref]$parsedVersion)) { $parsedVersion }
                    else { [version]'0.0' }
                } -Descending |
                ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
                Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
            if ($redist) { Get-ChildItem -LiteralPath $redist -Filter '*.dll' | Copy-Item -Destination $deployPath }
        }
        foreach ($runtime in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $deployPath $runtime))) {
                throw "Required MSVC runtime was not deployed: $runtime"
            }
        }
        Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $deployPath
        Write-Host "Deploy complete: $appPath"
    }
} finally {
    $env:PATH = $previousPath
}
Write-Host "Build complete: $(Join-Path $buildPath 'QSend.exe')"
