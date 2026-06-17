# Next hash-forge milestone specs

These milestones follow the completed ultimate-forge milestone set. They are
about keeping the project pleasant to work on as it grows, controlling generated
artifact sprawl, and turning discovered hashes into a durable searchable lab
asset.

Keep the standing project constraints:

- C11, native Windows x64, Visual Studio C build.
- Keep the hot loop free of disk I/O and avoid heap churn inside generation
  scoring and breeding.
- Preserve deterministic seeded runs.
- Keep `hash-forge` small enough that a single developer can understand it.
- Run real verification and commit after each milestone.

## Milestone 6: Clean source split

### Problem

`src/hash_forge.c` has become a productive but large monolith. It now contains
the VM, scoring, evolution, reporting, policy mode, benchmark mode, baseline
mode, champion persistence, history parsing, CLI parsing, and command dispatch.
That is still workable, but every future feature now asks one file to carry too
much context.

The danger is not compile time. The danger is accidental coupling: reporting
changes can disturb scoring code, champion work can disturb CLI parsing, and
small reviews become harder than they need to be.

### Goal

Split the C source into a small number of boring modules without changing user
behavior, scoring behavior, CLI output contracts, or generated artifacts.

This milestone is a refactor milestone. It should not add new user-facing
features.

### Proposed file layout

Keep the split conservative:

```txt
src/hash_forge.c          command dispatch and main
src/hf_core.h             shared constants, enums, structs, public helpers
src/hf_vm.c               VM execution, candidate ids, instruction utilities
src/hf_score.c            score_subject, score_candidate, baseline scoring core
src/hf_evolve.c           candidate generation, mutation, crossover, run loop
src/hf_report.c           best export, reports, history artifacts
src/hf_modes.c            compare, policy, bench, baselines, champions, history
```

If that feels too wide during implementation, prefer fewer modules:

```txt
src/hf_core.h
src/hf_engine.c
src/hf_reports.c
src/hf_modes.c
src/hash_forge.c
```

Avoid a forest of tiny files.

### Scope

- Move code mechanically first.
- Keep type and function names stable unless a rename is required by linkage.
- Use `static` for file-local helpers after the split.
- Put only genuinely shared declarations in `hf_core.h`.
- Keep build scripts simple; update `build.ps1` and `build.bat` with direct
  source file lists rather than adding CMake.
- Do not touch scoring formulas, evolution policy, or report schemas.
- Do not touch `dynamic_array` or `hash_table` except build-script source lists
  if necessary.

### Non-goals

- No scoring redesign.
- No database work.
- No artifact cleanup behavior.
- No public CLI changes.
- No large naming/style rewrite.

### Verification

Run:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Also run a deterministic equivalence check before and after the split if done in
one branch:

```txt
.\build\hash-forge.exe run --seed 123 --generations 100 --threads 1 --no-champions
```

Record the pre/post best id, quick score, deep score, flags, improvement count,
and total candidates. They should match for a pure source split.

### Acceptance criteria

1. The project builds with the same one-command Visual Studio flow.
2. Strict tests and smoke tests pass.
3. Deterministic generation-limited output matches the pre-split behavior.
4. The new file boundaries are obvious and boring.
5. `src/hash_forge.c` becomes a small command entry file rather than the whole
   program.

## Milestone 7: Artifact hygiene and pruning

### Problem

`out/` is now useful but noisy. Long testing creates many archived reports and
exports under `out/runs/`, plus latest files, history, policy reports,
champions, benchmark reports, and baseline reports. This is good evidence, but
without hygiene it becomes hard to find the meaningful artifacts and easy to
waste disk space.

Generated artifacts should be treated like a lab notebook with retention rules,
not like an unmanaged pile.

### Goal

Add explicit artifact inventory and pruning commands that keep important
discoveries while safely removing low-value generated run artifacts.

### Proposed CLI

```txt
hash-forge artifacts
hash-forge prune --dry-run
hash-forge prune --keep-runs 200 --keep-days 14
hash-forge prune --keep-runs 200 --keep-days 14 --yes
```

`artifacts` should be read-only. `prune` should default to dry-run unless
`--yes` is provided.

### Retention rules

Never prune:

- `out/best.c`
- `out/best.txt`
- `out/report.md`
- `out/summary.txt`
- `out/history.csv`
- `out/champions.md`
- `out/champions/*.hfch`
- any archived report/export referenced by a champion record
- `out/latest_report_path.txt`
- `out/latest_export_path.txt`
- the report/export pair referenced by the latest pointers

Prunable by default:

- old `out/runs/*.md`
- old `out/runs/*.c`
- stale latest-style generated reports that can be recreated

The command should preserve report/export pairs together. If a run has both
`.md` and `.c`, prune or keep both as one unit.

### Reporting

`hash-forge artifacts` should print:

- number of run archive files,
- number of complete report/export pairs,
- number of orphan reports,
- number of orphan exports,
- total bytes under `out/`,
- champion count,
- history row count,
- latest report path,
- latest export path,
- protected artifact count.

`hash-forge prune --dry-run` should print:

- files that would be removed,
- bytes that would be reclaimed,
- files protected and why,
- active retention settings.

