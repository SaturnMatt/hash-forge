#include "hf_core.h"

static int collision_add(ScoreScratch *scratch, uint64_t key, uint64_t seed, uint64_t h) {
    uint32_t mask = COLLISION_TABLE_SIZE - 1u;
    uint32_t at = (uint32_t)(h ^ (h >> 32)) & mask;
    for (;;) {
        if (scratch->collision_seen[at] != scratch->collision_epoch) {
            scratch->collision_seen[at] = scratch->collision_epoch;
            scratch->collision_keys[at] = h;
            scratch->collision_input_keys[at] = key;
            scratch->collision_input_seeds[at] = seed;
            return 0;
        }
        if (scratch->collision_keys[at] == h) {
            if (scratch->collision_input_keys[at] == key && scratch->collision_input_seeds[at] == seed) {
                return 0;
            }
            return 1;
        }
        at = (at + 1u) & mask;
    }
}

static int64_t score_zero(const HashSubject *subject, uint32_t *flags) {
    uint64_t out[4];
    out[0] = subject->eval(subject->ctx, 0, 0);
    out[1] = subject->eval(subject->ctx, 0, 1);
    out[2] = subject->eval(subject->ctx, 1, 0);
    out[3] = subject->eval(subject->ctx, 1, 1);

    int64_t score = 4000;
    for (int i = 0; i < 4; i++) {
        if (out[i] == 0 || out[i] == 1) {
            score -= 1000;
            *flags |= FAIL_ZERO;
        }
        for (int j = i + 1; j < 4; j++) {
            if (out[i] == out[j]) {
                score -= 1000;
                *flags |= FAIL_ZERO;
            }
        }
    }
    return score;
}

static int64_t score_collisions(const HashSubject *subject, ScoreScratch *scratch, uint64_t seed, int iterations, uint32_t *flags, uint32_t *evals) {
    scratch->collision_epoch++;
    if (scratch->collision_epoch == 0) {
        memset(scratch->collision_seen, 0, sizeof(scratch->collision_seen));
        scratch->collision_epoch = 1;
    }
    Rng rng = { seed };
    int collisions = 0;
    int outputs = 0;
    uint64_t constant = splitmix64_next(&rng);
    uint64_t seq1 = splitmix64_next(&rng);
    uint64_t seq2 = splitmix64_next(&rng);
    uint64_t rec1 = splitmix64_next(&rng);
    uint64_t rec2 = splitmix64_next(&rng);
    uint64_t rec3 = splitmix64_next(&rng);

#define ADD_HASH(k, s, expr) do { collisions += collision_add(scratch, (k), (s), (expr)); outputs++; } while (0)
    for (int i = 0; i < iterations; i++) {
        uint64_t r1 = splitmix64_next(&rng);
        uint64_t r2 = splitmix64_next(&rng);
        uint64_t s = (uint64_t)i;
        ADD_HASH(0, r1, subject->eval(subject->ctx, 0, r1));
        ADD_HASH(r1, 0, subject->eval(subject->ctx, r1, 0));
        ADD_HASH(constant, r1, subject->eval(subject->ctx, constant, r1));
        ADD_HASH(r1, constant, subject->eval(subject->ctx, r1, constant));
        ADD_HASH(r1, r2, subject->eval(subject->ctx, r1, r2));
        ADD_HASH(r2, r1, subject->eval(subject->ctx, r2, r1));
        ADD_HASH(s, 0, subject->eval(subject->ctx, s, 0));
        ADD_HASH(0, s, subject->eval(subject->ctx, 0, s));
        ADD_HASH(seq1, seq2, subject->eval(subject->ctx, seq1, seq2));
        ADD_HASH(seq2, seq1, subject->eval(subject->ctx, seq2, seq1));
        uint64_t rec_key = rec1;
        rec1 = subject->eval(subject->ctx, rec_key, constant);
        ADD_HASH(rec_key, constant, rec1);
        uint64_t rec_seed = rec2;
        rec2 = subject->eval(subject->ctx, constant, rec_seed);
        ADD_HASH(constant, rec_seed, rec2);
        rec_key = rec3;
        rec3 = subject->eval(subject->ctx, rec_key, rec_key);
        ADD_HASH(rec_key, rec_key, rec3);
        seq1++;
        seq2++;
    }
#undef ADD_HASH

    *evals += (uint32_t)(iterations * 13);
    if (collisions > 0) {
        *flags |= FAIL_COLLISION;
    }
    return (int64_t)outputs * 200 - (int64_t)collisions * 20000;
}

