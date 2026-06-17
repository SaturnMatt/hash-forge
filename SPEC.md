# hash-forge specification

## Summary

hash-forge is a tiny native C project for discovering fast 64-bit hash
functions through evolutionary search.

The program evolves small hash programs in memory. Each candidate is a short
sequence of simple integer operations over `key`, `seed`, `hash`, and a few
scratch registers. Candidates run inside a compact VM/evaluator, are scored
against hash-quality tests inspired by the earlier `hash64` experiment, and the
best candidates are mutated into future generations.

The first real implementation should be small, fast, deterministic when given a
seed, and useful as a local experiment harness. It should not depend on JIT
compilation, generated batch compilation, external databases, or a large build
system.

## Core Goals

- Discover high-quality 64-bit hash functions for `(uint64_t key, uint64_t seed)`.
- Keep the hot loop in RAM: candidate programs, populations, scores, and test
  state live in process memory during a run.
- Make speed a first-class design constraint.
- Keep the codebase tiny and readable.
- Preserve the spirit of hash64 without copying its implementation.
- Support future multithreading without requiring it in v1.
- Export winning candidates as plain C functions.

## Non-Goals For V1

- No JIT compiler.
- No generated batch C compilation loop.
- No SQLite or durable database.
- No GUI.
- No cryptographic hash claims.
- No cross-platform abstraction layer beyond ordinary C where convenient.

## Language And Build

- Language: C11.
- Platform: native Windows x64.
- Compiler: Visual Studio C through the local VS 2022 Build Tools.
- Build style: simple `build.ps1`; no CMake until needed.

Release flags:

```txt
/nologo /TC /std:c11 /O2 /DNDEBUG
```

Debug flags:

```txt
/nologo /TC /std:c11 /Od /Zi /W4
```

## Candidate Hash Model

Each candidate is a small VM program with this logical shape:

```c
uint64_t candidate(uint64_t key, uint64_t seed);
```

VM registers:

```txt
0: key
1: seed
2: hash
3: a
4: b
```

Initial state:

```txt
key  = input key
seed = input seed
hash = 0
a    = 0
b    = 0
```

Return value:

```txt
hash
```

V1 operation set:

```txt
MOV   dst = operand
ADD   dst += operand
MUL   dst *= operand
XOR   dst ^= operand
SHL   dst <<= imm_shift
SHR   dst >>= imm_shift
ROTL  dst = rotl64(dst, imm_shift)
ROTR  dst = rotr64(dst, imm_shift)
```

Operand forms:

```txt
register operand: one of key, seed, hash, a, b
constant operand: uint64_t immediate
shift operand: integer 1..63
```

V1 generator constraints:

- Candidate length defaults to 8..16 instructions.
- `MUL` constants should usually be odd.
- Shift and rotate amounts must be 1..63.
- Bias toward operations that affect `hash`.
- Reject or heavily penalize candidates that never write `hash`.

## VM And Runtime

The evaluator should be a small function that executes one candidate for one
`key, seed` pair.

Design priorities:

- No heap allocation in the evaluator.
- No file I/O in scoring hot loops.
- No function pointers inside the per-instruction loop.
- Use `uint64_t` unsigned overflow semantics.
- Keep register storage fixed-size, such as `uint64_t r[5]`.
- Mark tiny helpers `static inline`.
- Keep rotates well-defined for shifts 1..63.

The first implementation can use a switch over opcode. Later profiling can
decide whether a threaded interpreter or specialized evaluator is worthwhile.

## Randomness And Reproducibility

Runs must be deterministic when given a seed.

Use a fast local PRNG such as SplitMix64 or xoshiro. Do not use `rand()`.

Requirements:

- CLI accepts a run seed.
- Candidate generation and mutation derive from that seed.
- Test input generation derives from deterministic per-test/per-candidate seeds.
- Re-running the same version with the same seed and settings should produce
  the same sequence of candidates and scores.

## Evolution Loop

V1 uses a threaded continuous evolutionary loop.

Default parameters:

```txt
population_size: 256
survivor_count: 32
mutations_per_child: 1..3
crossover_children_per_generation: 16
random_immigrants_per_generation: 8
instruction_count: 8..16
status_interval_ms: 1000
deep_score_interval_generations: 25
deep_score_top_n: 8
```

