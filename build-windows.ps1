param(
    [Parameter(Mandatory = $true)][string]$QtBin,
    [string]$LibsshRoot = "",
    [string]$CompilerBin = ""
)

$ErrorActionPreference = "Stop"
$projectDirectory = $PSScriptRoot
$workspaceDirectory = Split-Path -Parent $projectDirectory
$buildDirectory = Join-Path $workspaceDirectory ".codex-tmp\serialssh-windows-build"
$distributionDirectory = Join-Path $projectDirectory "dist"
if (!$LibsshRoot) { $LibsshRoot = Join-Path $projectDirectory "lib\windows-x64" }
$QtBin = (Resolve-Path $QtBin).Path
$LibsshRoot = (Resolve-Path $LibsshRoot).Path
$qmake = Join-Path $QtBin "qmake.exe"
$deploy = Join-Path $QtBin "windeployqt.exe"

if (!(Test-Path $qmake) -or !(Test-Path $deploy)) {
    throw "QtBin must contain qmake.exe and windeployqt.exe."
}
if (!(Test-Path (Join-Path $LibsshRoot "include\libssh\libssh.h"))) {
    throw "LibsshRoot must contain include\libssh\libssh.h, lib and bin."
}
if ($CompilerBin) {
    $CompilerBin = (Resolve-Path $CompilerBin).Path
    $env:PATH = "$CompilerBin;$env:PATH"
}
$makeSpec = (& $qmake -query QMAKE_XSPEC).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not query the Qt compiler kit." }
if ($makeSpec -eq "win32-g++") {
    $make = "mingw32-make.exe"
    $makeArguments = @("-j4")
    if (!(Test-Path (Join-Path $LibsshRoot "lib\libssh.dll.a"))) {
        throw "The selected Qt MinGW kit needs lib\libssh.dll.a."
    }
} elseif ($makeSpec -eq "win32-msvc") {
    $make = "nmake.exe"
    $makeArguments = @("/NOLOGO")
    if (!(Test-Path (Join-Path $LibsshRoot "lib\ssh.lib"))) {
        throw "The bundled libssh is MinGW x64. Use a Qt MinGW kit, or supply an MSVC libssh through -LibsshRoot."
    }
} else {
    throw "Unsupported Qt compiler kit: $makeSpec"
}
if (!(Get-Command $make -ErrorAction SilentlyContinue)) {
    throw "Build tool $make was not found. Set -CompilerBin or use the matching developer shell."
}

New-Item -ItemType Directory -Force $buildDirectory | Out-Null
Push-Location $buildDirectory
try {
    & $qmake (Join-Path $projectDirectory "SerialSshLogger.pro") "CONFIG+=release" "CONFIG-=debug" "LIBSSH_ROOT=$LibsshRoot"
    if ($LASTEXITCODE -ne 0) { throw "qmake failed." }
    & $make @makeArguments
    if ($LASTEXITCODE -ne 0) { throw "$make failed." }
} finally {
    Pop-Location
}

$builtExecutable = Join-Path $buildDirectory "release\SerialSshLogger.exe"
if (!(Test-Path $builtExecutable)) { throw "Built executable was not found." }
New-Item -ItemType Directory -Force $distributionDirectory | Out-Null
Copy-Item $builtExecutable $distributionDirectory -Force
$executable = Join-Path $distributionDirectory "SerialSshLogger.exe"
& $deploy --compiler-runtime --dir $distributionDirectory $executable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }
Copy-Item (Join-Path $LibsshRoot "bin\*.dll") $distributionDirectory -Force
if (Test-Path (Join-Path $LibsshRoot "licenses")) {
    Copy-Item (Join-Path $LibsshRoot "licenses") $distributionDirectory -Recurse -Force
}
if (!(Test-Path (Join-Path $distributionDirectory "platforms\qwindows.dll"))) {
    throw "Qt Windows platform plugin is missing from the distribution."
}
Write-Host "Application directory: $distributionDirectory"