static int64_t score_buckets(const HashSubject *subject, ScoreScratch *scratch, uint64_t seed, int iterations, int deep, uint32_t *flags, uint32_t *evals) {
    Rng rng = { seed };
    uint64_t key0 = splitmix64_next(&rng);
    uint64_t seed0 = splitmix64_next(&rng);
    int64_t score = 0;
    int worst_abs = 0;
    int bucket_step = deep ? 1 : 3;

    for (int mode = 0; mode < 2; mode++) {
        for (int buckets = 2; buckets <= 64; buckets += bucket_step) {
            memset(scratch->bucket_counts, 0, sizeof(scratch->bucket_counts));
            for (int i = 0; i < iterations; i++) {
                uint64_t key = mode == 0 ? key0 + (uint64_t)i : key0;
                uint64_t s = mode == 1 ? seed0 + (uint64_t)i : seed0;
                uint64_t h = subject->eval(subject->ctx, key, s);
                scratch->bucket_counts[h % (uint64_t)buckets]++;
            }
            int expected = iterations / buckets;
            for (int b = 0; b < buckets; b++) {
                int diff = scratch->bucket_counts[b] - expected;
                if (diff < 0) diff = -diff;
                if (diff > worst_abs) worst_abs = diff;
                score += 100 - diff * diff;
            }
            *evals += (uint32_t)iterations;
        }
    }

    if (worst_abs > iterations / 2) {
        *flags |= FAIL_BUCKET;
    }
    return score;
}

static int64_t score_avalanche(const HashSubject *subject, ScoreScratch *scratch, uint64_t seed, int iterations, int deep, uint32_t *flags, uint32_t *evals) {
    Rng rng = { seed };
    int bit_step = deep ? 1 : 4;
    int64_t score = 0;
    int worst_abs = 0;

    for (int variant = 0; variant < 5; variant++) {
        for (int bit = 0; bit < 64; bit += bit_step) {
            memset(scratch->bit_counts, 0, sizeof(scratch->bit_counts));
            for (int i = 0; i < iterations; i++) {
                uint64_t key = (variant == 2 || variant == 3) ? (uint64_t)i + splitmix64_next(&rng) : splitmix64_next(&rng);
                uint64_t s = (variant == 2 || variant == 3) ? (uint64_t)i + splitmix64_next(&rng) : splitmix64_next(&rng);
                uint64_t key2 = key;
                uint64_t s2 = s;
                if (variant == 0 || variant == 2 || variant == 4) key2 ^= 1ull << bit;
                if (variant == 1 || variant == 3 || variant == 4) s2 ^= 1ull << bit;
                uint64_t diff = subject->eval(subject->ctx, key, s) ^ subject->eval(subject->ctx, key2, s2);
                for (int out_bit = 0; out_bit < 64; out_bit++) {
                    scratch->bit_counts[out_bit] += (int)((diff >> out_bit) & 1ull);
                }
            }
            int target = iterations / 2;
            for (int out_bit = 0; out_bit < 64; out_bit++) {
                int d = scratch->bit_counts[out_bit] - target;
                if (d < 0) d = -d;
                if (d > worst_abs) worst_abs = d;
                score += (int64_t)iterations - d * d;
            }
            *evals += (uint32_t)(iterations * 2);
        }
    }

    if (worst_abs > (iterations * 7) / 16) {
        *flags |= FAIL_AVALANCHE;
    }
    return score;
}

static int64_t score_differentials(const HashSubject *subject, ScoreScratch *scratch, uint64_t seed, int iterations, int deep, uint32_t *flags, uint32_t *evals) {
    Rng rng = { seed };
    int bucket_count = deep ? 16 : 8;
    int64_t score = 0;
    int worst_pop_abs = 0;
    int worst_bucket_abs = 0;
    int zero_diffs = 0;

    for (int mode = 0; mode < 3; mode++) {
        memset(scratch->bucket_counts, 0, sizeof(scratch->bucket_counts));
        for (int i = 0; i < iterations; i++) {
            uint64_t key = splitmix64_next(&rng) + (uint64_t)i;
            uint64_t s = splitmix64_next(&rng) + (uint64_t)(i * 0x9e3779b9u);
            uint64_t key2 = key;
            uint64_t s2 = s;
            if (mode == 0 || mode == 2) key2++;
            if (mode == 1 || mode == 2) s2++;

            uint64_t diff = subject->eval(subject->ctx, key, s) ^ subject->eval(subject->ctx, key2, s2);
            if (diff == 0) zero_diffs++;
            int pop_abs = popcount64(diff) - 32;
            if (pop_abs < 0) pop_abs = -pop_abs;
            if (pop_abs > worst_pop_abs) worst_pop_abs = pop_abs;
            scratch->bucket_counts[diff & (uint64_t)(bucket_count - 1)]++;
            score += 220 - (int64_t)pop_abs * pop_abs * 3;
        }

        int expected = iterations / bucket_count;
        if (expected < 1) expected = 1;
        for (int b = 0; b < bucket_count; b++) {
            int d = scratch->bucket_counts[b] - expected;
            if (d < 0) d = -d;
            if (d > worst_bucket_abs) worst_bucket_abs = d;
            score += 80 - d * d * 8;
        }
    }

    *evals += (uint32_t)(iterations * 2 * 3);
    if (zero_diffs > 0 || worst_pop_abs > 24 || worst_bucket_abs > (iterations * 3) / 4) {
        *flags |= FAIL_DIFFERENTIAL;
    }
    return score - (int64_t)zero_diffs * 5000;
}

