# hash-forge run analysis - 2026-06-17

## Setup

Commands run:

```txt
hash-forge baselines --seed 123 --quality deep --deep
hash-forge run --seed 9001 --seconds 10 --threads 32 --quality deep
hash-forge run --seed 9002 --seconds 10 --threads 32 --quality deep
hash-forge run --seed 9003 --seconds 10 --threads 32 --quality deep
hash-forge run --seed 9004 --seconds 10 --threads 32 --quality deep
hash-forge run --seed 9005 --seconds 10 --threads 32 --quality deep
hash-forge run --seed 9011 --seconds 60 --threads 32 --quality deep
```

Reference baselines from `out/baselines.md`:

| reference | deep score | flags |
|---|---:|---|
| splitmix64_finalizer | 5142083 | 0x0 |
| murmur3_fmix64 | 5121405 | 0x0 |
| fnv1a64_pair | -44120672 | 0x8 |

## Batch results

| seed | seconds | generations | total candidates | deep score | flags | audit result |
|---:|---:|---:|---:|---:|---|---|
| 9001 | 10 | 1728 | 445137 | 5125876 | 0x0 | clean, worst audit 5084894 |
| 9004 | 10 | 1785 | 459825 | 5120102 | 0x0 | clean, worst audit 5095036 |
| 9005 | 10 | 1756 | 452353 | 4442184 | 0x0 | clean, worst audit 4397002 |
| 9002 | 10 | 1795 | 462401 | 4082232 | 0x0 | clean, worst audit 4062478 |
| 9003 | 10 | 1782 | 459057 | 3799500 | 0x0 | audit found AVALANCHE |
| 9011 | 60 | 11110 | 2861953 | 5124925 | 0x0 | clean, worst audit 5101060 |

## Readout

The forge can produce clean, serious non-cryptographic hash candidates quickly.
The best 10-second run landed at 5125876, essentially tied with the Murmur3
fmix64 reference for this scoring suite and just under SplitMix64. The 60-second
run was also clean and stable, with an average audit score of 5119602, but it did
not beat the best short run.

The lab itself is doing well as an exploratory search engine. It evaluated about
445k to 462k candidates in 10 seconds and 2.86M candidates in 60 seconds on 32
threads with deep scoring enabled. Diversity stayed high at 256/256 unique
candidates in the final scored generation.

The biggest weakness is search plateau behavior. The 60-second run found a
strong family very early, then spent most of the remaining run refreshing around
the same score band. Refreshes kept diversity alive, but did not reliably climb
past SplitMix64.

The second weakness is winner retention. During the 60-second run, live status
showed a candidate with a higher deep score than the final exported winner. The
final selection favored the candidate ordering path at exit, so hash-forge should
track and export the best deep-scored candidate seen across the entire run, not
only the final selected best candidate.

## Improvement targets

1. Track `best_deep_seen` separately from current population best and export it
   when it has clean flags and better deep score.
2. Add cross-run champion mode: seed each new run with archived historical
   winners, then mutate from those champions.
3. Add a harder final acceptance audit before export, using more seeds and
   reporting whether the candidate stays clean.
4. Improve selection pressure after plateau: prefer candidates with better audit
   stability, not just quick score and occasional deep score.
5. Add explicit speed measurement for exported C candidates so quality can be
   balanced against real native hash throughput.

