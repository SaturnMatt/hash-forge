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

- `cc25afd Add improvement telemetry`

## Milestone 2: Policy comparison mode

Status: implemented and verified.

Intended files:

- `src/hash_forge.c`
- `scripts/test.ps1`
- `README.md`
- `SPEC.md`
- `docs/progress-ultimate-forge.md`

Plan:

- Add a dedicated `policy` command while leaving the existing `compare` command
  intact for compatibility.
- Support generated seed ranges via `--seed` and `--seeds`, plus explicit
  comma-separated `--seed-list`.
- Run a fixed generation budget across all policies and seeds.
- Compare at least default, no-crossover, no-starter, no-refresh, bare, and
  refresh-strong policies.
- Write `out/policy.md` and `out/policy.csv`.
- Include policy settings, per-trial metrics, aggregate wins, clean counts,
  average/best scores, candidate totals, improvement telemetry, uniqueness, and
  refresh counts.

Risks:

- Avoid making the current `compare` command brittle.
- Keep policy runs deterministic and bounded for strict tests.
- Avoid too much report-time audit work in the strict suite.
- Refresh strength needs runtime controls without changing default `run`
  behavior.

Verification plan:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Experiment plan:

```txt
.\build\hash-forge.exe policy --seed 9201 --seeds 2 --generations 50 --threads 4 --quality quick
```

Record the winning policy, clean counts, average deep scores, and whether the
policy report gives enough information to choose longer testing.

What changed:

- Added `hash-forge policy`.
- Added `--seed-list` parsing for explicit seed comparisons.
- Added full same-budget policy reports at `out/policy.md` and `out/policy.csv`.
- Compared default, no-crossover, no-starter, no-refresh, bare, and
  refresh-strong policies.
- Disabled champion starters inside policy comparison for fair policy isolation.
- Added runtime refresh window/immigrant settings so refresh-strong is a real
  run policy instead of only a label.
- Added strict tests for policy output, policy CSV, deterministic seed-list
  parity, and invalid seed-list handling.
- Updated README and SPEC command/output documentation.

Commands run:

```txt
.\build.ps1
.\build\hash-forge.exe policy --seed 123 --seeds 1 --generations 3 --threads 1 --quality quick
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
.\build\hash-forge.exe policy --seed 9201 --seeds 2 --generations 50 --threads 4 --quality quick
```

Verification result:

- `.\build.ps1`: pass.
- Tiny policy artifact check run: pass.
- `.\scripts\test.ps1`: pass.
- `git diff --check`: pass, with expected CRLF warnings only.
- `.\scripts\smoke.ps1`: pass.

Experiment result:

```txt
seed_count=2
generations=50
threads=4
quality=quick
champion_starters=disabled

policy          wins  clean_winners  clean_audits  avg_deep  best_deep  avg_last_improvement_gen
default         0     0              0             1449498   1458435    37
no-crossover    0     0              0             1508719   1519570    30
no-starter      2     0              1             1529103   1530842    46
no-refresh      0     0              0             1449498   1458435    37
bare            0     0              0             1456307   1463895    32
refresh-strong  0     0              0             1449498   1458435    37
```

Interpretation:

The short quick-budget experiment favored `no-starter` on both seeds by the
policy ordering, and one `no-starter` audit came back clean even though the
winner's primary flags still showed AVALANCHE. `no-crossover` improved average
deep score versus default but did not win these two seeds. Refresh policies were
identical at this 50-generation budget because no stagnation refresh fired, so
longer generation budgets are needed to evaluate refresh strength.

Commit hash:

- `d6f9a80 Add policy comparison mode`

## Milestone 3: Novelty lane

Status: implemented and verified.

Intended files:

- `src/hash_forge.c`
- `scripts/test.ps1`
- `README.md`
- `SPEC.md`
- `docs/progress-ultimate-forge.md`

Plan:

- Add a small structural novelty lane to breeding.
- Default to a conservative novelty lane, with `--no-novelty` and
  `--novelty-lane <n>` controls for A/B testing.
- Select novelty parents by a cheap structural fingerprint distance from current
  survivors, mutate them, and mark those children as novelty ancestry.
- Keep all novelty state in stack/fixed arrays during generation breeding.
- Report novelty lane size, admitted novelty children, last-generation best and
  average novelty scores, and source ancestry.
- Extend `policy` with a no-novelty policy so novelty off/on can be compared.

Risks:

- Avoid destabilizing deterministic generation-limited runs except where the new
  default policy intentionally changes breeding.
- Keep novelty scoring outside the hash scoring hot loop and free of disk I/O.
- Avoid introducing duplicate candidates; use the existing uniqueness repair.

Verification plan:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Experiment plan:

```txt
.\build\hash-forge.exe policy --seed 9401 --seeds 2 --generations 80 --threads 4 --quality quick
```

Record novelty on/off policy results, novelty telemetry, and whether novelty
changes convergence timing or score quality.

What changed:

- Added a default structural novelty lane of 16 children per generation.
- Added `--no-novelty` and `--novelty-lane <n>` run controls.
- Added `novelty` source ancestry for children admitted through the novelty
  lane.
- Added cheap structural fingerprint distance scoring against current survivors.
- Added novelty telemetry to console output, `out/report.md`, `out/best.txt`,
  and `out/summary.txt`.
- Extended `policy` with a `no-novelty` policy and novelty columns in
  `out/policy.md` and `out/policy.csv`.
- Added strict tests for novelty telemetry and no-novelty disable behavior.
- Updated README and SPEC.

Commands run:

```txt
.\build.ps1
.\build\hash-forge.exe run --seed 123 --generations 5 --threads 2 --no-champions --no-novelty
.\build\hash-forge.exe run --seed 123 --generations 5 --threads 2 --no-champions
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
.\build\hash-forge.exe policy --seed 9401 --seeds 2 --generations 80 --threads 4 --quality quick
```

