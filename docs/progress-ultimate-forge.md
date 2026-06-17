# Ultimate forge progress

This file records milestone implementation evidence for the five-part ultimate
hash-forge goal.

## Milestone 1: Best-over-time telemetry

Status: implemented and verified.

Intended files:

- `src/hash_forge.c`
- `scripts/test.ps1`
- `README.md`
- `SPEC.md`
- `docs/progress-ultimate-forge.md`

Plan:

- Add fixed-capacity in-memory improvement telemetry to `run_evolution`.
- Record each material best-candidate improvement with generation, elapsed
  seconds, id, parent id, candidate generation, source, instruction count,
  quick score, deep score or pending, fail flags, total candidates evaluated,
  and improvement reason.
- Flush the log only after a run completes, into `out/report.md` and
  `out/improvements.csv`.
- Add summary and `best.txt` fields for improvement count, last improvement
  generation, and last improvement elapsed.
- Keep selection behavior unchanged for this milestone.

Risks:

- Avoid hot-loop file I/O and hot-loop heap churn.
- Keep deterministic generation-limited runs stable enough for existing tests.
- Keep report parsing compatible with existing summary fields.

Verification plan:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Experiment plan:

```txt
.\build\hash-forge.exe run --seed 9301 --seconds 10 --threads 32 --quality deep --no-champions
```

Record the improvement count, last improvement generation, last improvement
elapsed seconds, final run generation, total candidates, and whether the report
timeline explains early or late convergence.

What changed:

- Added a fixed-capacity in-memory improvement log to each run.
- Added `out/improvements.csv` for the latest run.
- Added `## Improvement timeline` to `out/report.md`.
- Added improvement count, last improvement generation, and last improvement
  elapsed fields to `out/summary.txt` and `out/best.txt`.
- Added strict tests for the report section, CSV artifact, compact fields, and
  deterministic improvement counts.
- Updated README and SPEC output documentation.

Commands run:

```txt
.\build.ps1
.\build\hash-forge.exe run --seed 123 --generations 5 --threads 2 --no-champions
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
.\build\hash-forge.exe run --seed 9301 --seconds 10 --threads 32 --quality deep --no-champions
```

Verification result:

- `.\build.ps1`: pass.
- Tiny artifact check run: pass.
- `.\scripts\test.ps1`: pass.
- `git diff --check`: pass, with expected CRLF warnings only.
- `.\scripts\smoke.ps1`: pass.

Experiment result:

```txt
id=16334808532297612574
generation=20
run_generation=1828
quick=369398
deep=5124690
flags=0x0
elapsed_seconds=10.008
stop_reason=time limit
quality=deep
threads=32
quick_candidates=467968
deep_candidates=2929
total_candidates=470897
last_unique=256
stagnation_refreshes=31
adaptive_random_immigrants=1736
source=starter
crossover_children=16
improvement_count=16
last_improvement_generation=1593
last_improvement_elapsed=8.697
worst_audit_deep=5098443
combined_audit_flags=0x0
```

Interpretation:

The timeline shows 16 material best-candidate improvements. The exported winner
appeared late in the 10-second run, at generation 1593 and 8.697 seconds, so
this particular run was still finding better quick-ranked candidates late in the
budget. The report also makes a later milestone visible: live deep-scored
candidates exceeded the final exported deep score during the run, reinforcing
the need for Milestone 5 best-deep-seen retention.

Commit hash:

- Pending until the milestone commit is created.