Loop:

1. Initialize a population of random candidates.
2. Replace a tiny starter lane with a compact baseline mixer and deterministic
   mutations of it.
3. Quick-score every candidate.
4. Sort/rank by quick score, fail flags, instruction count, and speed.
5. Keep the top survivors.
6. Fill the rest of the population with mutated children of survivors.
7. Reserve a small lane for crossover children from survivor pairs.
8. Reserve a small tail for fresh random immigrants.
9. Repair duplicate candidate ids in the next generation through extra mutation
   or fresh random candidates.
10. Periodically deep-score current leaders.
11. Print compact live status.
12. Continue until stopped or until an optional generation limit is reached.

Mutation actions:

```txt
change opcode
change destination register
change operand register
change constant
change shift amount
swap two instructions
insert instruction, if below max length
remove instruction, if above min length
```

Opcode generation may be lightly biased toward useful mixing operations such as
XOR, MUL, and rotates, but every VM opcode must remain reachable and covered by
self-test invariants.
Instruction generation and mutation should repair obvious dead forms such as
self-MOV, multiply-by-one constants, and zero ADD/XOR constants.

## Test Suite

The tests are inspired by hash64's coverage style, but implemented fresh.

### Zero Behavior

Probe small special cases:

```txt
hash(0, 0)
hash(0, 1)
hash(1, 0)
hash(1, 1)
```

Fail or penalize repeated trivial outputs and obvious identity values.

### Collision Patterns

Score collision count across mixed inputs:

```txt
hash(0, random)
hash(random, 0)
hash(constant, random)
hash(random, constant)
hash(random1, random2)
hash(random2, random1)
hash(seq, 0)
hash(0, seq)
hash(seq1, seq2)
hash(seq2, seq1)
recursive: h = hash(h, seed)
recursive: h = hash(key, h)
recursive: h = hash(h, h)
```

### Bucket Distribution

For bucket counts 2..64, feed sequential keys and sequential seeds into the
candidate and count `hash % bucket_count`.

Variants:

```txt
key changes, seed fixed
seed changes, key fixed
```

### Avalanche

For each input bit, flip that bit and compare output difference.

Variants:

```txt
key bit flipped, random inputs
seed bit flipped, random inputs
key bit flipped, sequential inputs
seed bit flipped, sequential inputs
both key and seed bit flipped
```

Score how close output bit flips are to 50%.

### Neighbor Differentials

For neighboring inputs, compare the two output hashes:

```txt
hash(key, seed) vs hash(key + 1, seed)
hash(key, seed) vs hash(key, seed + 1)
hash(key, seed) vs hash(key + 1, seed + 1)
```

Score output XOR popcount near 50% and check that low bits of the output
difference do not collapse into a tiny number of buckets. Penalize zero
differences heavily.

### Speed Telemetry

Run a tight loop over candidate evaluation and time it. Use speed as telemetry
and tie-breaker rather than the primary quality score in v1.

## CLI

V1 target commands:

```txt
hash-forge self-test
hash-forge run --seed 123
hash-forge run --seed 123 --generations 1000
hash-forge run --seed 123 --seconds 60
hash-forge run --seed 123 --generations 1000 --seconds 60
hash-forge run --seed 123 --seconds 60 --threads 8
hash-forge run --seed 123 --seconds 60 --threads auto
hash-forge run --seed 123 --seconds 60 --quality deep
hash-forge run --seed 123 --generations 100 --no-starter --no-refresh
hash-forge compare --seed 123 --seeds 3 --generations 25 --threads 4
hash-forge bench --seconds 2 --threads 1,2,4,8,16,32 --quality normal
hash-forge history --top 10
hash-forge export-best
```

`run` starts evolution, prints live status, keeps the active population in RAM,
and writes the best candidate on normal exit or interrupt. If both `--generations`
and `--seconds` are provided, the run stops when either limit is reached.
Scoring is split across worker threads by default, using the machine's processor
count capped at 32. `--threads <n>` pins a run to a specific worker count.
Worker threads and per-thread scratch buffers are created once at run start and
reused for the whole run.
`--threads auto` runs a tiny quick-scoring warmup on the generated population and
selects the fastest observed worker count for that run.
`--quality quick|normal|deep` controls score iteration counts, deep-score cadence,
and deep-score leader count. `normal` is the default.
`--no-starter` and `--no-refresh` disable the compact starter lane and adaptive
stagnation refresh for deterministic comparison runs.

