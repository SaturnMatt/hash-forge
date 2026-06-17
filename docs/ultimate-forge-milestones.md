# Ultimate tiny hash-forge milestone specs

These milestones build directly on the 2026-06-17 crossover and long-run
analysis. The goal is to make hash-forge better at understanding its own search
behavior, comparing policy choices fairly, and escaping early starter-lineage
convergence without making the project large or framework-heavy.

Each milestone should preserve the core project constraints:

- C11, native Windows x64, Visual Studio C build.
- Keep the hot loop free of disk I/O and avoid heap churn inside generation
  scoring and breeding.
- Keep behavior deterministic for the same seed, options, binary, and thread
  count.
- Keep generated reports human-readable and machine-parsable enough for scripts.
- Run real verification and commit after each milestone.

## Milestone 1: Best-over-time telemetry

### Problem

Current reports describe the final winner, total evaluations, and diversity
telemetry, but they do not show when the search actually improved. This makes it
hard to tell whether a long run was productive, stale after generation 20, or
still climbing slowly.

### Goal

Record every material best-candidate improvement during a run and include that
timeline in `out/report.md`, archived run reports, and a compact machine-readable
artifact.

### Scope

Add a small fixed-capacity improvement log stored in RAM during the run. A log
entry should capture:

- run generation,
- elapsed seconds,
- candidate id,
- parent id,
- candidate generation,
- candidate source ancestry,
- instruction count,
- quick score,
- deep score if known, otherwise `pending`,
- fail flags and decoded flag names,
- total candidates evaluated so far,
- improvement reason: `first`, `quick`, `deep`, `flags`, or `tie-break`.

The log must not write to disk inside the hot loop. It should be flushed only
when exporting normal run artifacts.

If the log reaches capacity, keep the first entry, the latest entries, and a
counter of omitted middle entries. V1 capacity can be simple, such as 256 or 512
entries.

### Reporting

Add to `out/report.md`:

- `## Improvement timeline`
- a table of improvement events,
- omitted-event count if the fixed buffer overflowed,
- a short interpretation note: early final improvement means the run was likely
  stale after that point.

Add `out/improvements.csv` for the latest run and mention it in the report's
output-file list. Archived per-run CSV is optional for this milestone, but nice
if simple.

Add summary fields:

- `improvement_count`
- `last_improvement_generation`
- `last_improvement_elapsed`

### Tests

- Generation-limited deterministic runs with the same seed should produce the
  same improvement count and final best id.
- `out/report.md` must contain `Improvement timeline`.
- `out/improvements.csv` must exist and contain at least one event.
- A short time-limited run should show elapsed seconds in the timeline.

### Acceptance criteria

1. Build and strict tests pass.
2. The run report shows the full improvement timeline.
3. The latest run writes `out/improvements.csv`.
4. Deterministic generation-limited runs remain deterministic.
5. A short long-run comparison can now show whether the final winner appeared
   early or late.

## Milestone 2: Policy comparison mode

### Problem

We can manually run crossover on/off and starter/refresh variants, but the
process is fragile and slow to repeat. We need a built-in mode that fairly
compares policy knobs across the same seed list and fixed generation budgets.

### Goal

Add a deterministic policy comparison command that runs multiple search policies
against the same seeds and produces an aggregate report.

### Proposed CLI

```txt
hash-forge policy --seed 9201 --seeds 5 --generations 1000 --threads 32 --quality deep
hash-forge policy --seed 9201 --seed-list 9201,9202,9203 --generations 1000 --threads 32 --quality deep
```

Keep `compare` intact unless replacing it is clearly simpler. If the current
`compare` command can be extended cleanly, use that instead of adding a second
similar command.

### Policies to compare

At minimum:

- `default`: starter on, refresh normal, crossover on.
- `no-crossover`: starter on, refresh normal, crossover off.
- `no-starter`: starter off, refresh normal, crossover on.
- `no-refresh`: starter on, refresh off, crossover on.
- `bare`: starter off, refresh off, crossover off.

Refresh strength policies:

- `refresh-off`: no refresh.
- `refresh-normal`: current refresh settings.
- `refresh-strong`: shorter stagnation window or larger immigrant burst.

If refresh strength currently requires compile-time constants, add run options
that keep the default behavior unchanged.

### Metrics

For each trial record:

- policy name,
- seed,
- best id,
- source ancestry,
- quick score,
- deep score,
- fail flags,
- audit worst and audit flags if available without too much cost,
- total candidates,
- run generations,
- improvement count and last improvement generation if Milestone 1 exists,
- last unique candidate count,
- stagnation refreshes.

Aggregate by policy:

- trials,
- wins by deep score with clean flags first,
- clean winner count,
- clean audit count,
- average deep score,
- best deep score,
- average total candidates,
- average last improvement generation,
- average refresh count.

### Reporting

Write:

- `out/policy.md`
- `out/policy.csv`

The Markdown report should contain:

- run settings,
- policy definitions,
- aggregate policy table,
- per-seed trial table,
- interpretation note about sample size and deterministic same-budget comparison.

### Tests

- A tiny policy run with 1 seed and 3 generations completes.
- `out/policy.md` and `out/policy.csv` exist.
- Report contains default, no-crossover, no-starter, no-refresh, and bare rows.
- Same seed/generation policy run is deterministic enough to repeat final ids
  for each policy.

### Acceptance criteria

1. One command compares policy choices across the same seeds and fixed budget.
2. Crossover, starter, and refresh-strength choices are visible in output.
3. Reports are useful enough to decide which policy deserves longer testing.
4. Build, strict tests, and smoke tests pass.

## Milestone 3: Novelty or island lane

### Problem