Verification result:

- `.\build.ps1`: pass.
- No-novelty compatibility run: pass; reproduced the previous short-run best id
  `c3d69d252d36fbf7`.
- Default novelty artifact check run: pass; admitted 80 novelty candidates in 5
  generations.
- `.\scripts\test.ps1`: pass.
- `git diff --check`: pass, with expected CRLF warnings only.
- `.\scripts\smoke.ps1`: pass.

Experiment result:

```txt
seed_count=2
generations=80
threads=4
quality=quick
champion_starters=disabled

policy          wins  clean_winners  clean_audits  avg_deep  best_deep  avg_last_improvement_gen  avg_refreshes
default         0     0              0             1554746   1591789    45                        0
no-novelty      1     0              1             1517624   1551198    35                        0
no-crossover    0     0              0             1495894   1505785    41                        0
no-starter      0     0              0             1559097   1573195    51                        0
no-refresh      0     0              0             1554746   1591789    45                        0
bare            1     0              1             1531773   1540034    26                        0
refresh-strong  0     0              0             1431415   1517704    43                        2
```

Interpretation:

The novelty lane changed search behavior materially. Default novelty beat
no-novelty on seed 9401 and admitted 1280 novelty children over 80 generations.
One no-crossover trial selected a novelty-source winner, proving the lane can
surface candidates into the final result path. The two-seed sample is not a
blanket win for novelty: no-novelty produced the best policy-ordered candidate
on seed 9402 due to a clean audit, and bare also found one clean audit. This
looks like a real exploration knob worth evaluating with longer policy runs.

Commit hash:

- `3def400 Add novelty lane`

## Milestone 4: Starter-lineage cap

Status: implemented and verified.

Intended files:

- `src/hash_forge.c`
- `scripts/test.ps1`
- `README.md`
- `SPEC.md`
- `docs/progress-ultimate-forge.md`

Plan:

- Add an explicit starter survivor cap, disabled by default.
- Add `--starter-cap <n>`, `--starter-cap-after <generations>`, and
  `--no-starter-cap` controls.
- Apply the cap only to survivor slots used for breeding, after scoring and
  best tracking, so raw scores stay honest.
- Fill displaced starter survivor slots with the next best non-starter
  candidates.
- Report cap settings, before/after starter survivor counts, and displacement
  totals.
- Add a `starter-cap` policy so policy mode can compare the cap against default.

Risks:

- Source ancestry currently has one label, so novelty-source descendants of
  starter candidates are not counted as starter for the cap. This keeps the
  implementation tiny but should be noted in interpretation.
- Cap changes breeding pressure but should not mutate raw quick/deep scores.
- A forced cap of zero must still produce valid exports.

Verification plan:

```txt
.\build.ps1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
```

Experiment plan:

```txt
.\build\hash-forge.exe policy --seed 9501 --seeds 2 --generations 80 --threads 4 --quality quick
```

Record starter-cap policy results, displacement telemetry, and whether the cap
reduces starter survivor dominance without breaking candidate quality.

What changed:

- Added `--starter-cap <n>`, `--starter-cap-after <generations>`, and
  `--no-starter-cap`.
- Added starter survivor cap logic that reorders breeding survivor slots after
  scoring and best tracking, without changing raw quick/deep scores.
- Added starter-cap telemetry to console output, `out/report.md`, `out/best.txt`,
  and `out/summary.txt`.
- Added `starter-cap` to policy mode with cap 8 after generation 10.
- Added starter-cap columns to `out/policy.md` and `out/policy.csv`.
- Added strict tests for forced cap zero and displacement telemetry.
- Updated README and SPEC.

Commands run:

```txt
.\build.ps1
.\build\hash-forge.exe run --seed 123 --generations 5 --threads 2 --no-champions --starter-cap 0 --starter-cap-after 1
.\scripts\test.ps1
git diff --check
.\scripts\smoke.ps1
.\build\hash-forge.exe policy --seed 9501 --seeds 2 --generations 80 --threads 4 --quality quick
```

Verification result:

- `.\build.ps1`: pass.
- Forced starter-cap run: pass; cap 0 after generation 1 displaced 26 starter
  survivor slots in 5 generations and still exported a valid candidate.
- `.\scripts\test.ps1`: pass.
- `git diff --check`: pass, with expected CRLF warnings only.
- `.\scripts\smoke.ps1`: pass.

Experiment result:

```txt
seed_count=2
generations=80
threads=4
quality=quick
champion_starters=disabled

policy          wins  clean_winners  clean_audits  avg_deep  best_deep  cap_displacements
default         0     0              0             1460001   1490266    0
starter-cap     0     0              0             1451534   1486378    78, 53
no-novelty      0     0              0             1493201   1504637    0
no-crossover    0     0              0             1464037   1513289    0
no-starter      0     0              0             1494335   1503412    0
no-refresh      0     0              0             1460001   1490266    0
bare            1     0              0             1518063   1536206    0
refresh-strong  1     0              0             1446220   1512170    0
```

Interpretation:

The starter cap is active and measurable: it displaced 78 starter survivor slots
on seed 9501 and 53 on seed 9502. It reduced starter breeding pressure enough
that the seed 9502 starter-cap winner had random source ancestry, but it did not
win this two-seed quick-budget sample. This suggests the cap is useful as a
policy knob, but cap 8 after generation 10 may be too blunt for default use.
Current source labels are single-valued, so novelty-source descendants of starter
candidates are not counted as starter-lineage by this cap.

Commit hash:

- `c9a1bfc Add starter survivor cap`