`compare` runs deterministic short policy trials for default, no-starter,
no-refresh, and bare settings across one or more seeds. It should print a
compact table and write `out/compare.md`.

`self-test` checks intentionally bad hashes and baseline mixers so the test
suite can prove it rejects obvious failures.

`bench` measures scoring throughput for one or more thread counts and quality
modes. It should time quick and deep scoring separately, print candidates/sec,
and write `out/bench.md`. Benchmark results are machine-local tuning guidance,
not hash quality scores.

`history --top <n>` reads `out/history.csv`, ranks completed runs by failure
severity, deep score, quick score, total candidates evaluated, and recency,
prints a compact leaderboard, and writes `out/history.md` with total candidate
counts, decoded flag names, and starter/refresh policy columns.

## Output And Persistence

The hot loop should not depend on disk.

During a normal run:

- Population lives in RAM.
- Scores live in RAM.
- Test scratch buffers live in RAM.
- Status goes to stdout.

Durable output should be minimal and explicit:

```txt
out/best.c
out/best.txt
out/report.md
out/runs/*.md
out/runs/*.c
out/latest_report_path.txt
out/latest_export_path.txt
out/history.csv
out/history.md
out/summary.txt
out/bench.md
out/compare.md
```

`best.c` should be a standalone exported C function, independent of the VM.
It may include an optional `HASH_FORGE_BEST_TEST_MAIN` vector-test entry point
so tests can compile and run the exported C against VM-derived expected outputs.
`report.md` should be the full human-readable report for the completed run,
including quick, deep, and total candidate hash functions evaluated, decoded
fail flags, a quick/deep score breakdown, a final multi-seed audit, plus a
best-candidate operator histogram and baseline comparison scores. It should
also include diversity telemetry: final scored-generation uniqueness, duplicate
repairs, fresh random replacements, stagnation refreshes, and extra adaptive
random immigrants.
The latest report remains at `out/report.md`; completed runs should also archive
a copy under `out/runs/`, with `out/latest_report_path.txt` pointing to it.
The latest export remains at `out/best.c`; completed runs should also archive a
copy under `out/runs/`, with `out/latest_export_path.txt` pointing to it.
Completed runs should append one compact metrics row to `out/history.csv`.
`out/history.md` should be a human-readable leaderboard generated by the
history command for quick comparison of previous runs.

## Multithreading

Candidate scoring is the first threaded hot path:

- Split candidate scoring across worker threads.
- Each worker owns scratch buffers.
- Worker threads persist for the whole run.
- No shared writes in the hot test loop.
- Main thread merges scores after workers finish.

Avoid global mutable test state.

## First Milestone

The first useful milestone is:

```txt
hash-forge self-test
hash-forge run --seed 123 --generations 100
```

Acceptance criteria:

- Project builds with one command.
- Self-test passes.
- A seeded short run is deterministic.
- Evolution prints live status.
- At least one best candidate is exported to `out/best.c`.
- The exported candidate compiles as plain C.
- The implementation remains small enough to understand in one sitting.

## Test Strategy

`hash-forge self-test` should cover the core in-process invariants:

- VM instruction behavior for MOV, ADD, MUL, XOR, shifts, and rotates.
- Score calibration for constant, key-only, seed-only, xor-only, and baseline
  mixer candidates, including the compact starter and neighboring-input
  differential signal.
- Generator and mutation invariants: instruction bounds, valid opcodes and
  registers, hash writes, stable ids, and deterministic seeded behavior.

`scripts/test.ps1` is the strict executable-level suite. It should build the
project, run self-test and support tests, assert CLI error output, compare
seeded generation-limited runs across single-threaded and threaded scoring,
stress worker counts including default and over-cap values, verify report and
summary contents, check auto-thread selection, check time-limited run contracts,
check quality modes, verify benchmark output and `out/bench.md`, verify history
leaderboard output and `out/history.md`, verify policy comparison output and
`out/compare.md`, and compile `out/best.c` as standalone C.

`scripts/smoke.ps1` should remain a convenient entry point to the strict suite
so the familiar smoke command verifies outputs, not just process exit codes.