Optionally write:

```txt
out/artifacts.md
out/prune-plan.md
```

### Safety

- Refuse to delete anything outside `out/`.
- Resolve paths before deletion.
- Do not use shell glob deletion from C.
- Pruning should be deterministic and explainable.
- Dry-run should be the default.
- `--yes` should be required for deletion.

### Tests

- Create a small temporary `out/runs` fixture or use a test-only command mode if
  simpler.
- Verify `artifacts` reports counts without deleting files.
- Verify `prune --dry-run` deletes nothing.
- Verify `prune --yes` removes only unprotected old archive pairs.
- Verify champion-referenced report/export artifacts are protected.
- Verify latest report/export pointers are protected.

### Acceptance criteria

1. Users can see what `out/` contains with one command.
2. Users can preview pruning safely.
3. Real pruning requires explicit `--yes`.
4. Champion and latest artifacts are never removed.
5. Strict tests and smoke tests pass.

## Milestone 8: Best hash database

### Problem

Champion flat files are a good start, but hash-forge needs a stronger memory of
its discoveries. We want a growing database of the best custom hashes, their
scores, source runs, exported code paths, policy settings, and scoring-system
fingerprint.

When established baseline scores change, hash-forge should recognize that the
scoring system changed and automatically rescore saved custom hashes before
ranking or using them.

### Goal

Build a small local best-hash database that becomes the canonical index of
custom discoveries while keeping existing champion files compatible.

SQLite is allowed and recommended if it stays simple. Use the system or bundled
SQLite only if available cleanly; otherwise vendor a tiny amalgamation only if
the repository impact is acceptable. A plain append-only file is acceptable as a
fallback, but SQLite is the preferred design because queries and rescoring state
will matter.

### Proposed CLI

```txt
hash-forge db init
hash-forge db import-champions
hash-forge db add-latest
hash-forge db top --limit 20
hash-forge db rescore
hash-forge db verify
```

Optional convenience behavior:

- successful clean runs automatically upsert into the database,
- `champions` reads from the database when present, falling back to flat files.

### Proposed database

Default path:

```txt
out/hash-forge.db
```

Core tables:

```txt
schema_version(version, applied_at)
score_fingerprint(id, created_at, fingerprint, baseline_summary)
hash_candidate(id, candidate_id, instruction_hash, instruction_count, source,
               first_seen_at, last_seen_at, best_report_path, best_export_path)
candidate_score(id, candidate_id, fingerprint, seed, quality, quick_score,
                deep_score, fail_flags, audit_worst, audit_avg, audit_flags,
                run_generation, total_candidates, policy_name, created_at)
candidate_instruction(candidate_id, idx, op, dst, operand_kind, operand_reg,
                      shift, constant)
run_record(id, seed, quality, threads, generations, seconds, elapsed_seconds,
           stop_reason, total_candidates, best_candidate_id, report_path,
           export_path, created_at)
```

Keep schema names plain and stable. Do not over-normalize.

### Fingerprint behavior

Compute a scoring fingerprint from established baseline scores and scoring
settings. This should match the spirit of current champion fingerprints.

On `db top`, `champions`, or automatic run startup:

- read current fingerprint,
- compare it to saved candidate score fingerprints,
- if mismatched, mark rows stale,
- rescore saved candidates before ranking or loading them as starters,
- store new score rows under the new fingerprint.

Do not silently rank stale scores as current.

### Candidate import and export

- Import existing `out/champions/*.hfch`.
- Import latest `out/best.c` only if the VM instruction record is available via
  `out/best.txt`, champion file, or report.
- Prefer storing VM instructions, not generated C text, as the canonical
  candidate representation.
- Keep `out/best.c` as the canonical standalone C export for the latest run.

### Ranking

Rank candidates by:

1. fail severity,
2. fail flags,
3. audit flags,
4. deep score,
5. audit worst,
6. quick score,
7. shorter exported instruction count,
8. recency as final tie-break.

Reports should make clear which scores are current fingerprint scores.

### Reporting

`db top` should print a compact leaderboard and write:

```txt
out/db-top.md
```

Include:

- candidate id,
- current/stale status,
- source,
- quick/deep score,
- fail flags and decoded names,
- audit worst/avg,
- first seen and last seen,
- best report/export paths,
- scoring fingerprint.

`db verify` should report:

- database exists,
- schema version,
- candidate count,
- score row count,
- stale score count,
- missing artifact references,
- candidates with invalid instruction rows.

### Tests

- `db init` creates the database.
- `db import-champions` imports existing champion files.
- `db top --limit 5` writes `out/db-top.md`.
- Setting `HASH_FORGE_SCORE_FINGERPRINT_SALT` forces scores stale and
  `db rescore` refreshes them.
- `db verify` passes after import and rescore.
- Existing `champions` behavior remains compatible.

### Acceptance criteria

1. The database becomes a durable searchable index of best custom hashes.
2. Existing champion files can be imported.
3. Current scoring fingerprint is tracked.
4. Stale scores are detected and rescored before ranking.
5. Top hashes can be listed from the database.
6. Strict tests and smoke tests pass.

