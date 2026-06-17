# Codex goal-seek prompt: ultimate tiny hash-forge

Use this in a fresh Codex thread from `C:\Users\mrmoe\Documents\hash-forge`.

```text
You are Codex working in C:\Users\mrmoe\Documents\hash-forge.

Read AGENTS.md first. Then read README.md, SPEC.md, docs/run-analysis-2026-06-17.md, docs/crossover-analysis-2026-06-17.md, and docs/ultimate-forge-milestones.md completely.

Goal:
Implement the five milestones in docs/ultimate-forge-milestones.md one at a time, in order, without skipping verification. Keep hash-forge tiny, native C11, deterministic, RAM-first in the hot loop, and built with Visual Studio C. Do not touch C:\Users\mrmoe\Documents\hash64. Preserve unrelated local changes, especially unrelated dynamic_array work.

Milestones:
1. Best-over-time telemetry.
2. Policy comparison mode for crossover/starter/refresh policies.
3. Novelty or island lane for structural diversity.
4. Starter-lineage cap or decay.
5. Best deep score seen/export tracking.

Work loop for each milestone:
1. Inspect the current code and docs relevant to that milestone.
2. Write a short implementation note in docs/progress-ultimate-forge.md before editing: milestone name, intended files, test commands, and risks.
3. Implement only that milestone. Keep changes scoped and simple.
4. Update README.md, SPEC.md, and tests for any new CLI/report fields.
5. Run real verification:
   - .\build.ps1
   - .\scripts\test.ps1
   - git diff --check
   - .\scripts\smoke.ps1 when behavior/export/reporting changed
6. Run at least one practical experiment for the milestone using deterministic seeds and record the command, summary metrics, and interpretation in docs/progress-ultimate-forge.md.
7. If any check fails, fix it and rerun until clean or until there is a true external blocker.
8. Commit the milestone with a focused message after verification. Push after each commit.
9. Only then proceed to the next milestone.

Required result recording:
- docs/progress-ultimate-forge.md must accumulate one section per milestone.
- Each section must include:
  - what changed,
  - commands run,
  - pass/fail result,
  - experiment commands,
  - key metrics,
  - interpretation,
  - commit hash.

Implementation priorities:
- Prefer fixed-size arrays or run-start allocations over hot-loop allocation.
- Do not add disk I/O inside generation scoring or breeding.
- Make every new policy visible in out/report.md, out/summary.txt, and relevant CSV/Markdown reports.
- Preserve deterministic generation-limited runs.
- Keep CLI names boring and explicit.
- Keep reports human-readable and script-friendly.
- Use raw scores honestly; if selection pressure or penalties are added, report them separately from raw quick/deep scores.

Milestone-specific reminders:
- Improvement telemetry should log generation, elapsed seconds, source, quick/deep score, flags, and total candidates at each material improvement.
- Policy comparison must use the same seed list and fixed generation budget across all policies.
- Novelty/island work should be controllable and comparable; default conservatively if uncertain.
- Starter-lineage pressure should prevent monopoly without deleting the value of starter candidates.
- Best-deep-seen tracking should ensure a strong periodically deep-scored candidate cannot disappear before export.

Do not stop at planning. Continue milestone by milestone until all five are implemented, verified, documented, committed, and pushed, or until a true external blocker remains. At the end, report the commits, commands passed, experiment conclusions, and any caveats.
```

