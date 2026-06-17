param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "src\hash_forge.c"
$tableSrc = Join-Path $root "hash_table\hash_table.c"
$tableTestSrc = Join-Path $root "hash_table\test_hash_table.c"
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "hash-forge.exe"
$tableTestExe = Join-Path $buildDir "hash_table_tests.exe"

$candidates = @()
if ($env:CC) {
    $candidates += $env:CC
}
$candidates += "C:\msys64\ucrt64\bin\gcc.exe"
$candidates += "gcc.exe"

$gcc = $null
foreach ($candidate in $candidates) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
        $gcc = $cmd.Source
        break
    }
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

if ($gcc) {
    if ($Config -eq "Release") {
        $flags = @("-std=c11", "-O3", "-march=native", "-flto", "-fomit-frame-pointer", "-DNDEBUG")
    } else {
        $flags = @("-std=c11", "-O0", "-g", "-Wall", "-Wextra")
    }

    & $gcc @flags $src "-o" $exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ((Test-Path $tableSrc) -and (Test-Path $tableTestSrc)) {
        & $gcc @flags "-Wall" "-Wextra" "-I" (Join-Path $root "hash_table") $tableSrc $tableTestSrc "-o" $tableTestExe
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        Write-Host "Built $tableTestExe with GCC"
    }

    Write-Host "Built $exe with GCC"
    exit 0
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "GCC was not found. Install MSYS2 UCRT64 GCC, expected at C:\msys64\ucrt64\bin\gcc.exe."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "GCC was not found, and no Visual Studio C++ fallback was found."
}

$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
    Write-Error "Visual Studio fallback is missing VsDevCmd.bat."
}

if ($Config -eq "Release") {
    $clFlags = "/nologo /TC /std:c11 /O2 /DNDEBUG"
} else {
    $clFlags = "/nologo /TC /std:c11 /Od /Zi /W4"
}

$cmd = "`"$devCmd`" -arch=x64 >nul && cl $clFlags `"$src`" /Fe:`"$exe`""
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ((Test-Path $tableSrc) -and (Test-Path $tableTestSrc)) {
    $include = Join-Path $root "hash_table"
    $cmd = "`"$devCmd`" -arch=x64 >nul && cl $clFlags /I `"$include`" `"$tableSrc`" `"$tableTestSrc`" /Fe:`"$tableTestExe`""
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "Built $tableTestExe with Visual Studio C fallback"
}

Write-Host "Built $exe with Visual Studio C fallback; install MSYS2 UCRT64 GCC for the preferred toolchain."
