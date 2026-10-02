# Configure + build mdlite (x64 Release). Run from anywhere; uses the VS-bundled cmake/ninja.
param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vs   = "C:\Program Files\Microsoft Visual Studio\18\Professional"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Import the x64 MSVC environment.
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
}

# Pin ninja: another ninja earlier on PATH (depot_tools) is broken here.
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
& $cmake -S $root -B "$root\build" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build "$root\build" --config $Config
exit $LASTEXITCODE
