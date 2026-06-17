$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $root "build.ps1")

$exe = Join-Path $root "build\hash-forge.exe"
& $exe self-test
& $exe run --seed 123 --generations 100
