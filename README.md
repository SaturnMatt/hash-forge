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
.\build\hash-forge.exe run --seed 123 --seconds 60
.\build\hash-forge.exe run --seed 123 --seconds 60 --threads 8
.\build\hash-forge.exe run --seed 123 --seconds 60 --threads auto
.\build\hash-forge.exe run --seed 123 --seconds 60 --quality deep
.\build\hash-forge.exe bench --seconds 2 --threads 1,2,4,8,16,32 --quality normal
.\build\hash-forge.exe history --top 10
.\build\hash-forge.exe export-best
```

`run` keeps the population, scoring state, and candidate programs in memory
during evolution. Scoring is split across worker threads by default, using the
machine's processor count capped at 32. Use `--threads <n>` to pin a run to a
specific worker count. Worker threads and per-thread scratch buffers are created
once at run start and reused for the whole run. Each generation keeps the top
survivors, mutates most of the remaining population, recombines a small
crossover lane from survivor pairs, and injects fresh random immigrants to
preserve diversity. On completion it writes:
Opcode generation is lightly biased toward mixing-heavy operations such as
`XOR`, `MUL`, and rotates while keeping every VM operation reachable.
Generated and mutated instructions are repaired to avoid obvious dead forms
such as self-MOV, multiply-by-one constants, and zero ADD/XOR constants.
Use `--threads auto` to run a tiny quick-scoring warmup and select the fastest
observed worker count for that run.
Use `--quality quick|normal|deep` to trade scoring speed for stronger per-candidate
checks. `normal` is the default.

```txt
out\best.c
out\best.txt
out\report.md
out\runs\*.md
out\runs\*.c
out\latest_report_path.txt
out\latest_export_path.txt
out\history.csv
out\history.md
out\summary.txt
out\bench.md
```

`out\best.c` is standalone C containing the exported winner.
Define `HASH_FORGE_BEST_TEST_MAIN` when compiling it to build a tiny vector
self-test that verifies the exported C still matches the VM outputs used at
export time.
`out\report.md` is the full human-readable run report, including run settings,
stop reason, scores, decoded fail flags, and the best candidate instruction
listing. Scoring signals include trivial-output, collision, bucket,
avalanche, and neighboring-input differential checks. The report also records
the best candidate score breakdown, final multi-seed audit, operator histogram,
baseline comparison scores, plus quick, deep, and total candidate hash functions
evaluated during the run.
`out\runs\*.md` stores archived per-run copies of completed reports, while
`out\latest_report_path.txt` points to the latest archived report.
`out\runs\*.c` stores archived per-run standalone C exports, while
`out\latest_export_path.txt` points to the latest archived export.
`out\history.csv` is a compact append-only index of completed runs for quick
comparison across seeds, qualities, thread counts, scores, and candidate ids.
`history --top <n>` reads that index, ranks the strongest historical runs by
failure severity and score, prints a compact leaderboard, and writes
`out\history.md`.

`bench` measures quick and deep candidate scoring throughput for one or more
thread counts and quality modes, then writes `out\bench.md`. Benchmark rates
are machine-local guidance for choosing thread counts, not hash quality scores.

## Tests

```powershell
.\scripts\test.ps1
.\scripts\smoke.ps1
```

`test.ps1` is the strict verification path. It builds the project, runs the
expanded `self-test`, runs support-module tests, checks CLI error handling,
compares deterministic 100-generation runs across `--threads 1` and
`--threads 4`, stress-checks worker counts including the default and over-cap
values, verifies `--threads auto`, verifies `out/report.md` and
`out/summary.txt`, checks quality modes, checks time-limited run contracts,
verifies `bench` plus `out/bench.md`, verifies history leaderboard output, and
compiles `out/best.c` independently.

`smoke.ps1` is a compatibility entry point that runs the same strict suite.

`hash-forge self-test` includes VM instruction checks, score calibration for
constant/key-only/seed-only/xor-only bad hashes, a baseline mixer comparison,
differential-score calibration, and generator/mutation invariants.
