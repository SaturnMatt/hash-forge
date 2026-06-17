# Next forge progress

This file records implementation evidence for the next hash-forge milestones:
source split, artifact hygiene, and best-hash database.

## Milestone 6: Clean source split

Status: in progress.

Intended files:

- `src/hash_forge.c`
- `src/hf_core.h`
- `src/hf_vm.c`
- `src/hf_score.c`
- `src/hf_evolve.c`
- `src/hf_report.c`
- `src/hf_modes.c`
- `build.ps1`
- `build.bat`
- `docs/progress-next-forge.md`
- `docs/next-forge-milestones.md`

Plan:

- Split the large source file mechanically into a few implementation modules.
- Keep scoring, evolution, CLI behavior, report schemas, and output artifacts
  unchanged.
- Put shared constants, structs, and cross-module declarations in `hf_core.h`.
- Keep `src/hash_forge.c` focused on CLI parsing and command dispatch.
- Update the simple Visual Studio build scripts to compile direct source lists.

Risks:

- The current file uses many `static` helpers, so the split needs careful
  linkage cleanup without accidentally changing behavior.
- This is a refactor-only milestone; deterministic output must match before and
  after.
- Existing unrelated `dynamic_array/dynamic_array_tiny.c` changes must remain
  untouched.

Verification plan:

```txt
.\build.ps1
.\build\hash-forge.exe run --seed 123 --generations 100 --threads 1 --no-champions
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Pre-split deterministic reference:

```txt
command=.\build\hash-forge.exe run --seed 123 --generations 100 --threads 1 --no-champions
id=c9679374aaa952e5
quick=254272
deep=2746063
flags=0x0
improvement_count=10
total_candidates=25641
export_selection=deep-seen
```

What changed:

- Split the former `src/hash_forge.c` monolith into direct C modules:
  - `src/hash_forge.c`: CLI parsing and dispatch.
  - `src/hf_core.h`: shared constants, structs, and cross-module declarations.
  - `src/hf_core.c`: console, timing, names, PRNG, and small shared helpers.
  - `src/hf_vm.c`: VM execution, candidate ids, generation, mutation, and
    crossover.
  - `src/hf_score.c`: scoring, candidate ordering, improvements, and export
    selection helpers.
  - `src/hf_report.c`: printing, report/export artifacts, champion flat files,
    and self-test fixture candidates.
  - `src/hf_evolve.c`: self-tests, scoring worker pool, and evolution run loop.
  - `src/hf_modes.c`: compare, policy, bench, baselines, champions, and
    history commands.
- Updated `build.ps1` to compile the direct source list with the same Visual
  Studio C release flags.
- Left `build.bat` as the simple wrapper around `build.ps1`.
- Updated README and SPEC source-layout notes.
- Added `docs/next-forge-milestones.md` as the durable spec for Milestones 6-8.

Post-split deterministic check:

```txt
command=.\build\hash-forge.exe run --seed 123 --generations 100 --threads 1 --no-champions
id=c9679374aaa952e5
quick=254272
deep=2746063
flags=0x0
improvement_count=10
total_candidates=25641
export_selection=deep-seen
```

Module sizes after split:

```txt
src/hash_forge.c    488 lines
src/hf_core.c       148 lines
src/hf_vm.c         409 lines
src/hf_score.c      394 lines
src/hf_report.c    1056 lines
src/hf_evolve.c    1069 lines
src/hf_modes.c      994 lines
```

Verification result:

```txt
.\build.ps1
pass

.\build\hash-forge.exe run --seed 123 --generations 100 --threads 1 --no-champions
pass, deterministic output matched pre-split id/scores/flags/counts

.\scripts\test.ps1
pass

git diff --check
pass, with existing CRLF normalization warnings only

.\scripts\smoke.ps1
pass
```

Interpretation:

The split is behavior-preserving for the required deterministic run. The entry
file is now small and command-focused, while the larger implementation is
grouped by VM, scoring, reporting, evolution, and auxiliary modes. The split is
still intentionally boring: no scoring formulas, report schemas, evolution
policy, or CLI contracts changed.

Commit:

- pending