static int64_t score_input_sensitivity(const HashSubject *subject, uint64_t seed, int iterations, uint32_t *flags, uint32_t *evals) {
    Rng rng = { seed };
    int key_zero = 0;
    int seed_zero = 0;
    int both_zero = 0;
    int64_t score = 0;

    for (int i = 0; i < iterations; i++) {
        uint64_t key = splitmix64_next(&rng) + (uint64_t)i;
        uint64_t s = splitmix64_next(&rng) + ((uint64_t)i << 32);
        uint64_t h = subject->eval(subject->ctx, key, s);
        uint64_t key_diff = h ^ subject->eval(subject->ctx, key ^ 0x9e3779b97f4a7c15ull, s);
        uint64_t seed_diff = h ^ subject->eval(subject->ctx, key, s ^ 0xbf58476d1ce4e5b9ull);
        uint64_t both_diff = h ^ subject->eval(subject->ctx, key ^ 0xd1b54a32d192ed03ull, s ^ 0x94d049bb133111ebull);
        if (key_diff == 0) key_zero++;
        if (seed_diff == 0) seed_zero++;
        if (both_diff == 0) both_zero++;
        int key_pop = popcount64(key_diff);
        int seed_pop = popcount64(seed_diff);
        int both_pop = popcount64(both_diff);
        int key_abs = key_pop - 32;
        int seed_abs = seed_pop - 32;
        int both_abs = both_pop - 32;
        if (key_abs < 0) key_abs = -key_abs;
        if (seed_abs < 0) seed_abs = -seed_abs;
        if (both_abs < 0) both_abs = -both_abs;
        score += 260 - (int64_t)(key_abs + seed_abs + both_abs) * 12;
    }

    *evals += (uint32_t)(iterations * 4);
    if (key_zero > 0 || seed_zero > 0 || both_zero > 0) {
        *flags |= FAIL_SENSITIVITY;
    }
    return score - (int64_t)(key_zero + seed_zero + both_zero) * 8000;
}

static ScoreResult score_subject(const HashSubject *subject, uint64_t subject_id, uint32_t size_penalty_count,
                                 ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality) {
    const int iterations = score_iterations_for_quality(quality, deep);
    ScoreResult result;
    memset(&result, 0, sizeof(result));
    result.zero_score = score_zero(subject, &result.fail_flags);
    result.eval_count += 4;
    result.collision_score = score_collisions(subject, scratch, mix_seed(run_seed, subject_id, 11), iterations, &result.fail_flags, &result.eval_count);
    result.bucket_score = score_buckets(subject, scratch, mix_seed(run_seed, subject_id, 22), deep ? iterations * 2 : iterations, deep, &result.fail_flags, &result.eval_count);
    result.avalanche_score = score_avalanche(subject, scratch, mix_seed(run_seed, subject_id, 33), iterations, deep, &result.fail_flags, &result.eval_count);
    result.differential_score = score_differentials(subject, scratch, mix_seed(run_seed, subject_id, 44), iterations, deep, &result.fail_flags, &result.eval_count);
    result.sensitivity_score = score_input_sensitivity(subject, mix_seed(run_seed, subject_id, 55), iterations, &result.fail_flags, &result.eval_count);
    result.size_penalty = -((int64_t)size_penalty_count * 20);
    result.score += result.zero_score;
    result.score += result.collision_score;
    result.score += result.bucket_score;
    result.score += result.avalanche_score;
    result.score += result.differential_score;
    result.score += result.sensitivity_score;
    result.score += result.size_penalty;
    return result;
}

ScoreResult score_candidate(const Candidate *candidate, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality) {
    HashSubject subject = { eval_candidate_subject, (void *)candidate };
    ScoreResult result = score_subject(&subject, candidate->id, candidate->instruction_count, scratch, run_seed, deep, quality);
    if (!writes_hash(candidate)) {
        result.fail_flags |= FAIL_NO_HASH;
        result.score -= 1000000;
    }
    return result;
}

ScoreResult score_baseline_case(const BaselineCase *baseline, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality) {
    BaselineEvalCtx ctx = { baseline->fn };
    HashSubject subject = { eval_baseline_subject, &ctx };
    return score_subject(&subject, baseline->id, 0, scratch, run_seed, deep, quality);
}

