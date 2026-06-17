# hash-forge

hash-forge is a tiny native C project for discovering fast 64-bit hash
functions through evolutionary search.

The project will evolve short in-memory VM programs over `key`, `seed`, `hash`,
and scratch registers, score them with hash64-inspired tests, and export the
best candidates as standalone C.

The current implementation is intentionally small: a single native C CLI plus a
small hash-table support module used by tests.

## Toolchain

Preferred compiler:

```txt
MSYS2 UCRT64 GCC
```

Expected GCC path:

```txt
C:\msys64\ucrt64\bin\gcc.exe
```

If GCC is not installed yet, `build.ps1` falls back to Visual Studio C Build
Tools when available. GCC remains the preferred release toolchain.

If MSYS2 is missing:

```powershell
winget install MSYS2.MSYS2
```

Then from the MSYS2 UCRT64 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
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