The population remains unique by id, but selection still collapses around early
high-scoring starter descendants. We need part of the population to preserve or
seek structural diversity, not only current score.

### Goal

Reserve a small portion of each generation for candidates selected or bred by
novelty/island rules. This should increase exploration while keeping the project
tiny and deterministic.

### Design options

Choose the simplest option that fits the code:

Option A, novelty lane:

- Compute a cheap structural fingerprint for every candidate from op histogram,
  instruction count, hash-write positions, constant usage, and source ancestry.
- Track novelty as distance from survivors or from a small rolling archive.
- Reserve N children per generation for high-novelty candidates, even if their
  current score is not top-tier.

Option B, island lane:

- Split the population into a small number of deterministic islands.
- Each island keeps local survivors and breeds mostly within itself.
- Migrate a few candidates between islands every fixed number of generations.
- Keep global final ranking/export unchanged.

The first implementation should prefer one option, not both, unless both are
surprisingly small. A novelty lane is probably the smallest next step.

### Proposed controls

```txt
--novelty-lane <n>
--no-novelty
```

Default can be conservative, such as 16 novelty children per generation, or off
by default if more testing is needed. If enabled by default, the policy report
must make that visible.

### Telemetry

Add:

- novelty lane size,
- novelty candidates admitted,
- average or best novelty score in last generation,
- best candidate source if novelty produced the winner.

Report these in `out/report.md`, `out/summary.txt`, and policy reports.

### Tests

- `--no-novelty` or `--novelty-lane 0` preserves the old deterministic path.
- A small novelty-enabled run completes and records novelty telemetry.
- The population remains fully valid and unique after breeding.
- Policy mode can compare novelty off/on if Milestone 2 exists.

### Acceptance criteria

1. A controlled diversity lane exists.
2. It does not add disk I/O or heap churn inside the hot loop.
3. It can be disabled for A/B tests.
4. Reports expose whether novelty participated meaningfully.
5. Strict tests pass.

## Milestone 4: Starter-lineage cap or decay

### Problem

Starter-derived candidates are useful, but current evidence shows they can
monopolize winners. The forge needs to let starter lineage lead when it is truly
best while preventing it from swallowing the entire search.

### Goal

Add a lineage pressure mechanism that reduces starter-lineage dominance without
removing starter benefits entirely.

### Design options

Choose the simplest deterministic approach:

Option A, starter survivor cap:

- Limit how many starter-descended candidates may occupy survivor slots after a
  configurable warmup.
- Fill overflow survivor slots with the next best non-starter candidates.

Option B, starter score decay:

- Apply a small selection-only penalty to starter-descended candidates after a
  warmup generation.
- Do not change raw quick/deep scores in reports; keep the penalty separate and
  label it selection pressure.

Option C, starter lane sunset:

- Allow starter descendants normally for the first N generations.
- After that, reduce their breeding share unless they are clean and materially
  ahead.

V1 should prefer a cap because it is easy to reason about and report.

### Proposed controls

```txt
--starter-cap <n>
--starter-cap-after <generations>
--no-starter-cap
```

Default should be conservative. If there is uncertainty, leave the cap disabled
by default and use policy mode to evaluate it.

### Telemetry

Record:

- starter cap setting,
- starter descendants in top survivors before cap,
- starter descendants after cap,
- candidates displaced by cap,
- whether final winner is starter-descended.

Report these in `out/report.md`, `out/summary.txt`, and policy reports.

### Tests

- Cap disabled preserves deterministic old path.
- Cap enabled completes and reports displacement telemetry.
- A tiny forced cap, such as `--starter-cap 0 --starter-cap-after 1`, does not
  crash and still produces a valid export.
- Policy mode can compare cap off/on if Milestone 2 exists.

### Acceptance criteria

1. Starter lineage can no longer monopolize survivor slots when cap is enabled.
2. Raw scores remain honest and separate from selection penalties.
3. Reports explain the cap's effect.
4. Strict tests pass.

## Milestone 5: Best deep seen export

### Problem

During periodic deep scoring, a candidate can show a strong deep score and then
disappear if quick-ranked selection favors another candidate. The final export
should not lose the best deep-scored candidate seen during the run.

### Goal

Track `best_deep_seen` separately from the quick-ranked leader and export the
best candidate according to final deep-aware ordering.

### Behavior

Maintain at least two best trackers:

- `best_quick_seen`: best candidate by current quick-oriented ordering.
- `best_deep_seen`: best candidate among candidates that have received deep
  scoring.

At export time:

- deep-score both trackers if needed,
- choose the final exported candidate with clean flags first, then best deep
  score, then quick score, then instruction count/tie-breaker,
- report whether the exported candidate came from quick leader or deep-seen
  leader.

This must not deep-score the entire population every generation. It should only
preserve candidates already deep-scored by the existing cadence plus final
tracker validation.

### Reporting

Add to `out/report.md`:

- `## Best trackers`
- quick leader id, source, quick/deep score, flags,
- deep-seen leader id, source, quick/deep score, flags,
- exported winner source: `quick`, `deep-seen`, or `same`.

Add summary fields:

- `best_quick_id`
- `best_deep_seen_id`
- `export_selection`

Add `best.txt` fields for the same.

### Tests

- Deterministic generation-limited runs remain deterministic.
- Reports contain `Best trackers`.
- Exported C still compiles and passes vector self-test.
- Add a targeted unit/self-test if practical: construct two candidates where
  one has better quick score and one has better deep score, then verify final
  selection chooses the deep-clean winner.

### Acceptance criteria

1. Deep-scored winners cannot silently disappear from final export.
2. Reports explain quick leader versus deep leader.
3. Existing export artifacts still work.
4. Strict tests and smoke tests pass.

