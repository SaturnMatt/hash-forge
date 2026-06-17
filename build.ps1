param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "src\hash_forge.c"
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "hash-forge.exe"

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

if (-not $gcc) {
    Write-Error "GCC was not found. Install MSYS2 UCRT64 GCC, expected at C:\msys64\ucrt64\bin\gcc.exe."
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

if ($Config -eq "Release") {
    $flags = @("-std=c11", "-O3", "-march=native", "-flto", "-fomit-frame-pointer", "-DNDEBUG")
} else {
    $flags = @("-std=c11", "-O0", "-g", "-Wall", "-Wextra")
}

& $gcc @flags $src "-o" $exe
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built $exe"
