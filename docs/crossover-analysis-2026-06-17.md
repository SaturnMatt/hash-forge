# Crossover and long-run evolution analysis - 2026-06-17

## Setup

Build under test:

```txt
.\build.ps1
.\scripts\test.ps1
```

Experiment commands used fixed deep-quality scoring, 32 scoring workers, and no
champion starters so the crossover policy was isolated:

```txt
.\build\hash-forge.exe run --seed <seed> --seconds <n> --threads 32 --quality deep --no-champions
.\build\hash-forge.exe run --seed <seed> --seconds <n> --threads 32 --quality deep --no-champions --no-crossover
```

The `--no-crossover` run keeps population size and random immigrant count the
same, but replaces the 16 crossover children per generation with normal mutation
children.

## Short-run A/B results

Five 10-second paired runs:

| seed | crossover | deep | flags | audit worst | audit flags | winner candidate generation | run generations | evals/sec | last unique | refreshes |
|---:|---|---:|---|---:|---|---:|---:|---:|---:|---:|
| 9201 | on | 3463496 | 0x0 | 3376658 | 0x0 | 19 | 1839 | 47326 | 256 | 36 |
| 9201 | off | 3562346 | 0x8 | 3547836 | 0x8 | 19 | 1642 | 42266 | 256 | 31 |
| 9202 | on | 3838952 | 0x8 | 3789346 | 0x8 | 15 | 1592 | 40974 | 256 | 28 |
| 9202 | off | 5073664 | 0x0 | 5051540 | 0x0 | 14 | 1684 | 43338 | 256 | 31 |
| 9203 | on | 3386602 | 0x8 | 3361185 | 0x8 | 13 | 1732 | 44573 | 256 | 33 |
| 9203 | off | 4588136 | 0x0 | 4441840 | 0x8 | 16 | 1825 | 46971 | 256 | 28 |
| 9204 | on | 4922253 | 0x0 | 4888296 | 0x0 | 22 | 1708 | 43963 | 256 | 30 |
| 9204 | off | 5105467 | 0x0 | 5103657 | 0x0 | 16 | 1717 | 44182 | 256 | 31 |
| 9205 | on | 5113955 | 0x0 | 5115193 | 0x0 | 17 | 1870 | 48130 | 256 | 34 |
| 9205 | off | 5096084 | 0x0 | 5094267 | 0x0 | 13 | 1810 | 46581 | 256 | 34 |

Short-run aggregate:

| policy | avg deep | best deep | clean winners | clean audits | avg winner generation | avg evals/sec | deep-score wins |
|---|---:|---:|---:|---:|---:|---:|---:|
| crossover on | 4145052 | 5113955 | 3/5 | 3/5 | 17.2 | 44993 | 1/5 |
| crossover off | 4685139 | 5105467 | 4/5 | 3/5 | 15.6 | 44667 | 4/5 |

In this small sample, disabling crossover improved the average final deep score
by about 13.0 percent and won four of five paired seeds. Throughput was nearly
unchanged, so this looks like search behavior rather than a performance artifact.

## Long-run check

Same seed, 10 seconds versus 60 seconds:

| seed | seconds | crossover | deep | flags | audit worst | audit flags | winner candidate generation | run generations | evals/sec | last unique | refreshes |
|---:|---:|---|---:|---|---:|---|---:|---:|---:|---:|---:|
| 9301 | 10 | on | 5124690 | 0x0 | 5098443 | 0x0 | 20 | 1832 | 47147 | 256 | 31 |
| 9301 | 60 | on | 5124690 | 0x0 | 5098443 | 0x0 | 20 | 11066 | 47505 | 256 | 216 |
| 9301 | 10 | off | 3806363 | 0x0 | 3635270 | 0x0 | 16 | 1755 | 45169 | 256 | 33 |
| 9301 | 60 | off | 5043825 | 0x0 | 4995724 | 0x0 | 24 | 10840 | 46526 | 256 | 213 |

Crossover-on converged immediately for this seed: the 10-second and 60-second
winners were identical. Crossover-off needed the longer run and improved its
deep score by about 32.5 percent, from 3806363 to 5043825.

The 60-second crossover-on result still beat the 60-second crossover-off result
on this seed, but only because it found a very strong candidate early.

## Staleness read

The candidate pool does not appear to go stale by duplication collapse. Every
tested run ended with `256/256` unique candidates in the last scored generation,
and duplicate repair never had to fall back to fresh random replacement.

The pool does show fitness staleness. In all runs, the final winner descended
from starter material and appeared by candidate generation 13 through 24, while
the engine continued through thousands of run generations. The stagnation
refresh mechanism did fire repeatedly, but it did not usually dislodge the early
starter-lineage winner.

## Current conclusion

For this build, crossover is not obviously a universal win. It slightly improves
or preserves throughput and can find excellent candidates quickly, but the
short-run sample favored mutation-only search. Long runs are sometimes helpful:
the no-crossover 9301 run materially improved after 10 seconds. They are also
often wasteful: the crossover-on 9301 run spent another 50 seconds without
improving at all.

The bigger limiter is not population uniqueness; it is convergence pressure.
The forge maintains diverse candidate ids, but selection still collapses around
early starter-derived winners.

## Recommended next changes

1. Add best-over-time telemetry so reports show each improvement event with
   generation, elapsed seconds, source, score, and flags.
2. Add a policy comparison mode for crossover on/off, starter on/off, and
   refresh strength using the same seed list and fixed generation budgets.
3. Add a novelty or island lane so a portion of the population is selected for
   structural diversity rather than only current score.
4. Add a starter-lineage cap or decay so starter descendants can lead, but not
   monopolize every run forever.
5. Track best deep score seen during periodic deep scoring separately from the
   quick-ranked leader so strong deep-scored candidates cannot disappear from
   the final export.

