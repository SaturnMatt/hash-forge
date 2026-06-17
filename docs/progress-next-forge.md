# Next forge progress

This file records implementation evidence for the next hash-forge milestones:
source split, artifact hygiene, and best-hash database.

## Milestone 6: Clean source split

Status: completed.

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

- `e07499e Split hash forge source modules`

## Milestone 7: Artifact hygiene and pruning

Status: in progress.

Intended files:

- `src/hf_core.h`
- `src/hash_forge.c`
- `src/hf_modes.c`
- `scripts/test.ps1`
- `README.md`
- `SPEC.md`
- `docs/progress-next-forge.md`

Plan:

- Add read-only `hash-forge artifacts`.
- Add safe `hash-forge prune` with dry-run default and explicit `--yes` for
  deletion.
- Preserve latest artifacts, champion records, champion-referenced archives,
  history, current best/report/summary files, and anything outside the chosen
  `out/` tree.
- Preserve report/export archive pairs together.
- Add a test-only-friendly `--out-dir out/<fixture>` option so strict tests can
  verify deletion behavior without touching real run archives.

Risks:

- Deletion safety is the main risk; pruning must refuse paths outside `out/`.
- The repo currently has many local generated artifacts, so tests must use a
  fixture directory and not clean the real lab notebook.
- Keep this out of the hash evaluation hot loop.

Verification plan:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Experiment plan:

```txt
.\build\hash-forge.exe artifacts
.\build\hash-forge.exe prune --dry-run --keep-runs 200 --keep-days 14
```

What changed:

- Added `hash-forge artifacts` for read-only inventory of the generated `out/`
  tree.
- Added `hash-forge prune`, defaulting to dry-run and requiring `--yes` for
  actual deletion.
- Added retention controls: `--keep-runs <n>` and `--keep-days <n>`.
- Added `--out-dir out/<fixture>` support for safe fixture testing and future
  scoped cleanup.
- Protected current files, history, champion records, champion-referenced
  report/export pairs, latest pointer files, and latest pointer targets.
- Preserved report/export archive pairs together.
- Added `out/artifacts.md` and `out/prune-plan.md`.
- Updated strict tests, README, and SPEC.

Verification result:

```txt
.\build.ps1
pass

.\scripts\test.ps1
pass, including fixture dry-run and real fixture pruning

git diff --check
pass, with existing CRLF normalization warnings only

.\scripts\smoke.ps1
pass
```

Experiment:

```txt
.\build\hash-forge.exe artifacts
.\build\hash-forge.exe prune --dry-run --keep-runs 200 --keep-days 14
```

Key metrics:

```txt
total_files=1307
total_bytes=4627952
run_archive_files=1272
complete_report_export_pairs=589
orphan_reports=94
orphan_exports=0
champion_records=3
history_rows=946
protected_artifacts=19
prune_groups_scanned=683
prune_delete_candidates=0
prune_files_to_delete=0
prune_bytes_reclaimable=0
```

Fixture pruning proof:

```txt
command=.\build\hash-forge.exe prune --out-dir out/prune-fixture --keep-runs 0 --keep-days 0 --dry-run
result=unprotected pair remained on disk

command=.\build\hash-forge.exe prune --out-dir out/prune-fixture --keep-runs 0 --keep-days 0 --yes
result=unprotected delete_old.md/delete_old.c removed; latest-protected and champion-protected pairs remained
```

Interpretation:

The main `out/` tree currently has many archives, but with a 14-day retention
window nothing is old enough to prune yet. The fixture test proves the important
safety behavior: dry-run does not delete, real deletion requires `--yes`, and
latest/champion-protected artifacts survive even when keep-runs and keep-days
are zero.

Commit:

- pending
