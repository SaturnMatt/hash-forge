# hash-forge

hash-forge is a tiny native C project for discovering fast 64-bit hash
functions through evolutionary search.

The project will evolve short in-memory VM programs over `key`, `seed`, `hash`,
and scratch registers, score them with hash64-inspired tests, and export the
best candidates as standalone C.

This repository is intentionally starting small. The current tree is the project
environment and scaffold for the first implementation pass.

## Toolchain

Preferred compiler:

```txt
MSYS2 UCRT64 GCC
```

Expected GCC path:

```txt
C:\msys64\ucrt64\bin\gcc.exe
```

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

## Current Scaffold Commands

```powershell
.\build\hash-forge.exe self-test
.\build\hash-forge.exe run --seed 123 --generations 100
```

The scaffold is not the full engine yet. See [SPEC.md](SPEC.md) for the v1
target.
