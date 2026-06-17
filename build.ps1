param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir = Join-Path $root "src"
$sources = @(
    (Join-Path $srcDir "hash_forge.c"),
    (Join-Path $srcDir "hf_core.c"),
    (Join-Path $srcDir "hf_vm.c"),
    (Join-Path $srcDir "hf_score.c"),
    (Join-Path $srcDir "hf_report.c"),
    (Join-Path $srcDir "hf_evolve.c"),
    (Join-Path $srcDir "hf_modes.c"),
    (Join-Path $srcDir "hf_db.c")
)
$tableSrc = Join-Path $root "hash_table\hash_table_tiny.c"
$tableTestSrc = Join-Path $root "hash_table\test_hash_table.c"
$arraySrc = Join-Path $root "dynamic_array\dynamic_array_tiny.c"
$arrayTestSrc = Join-Path $root "dynamic_array\test_dynamic_array.c"
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "hash-forge.exe"
$objDir = ($buildDir -replace "\\", "/") + "/"
$mainObj = Join-Path $buildDir "hash_forge.obj"
$tableTestExe = Join-Path $buildDir "hash_table_tests.exe"
$arrayTestExe = Join-Path $buildDir "dynamic_array_tests.exe"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "Visual Studio Build Tools were not found. Install Visual Studio 2022 Build Tools with the C++ workload."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "Visual Studio C++ Build Tools were not found."
}

$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
    Write-Error "Visual Studio Build Tools are missing VsDevCmd.bat."
}

if ($Config -eq "Release") {
    $clFlags = "/nologo /TC /std:c11 /O2 /Ot /Oi /GL /Gw /Gy /DNDEBUG"
    $linkFlags = "/link /LTCG /OPT:REF /OPT:ICF"
} else {
    $clFlags = "/nologo /TC /std:c11 /Od /Zi /W4"
    $linkFlags = ""
}

$quotedSources = ($sources | ForEach-Object { "`"$_`"" }) -join " "
$cmd = "`"$devCmd`" -arch=x64 >nul && cl $clFlags /Fo`"$objDir`" $quotedSources /Fe:`"$exe`" $linkFlags"
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ((Test-Path $tableSrc) -and (Test-Path $tableTestSrc)) {
    $include = Join-Path $root "hash_table"
    $cmd = "`"$devCmd`" -arch=x64 >nul && cl $clFlags /I `"$include`" /Fo`"$objDir`" `"$tableSrc`" `"$tableTestSrc`" /Fe:`"$tableTestExe`" $linkFlags"
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "Built $tableTestExe with Visual Studio C"
}

if ((Test-Path $arraySrc) -and (Test-Path $arrayTestSrc)) {
    $include = Join-Path $root "dynamic_array"
    $cmd = "`"$devCmd`" -arch=x64 >nul && cl $clFlags /I `"$include`" /Fo`"$objDir`" `"$arraySrc`" `"$arrayTestSrc`" /Fe:`"$arrayTestExe`" $linkFlags"
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "Built $arrayTestExe with Visual Studio C"
}

Write-Host "Built $exe with Visual Studio C"
