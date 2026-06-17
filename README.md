# hash-forge

hash-forge is a tiny native C project for discovering fast 64-bit hash
functions through evolutionary search.

The project will evolve short in-memory VM programs over `key`, `seed`, `hash`,
and scratch registers, score them with hash64-inspired tests, and export the
best candidates as standalone C.

The current implementation is intentionally small: a single native C CLI plus a
small hash-table support module used by tests.

## Toolchain

Compiler:

```txt
Visual Studio C via Visual Studio 2022 Build Tools
```

`build.ps1` locates the installed Build Tools with `vswhere`, opens the x64
developer environment, and invokes `cl`.

Required component:

```txt
Microsoft.VisualStudio.Component.VC.Tools.x86.x64
```

## Build

```powershell
.\build.ps1
```

Debug build:

```powershell
.\build.ps1 -Config Debug
```

The output binary is:

```txt
build\hash-forge.exe
```

## Commands

```powershell
.\build\hash-forge.exe self-test
.\build\hash-forge.exe run --seed 123 --generations 100
.\build\hash-forge.exe export-best
```

`run` keeps the population, scoring state, and candidate programs in memory
during evolution. On completion it writes:

```txt
out\best.c
out\best.txt
out\summary.txt
```

`out\best.c` is standalone C containing the exported winner.

## Smoke Test

```powershell
.\scripts\smoke.ps1
```

The smoke test builds the project, runs self-test, runs the hash-table tests,
and performs a deterministic 100-generation evolution run.