int compare_candidates(const void *a_ptr, const void *b_ptr) {
    const Candidate *a = (const Candidate *)a_ptr;
    const Candidate *b = (const Candidate *)b_ptr;
    uint32_t a_severity = fail_severity(a->fail_flags);
    uint32_t b_severity = fail_severity(b->fail_flags);
    if (a_severity != b_severity) return a_severity < b_severity ? -1 : 1;
    if (a->fail_flags != b->fail_flags) return a->fail_flags < b->fail_flags ? -1 : 1;
    if (a->deep_score != INT64_MIN && b->deep_score != INT64_MIN) {
        if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score ? -1 : 1;
    }
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score ? -1 : 1;
    if (a->instruction_count != b->instruction_count) return a->instruction_count < b->instruction_count ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

const char *improvement_reason_name(uint8_t reason) {
    switch ((ImprovementReason)reason) {
        case IMPROVEMENT_FIRST: return "first";
        case IMPROVEMENT_QUICK: return "quick";
        case IMPROVEMENT_DEEP: return "deep";
        case IMPROVEMENT_FLAGS: return "flags";
        case IMPROVEMENT_TIE_BREAK: return "tie-break";
        default: return "unknown";
    }
}

const char *export_selection_name(uint8_t selection) {
    switch ((ExportSelection)selection) {
        case EXPORT_SELECTION_SAME: return "same";
        case EXPORT_SELECTION_QUICK: return "quick";
        case EXPORT_SELECTION_DEEP_SEEN: return "deep-seen";
        default: return "unknown";
    }
}

int deep_export_candidate_better(const Candidate *a, const Candidate *b) {
    uint32_t a_severity = fail_severity(a->fail_flags);
    uint32_t b_severity = fail_severity(b->fail_flags);
    if (a_severity != b_severity) return a_severity < b_severity;
    if (a->fail_flags != b->fail_flags) return a->fail_flags < b->fail_flags;
    if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score;
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score;
    if (a->instruction_count != b->instruction_count) return a->instruction_count < b->instruction_count;
    return a->id < b->id;
}

void update_best_deep_seen(Candidate *best_deep_seen, int *has_best_deep_seen, const Candidate *candidate) {
    if (candidate->deep_score == INT64_MIN) return;
    if (!*has_best_deep_seen || deep_export_candidate_better(candidate, best_deep_seen)) {
        *best_deep_seen = *candidate;
        *has_best_deep_seen = 1;
    }
}

uint8_t improvement_reason_for(const Candidate *previous, const Candidate *next) {
    if (previous->quick_score == INT64_MIN) return IMPROVEMENT_FIRST;
    if (fail_severity(next->fail_flags) < fail_severity(previous->fail_flags) ||
        next->fail_flags < previous->fail_flags) {
        return IMPROVEMENT_FLAGS;
    }
    if (next->deep_score != INT64_MIN && previous->deep_score != INT64_MIN &&
        next->deep_score > previous->deep_score) {
        return IMPROVEMENT_DEEP;
    }
    if (next->quick_score > previous->quick_score) return IMPROVEMENT_QUICK;
    return IMPROVEMENT_TIE_BREAK;
}

void append_improvement_event(struct ImprovementLog *log, const Candidate *candidate,
                                     uint64_t run_generation, double elapsed_seconds,
                                     uint64_t total_candidates, uint8_t reason) {
    struct ImprovementEvent event;
    memset(&event, 0, sizeof(event));
    event.run_generation = run_generation;
    event.elapsed_seconds = elapsed_seconds;
    event.candidate_id = candidate->id;
    event.parent_id = candidate->parent_id;
    event.candidate_generation = candidate->generation;
    event.instruction_count = candidate->instruction_count;
    event.quick_score = candidate->quick_score;
    event.deep_score = candidate->deep_score;
    event.deep_known = candidate->deep_score != INT64_MIN ? 1u : 0u;
    event.fail_flags = candidate->fail_flags;
    event.total_candidates = total_candidates;
    event.source = candidate->source;
    event.reason = reason;

    log->total_count++;
    if (log->count < MAX_IMPROVEMENT_EVENTS) {
        log->events[log->count++] = event;
        return;
    }

    memmove(&log->events[1], &log->events[2],
            (MAX_IMPROVEMENT_EVENTS - 2u) * sizeof(log->events[0]));
    log->events[MAX_IMPROVEMENT_EVENTS - 1u] = event;
    log->omitted_count++;
}

const struct ImprovementEvent *last_improvement_event(const struct ImprovementLog *log) {
    if (!log || log->count == 0) return NULL;
    return &log->events[log->count - 1u];
}

