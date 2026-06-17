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
.\build\hash-forge.exe run --seed 123 --generations 100 --no-starter --no-refresh
.\build\hash-forge.exe run --seed 123 --generations 100 --no-champions
.\build\hash-forge.exe run --seed 123 --generations 100 --no-crossover --no-champions
.\build\hash-forge.exe run --seed 123 --generations 100 --no-novelty --no-champions
.\build\hash-forge.exe run --seed 123 --generations 100 --starter-cap 8 --starter-cap-after 10 --no-champions
.\build\hash-forge.exe compare --seed 123 --seeds 3 --generations 25 --threads 4
.\build\hash-forge.exe policy --seed 9201 --seeds 5 --generations 1000 --threads 32 --quality deep
.\build\hash-forge.exe policy --seed-list 9201,9202,9203 --generations 1000 --threads 32 --quality deep
.\build\hash-forge.exe bench --seconds 2 --threads 1,2,4,8,16,32 --quality normal
.\build\hash-forge.exe baselines --seed 123 --quality deep
.\build\hash-forge.exe champions
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
preserve diversity. Runs begin with a tiny compact-starter lane plus random
candidates, so the forge has a known decent mixer family without losing broad
exploration. Clean winners are saved under `out\champions\`, and future runs
load the top clean champions as starter material unless `--no-champions` is
supplied. Duplicate candidate ids are repaired during breeding so a generation
spends less budget rechecking identical programs. On completion it writes:
Opcode generation is lightly biased toward mixing-heavy operations such as
`XOR`, `MUL`, and rotates while keeping every VM operation reachable.
Generated and mutated instructions are repaired to avoid obvious dead forms
such as self-MOV, multiply-by-one constants, and zero ADD/XOR constants.
Candidates are finalized by ensuring a `hash` write and trimming trailing
non-`hash` instructions that cannot affect output.
Use `--threads auto` to run a tiny quick-scoring warmup and select the fastest
observed worker count for that run.
Use `--quality quick|normal|deep` to trade scoring speed for stronger per-candidate
checks. `normal` is the default.
Use `--no-starter` or `--no-refresh` for deterministic A/B runs against the
compact starter lane or adaptive stagnation refresh.
Use `--no-crossover` for deterministic A/B runs that replace the crossover lane
with more mutation children while keeping the same population and immigrant
counts.
Use `--no-novelty` or `--novelty-lane <n>` for A/B runs against the structural
novelty lane. The default novelty lane reserves a small slice of each generation
for children of structurally distinct candidates.
Use `--starter-cap <n>` with `--starter-cap-after <generations>` to limit how
many starter-lineage candidates may occupy survivor slots for breeding after a
warmup period. Raw scores are unchanged; the cap only changes breeding pressure.

```txt
out\best.c
out\best.txt
out\report.md
out\improvements.csv
out\runs\*.md
out\runs\*.c
out\latest_report_path.txt
out\latest_export_path.txt
out\history.csv
out\history.md
out\summary.txt
out\bench.md
out\compare.md
out\policy.md
out\policy.csv
out\baselines.md
out\champions\*.hfch
out\champions.md
```

`out\best.c` is standalone C containing the exported winner. Export removes
instructions that cannot contribute to the returned `hash`.
Define `HASH_FORGE_BEST_TEST_MAIN` when compiling it to build a tiny vector
self-test that verifies the exported C still matches the VM outputs used at
export time.
`out\report.md` is the full human-readable run report, including run settings,
stop reason, scores, decoded fail flags, and the best candidate instruction
listing. Scoring signals include trivial-output, collision, bucket,
avalanche, neighboring-input differential, and key/seed sensitivity checks. The report also records
the best candidate score breakdown, final multi-seed audit, operator histogram,
baseline comparison scores, plus quick, deep, and total candidate hash functions
evaluated during the run. It also records diversity telemetry for the final
scored generation, duplicate-candidate repairs made while breeding, and any
adaptive random-immigrant refreshes triggered by stagnant search. It includes
an improvement timeline showing when the best candidate changed, with generation,
elapsed seconds, source, scores, flags, and total candidates evaluated.
`out\improvements.csv` stores the latest run's improvement timeline in compact
CSV form for quick convergence checks.
`out\runs\*.md` stores archived per-run copies of completed reports, while
`out\latest_report_path.txt` points to the latest archived report.
`out\runs\*.c` stores archived per-run standalone C exports, while
`out\latest_export_path.txt` points to the latest archived export.
`out\history.csv` is a compact append-only index of completed runs for quick
comparison across seeds, qualities, thread counts, scores, and candidate ids.
`history --top <n>` reads that index, ranks the strongest historical runs by
failure severity and score, prints a compact leaderboard, and writes
`out\history.md` with total candidate counts, decoded flag names, and the
starter/refresh policy used by each run.

`bench` measures quick and deep scoring throughput for one or more thread counts
and quality modes, including both candidates/sec and normalized hash evals/sec,
then writes `out\bench.md`. Benchmark rates are machine-local guidance for
choosing thread counts, not hash quality scores.

`compare` runs deterministic short A/B policy trials for default, no-starter,
no-refresh, and bare settings across one or more seeds, prints aggregate wins
and averages, then writes `out\compare.md`.

`policy` runs a fuller deterministic same-budget policy comparison across the
same seed list and fixed generation count. It compares default, no-crossover,
starter-cap, no-novelty, no-starter, no-refresh, bare, and refresh-strong
policies with champion starters disabled for fairness, then writes `out\policy.md` and
`out\policy.csv`.

`baselines` scores established non-cryptographic 64-bit reference mixers with
the same zero, collision, bucket, avalanche, differential, and sensitivity
tests used for evolved candidates, then writes `out\baselines.md`. Use these
rows as lab reference marks: fail flags matter first, then deep score, then
speed and exported instruction count. Matching or beating a baseline is useful
evidence, not cryptographic proof.

`champions` lists saved custom hashes from `out\champions\`, ranks them by the
same ordering used by selection, and writes `out\champions.md`. Champion records
include a scoring fingerprint derived from established baseline scores. If that
fingerprint changes, champion loading and `champions` automatically rescore
saved records before using them.

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
verifies `bench` plus `out/bench.md`, verifies policy comparison output,
verifies established-hash baseline output, verifies champion save/list/load and
fingerprint rescore behavior, verifies history leaderboard output, and compiles
`out/best.c` independently.

`smoke.ps1` is a compatibility entry point that runs the same strict suite.

`hash-forge self-test` includes VM instruction checks, score calibration for
constant/key-only/seed-only/xor-only bad hashes, a baseline mixer comparison,
differential-score calibration, compact-starter calibration, and
generator/mutation invariants.
