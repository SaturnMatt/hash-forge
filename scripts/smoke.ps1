$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $root "build.ps1")

$exe = Join-Path $root "build\hash-forge.exe"
$tableTests = Join-Path $root "build\hash_table_tests.exe"
$arrayTests = Join-Path $root "build\dynamic_array_tests.exe"
& $exe self-test
& $tableTests
& $arrayTests
& $exe run --seed 123 --generations 100
