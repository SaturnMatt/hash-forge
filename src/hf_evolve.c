#include "hf_core.h"

static int self_check(int condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "self-test failed: %s\n", name);
        return 0;
    }
    return 1;
}

static int run_vm_self_tests(void) {
    Candidate c;
    if (!self_check(popcount64(0ull) == 0, "popcount zero")) return 0;
    if (!self_check(popcount64(1ull) == 1, "popcount one")) return 0;
    if (!self_check(popcount64(0xffffffffffffffffull) == 64, "popcount all bits")) return 0;
    if (!self_check(popcount64(0xaaaaaaaaaaaaaaaaull) == 32, "popcount alternating bits")) return 0;
    if (!self_check(popcount64(0x8000000000000000ull) == 1, "popcount high bit")) return 0;

    Instruction mov_key[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 }
    };
    Instruction add_const[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_ADD, 2, OPERAND_CONST, 0, 1, 5 }
    };
    Instruction mul_const[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_MUL, 2, OPERAND_CONST, 0, 1, 3 }
    };
    Instruction xor_seed[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_XOR, 2, OPERAND_REG, 1, 1, 0 }
    };
    Instruction shl_hash[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_SHL, 2, OPERAND_CONST, 0, 4, 0 }
    };
    Instruction shr_hash[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_SHR, 2, OPERAND_CONST, 0, 4, 0 }
    };
    Instruction rotl_hash[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_ROTL, 2, OPERAND_CONST, 0, 8, 0 }
    };
    Instruction rotr_hash[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_ROTR, 2, OPERAND_CONST, 0, 8, 0 }
    };

    make_program(&c, mov_key, 1);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == 0x1234ull, "VM MOV key")) return 0;
    make_program(&c, add_const, 2);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == 0x1239ull, "VM ADD constant")) return 0;
    make_program(&c, mul_const, 2);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == 0x369cull, "VM MUL constant")) return 0;
    make_program(&c, xor_seed, 2);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == (0x1234ull ^ 0x99ull), "VM XOR seed")) return 0;
    make_program(&c, shl_hash, 2);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == 0x12340ull, "VM SHL")) return 0;
    make_program(&c, shr_hash, 2);
    if (!self_check(eval_candidate(&c, 0x1234ull, 0x99ull) == 0x123ull, "VM SHR")) return 0;
    make_program(&c, rotl_hash, 2);
    if (!self_check(eval_candidate(&c, 0x0123456789abcdefull, 0x99ull) == rotl64(0x0123456789abcdefull, 8), "VM ROTL")) return 0;
    make_program(&c, rotr_hash, 2);
    if (!self_check(eval_candidate(&c, 0x0123456789abcdefull, 0x99ull) == rotr64(0x0123456789abcdefull, 8), "VM ROTR")) return 0;

    printf("vm tests: pass\n");
    return 1;
}

static int run_calibration_self_tests(void) {
    typedef void (*Maker)(Candidate *);
    typedef struct CalibrationCase {
        const char *name;
        Maker maker;
        uint32_t expected_flags;
    } CalibrationCase;

    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) {
        fprintf(stderr, "failed to allocate score scratch\n");
        return 0;
    }

    Candidate baseline;
    make_reasonable_baseline(&baseline);
    ScoreResult baseline_score = score_candidate(&baseline, scratch, 1234, 1, QUALITY_NORMAL);
    printf("baseline_mixer score=%lld flags=0x%x\n", (long long)baseline_score.score, baseline_score.fail_flags);

    Candidate starter;
    make_compact_starter(&starter);
    ScoreResult starter_score = score_candidate(&starter, scratch, 1234, 1, QUALITY_NORMAL);
    printf("compact_starter score=%lld flags=0x%x\n", (long long)starter_score.score, starter_score.fail_flags);
    if (!self_check(starter.instruction_count <= MAX_PROGRAM_LEN, "compact starter stays within generator instruction bound")) {
        free(scratch);
        return 0;
    }
    if (!self_check((starter_score.fail_flags & (FAIL_ZERO | FAIL_BUCKET | FAIL_DIFFERENTIAL | FAIL_SENSITIVITY | FAIL_NO_HASH)) == 0,
                    "compact starter core flags")) {
        free(scratch);
        return 0;
    }

    if (!self_check((baseline_score.fail_flags & (FAIL_ZERO | FAIL_BUCKET | FAIL_DIFFERENTIAL | FAIL_SENSITIVITY | FAIL_NO_HASH)) == 0,
                    "baseline mixer core flags")) {
        free(scratch);
        return 0;
    }

    CalibrationCase cases[] = {
        { "bad_constant", make_constant_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE | FAIL_DIFFERENTIAL | FAIL_SENSITIVITY },
        { "bad_key_only", make_key_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE | FAIL_DIFFERENTIAL | FAIL_SENSITIVITY },
        { "bad_seed_only", make_seed_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE | FAIL_DIFFERENTIAL | FAIL_SENSITIVITY },
        { "bad_xor_only", make_xor_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE | FAIL_DIFFERENTIAL }
    };

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Candidate bad;
        cases[i].maker(&bad);
        ScoreResult bad_score = score_candidate(&bad, scratch, 1234, 1, QUALITY_NORMAL);
        printf("%s score=%lld flags=0x%x\n", cases[i].name, (long long)bad_score.score, bad_score.fail_flags);
        if (!self_check((bad_score.fail_flags & cases[i].expected_flags) == cases[i].expected_flags,
                        "bad hash expected flags")) {
            free(scratch);
            return 0;
        }
        if (!self_check(baseline_score.score > bad_score.score, "baseline ranks above bad hash")) {
            free(scratch);
            return 0;
        }
    }

    free(scratch);
    printf("calibration tests: pass\n");
    return 1;
}

static int run_generation_self_tests(void) {
    Rng a_rng = { 0x123456789abcdef0ull };
    Rng b_rng = { 0x123456789abcdef0ull };
    uint32_t op_seen[OP_COUNT];
    memset(op_seen, 0, sizeof(op_seen));

    for (uint32_t i = 0; i < 128; i++) {
        Candidate a;
        Candidate b;
        random_candidate(&a, &a_rng);
        random_candidate(&b, &b_rng);
        if (!self_check(candidate_is_valid(&a), "random candidate validity")) return 0;
        if (!self_check(candidate_is_valid(&b), "random candidate validity duplicate")) return 0;
        if (!self_check(a.id == b.id && a.instruction_count == b.instruction_count &&
                        memcmp(a.instructions, b.instructions, a.instruction_count * sizeof(a.instructions[0])) == 0,
                        "random candidate determinism")) return 0;

        Rng ma_rng = { 0xfedcba9876543210ull + i };
        Rng mb_rng = { 0xfedcba9876543210ull + i };
        Candidate child_a;
        Candidate child_b;
        mutate_candidate(&child_a, &a, &ma_rng);
        mutate_candidate(&child_b, &a, &mb_rng);
        if (!self_check(candidate_is_valid(&child_a), "mutated candidate validity")) return 0;
        if (!self_check(child_a.parent_id == a.id, "mutated candidate parent id")) return 0;
        if (!self_check(child_a.generation == a.generation + 1, "mutated candidate generation")) return 0;
        if (!self_check(child_a.id == child_b.id && child_a.instruction_count == child_b.instruction_count &&
                        memcmp(child_a.instructions, child_b.instructions, child_a.instruction_count * sizeof(child_a.instructions[0])) == 0,
                        "mutated candidate determinism")) return 0;

        Rng ca_rng = { 0x0f0e0d0c0b0a0908ull + i };
        Rng cb_rng = { 0x0f0e0d0c0b0a0908ull + i };
        Candidate cross_a;
        Candidate cross_b;
        crossover_candidate(&cross_a, &a, &b, &ca_rng);
        crossover_candidate(&cross_b, &a, &b, &cb_rng);
        if (!self_check(candidate_is_valid(&cross_a), "crossover candidate validity")) return 0;
        if (!self_check(cross_a.generation == ((a.generation > b.generation ? a.generation : b.generation) + 1),
                        "crossover candidate generation")) return 0;
        if (!self_check(cross_a.id == cross_b.id && cross_a.instruction_count == cross_b.instruction_count &&
                        memcmp(cross_a.instructions, cross_b.instructions, cross_a.instruction_count * sizeof(cross_a.instructions[0])) == 0,
                        "crossover candidate determinism")) return 0;
    }

    {
        Candidate existing[1];
        Candidate duplicate;
        random_candidate(&existing[0], &a_rng);
        duplicate = existing[0];
        int uniqueness = ensure_unique_candidate(&duplicate, existing, 1, &a_rng);
        if (!self_check(uniqueness != 0, "unique repair reports duplicate change")) return 0;
        if (!self_check(candidate_is_valid(&duplicate), "unique repair candidate validity")) return 0;
        if (!self_check(duplicate.id != existing[0].id, "unique repair changes duplicate id")) return 0;
    }

    Rng op_rng = { 0x55aa55aa55aa55aaull };
    for (uint32_t i = 0; i < 512; i++) {
        Instruction ins;
        random_instruction(&ins, &op_rng, 0);
        if (ins.op < OP_COUNT) op_seen[ins.op]++;
        if (!self_check(!(ins.op == OP_MOV && ins.operand_kind == OPERAND_REG && ins.operand_reg == ins.dst),
                        "random instruction avoids self MOV")) return 0;
        if (!self_check(!(ins.op == OP_MUL && ins.operand_kind == OPERAND_CONST && ins.constant == 1ull),
                        "random instruction avoids multiply by one")) return 0;
        if (!self_check(!((ins.op == OP_ADD || ins.op == OP_XOR) && ins.operand_kind == OPERAND_CONST && ins.constant == 0),
                        "random instruction avoids zero add/xor constants")) return 0;
    }
    for (uint32_t i = 0; i < OP_COUNT; i++) {
        if (!self_check(op_seen[i] > 0, "weighted opcode sampler reaches every op")) return 0;
    }

    Instruction repaired = { OP_MOV, 2, OPERAND_REG, 2, 0, 0 };
    repair_instruction(&repaired, &op_rng);
    if (!self_check(repaired.operand_reg != repaired.dst, "repair fixes self MOV")) return 0;
    repaired = (Instruction){ OP_MUL, 2, OPERAND_CONST, 0, 99, 1 };
    repair_instruction(&repaired, &op_rng);
    if (!self_check(repaired.constant != 1ull && (repaired.constant & 1ull), "repair fixes multiply by one")) return 0;
    repaired = (Instruction){ OP_ADD, 2, OPERAND_CONST, 0, 99, 0 };
    repair_instruction(&repaired, &op_rng);
    if (!self_check(repaired.constant != 0, "repair fixes zero ADD constant")) return 0;

    {
        Rng final_rng = { 0x1234abcd9876ef00ull };
        Candidate trailing;
        memset(&trailing, 0, sizeof(trailing));
        trailing.instruction_count = 10;
        for (uint32_t i = 0; i < trailing.instruction_count; i++) {
            trailing.instructions[i] = (Instruction){ OP_XOR, 2, OPERAND_CONST, 0, 1, 0x9e3779b97f4a7c15ull + i };
        }
        trailing.instructions[8].dst = 3;
        trailing.instructions[9].dst = 4;
        finalize_candidate(&trailing, &final_rng);
        if (!self_check(trailing.instruction_count == 8, "finalize trims trailing dead instructions")) return 0;
        if (!self_check(candidate_is_valid(&trailing), "trimmed candidate remains valid")) return 0;

        Candidate no_hash;
        memset(&no_hash, 0, sizeof(no_hash));
        no_hash.instruction_count = MIN_PROGRAM_LEN;
        for (uint32_t i = 0; i < no_hash.instruction_count; i++) {
            no_hash.instructions[i] = (Instruction){ OP_ADD, 3, OPERAND_CONST, 0, 1, 3 + i };
        }
        finalize_candidate(&no_hash, &final_rng);
        if (!self_check(writes_hash(&no_hash), "finalize repairs missing hash write")) return 0;
        if (!self_check(candidate_is_valid(&no_hash), "finalized no-hash candidate remains valid")) return 0;

        Candidate export_source;
        Candidate export_pruned;
        memset(&export_source, 0, sizeof(export_source));
        export_source.instruction_count = 7;
        export_source.instructions[0] = (Instruction){ OP_MOV, 3, OPERAND_REG, 0, 1, 0 };
        export_source.instructions[1] = (Instruction){ OP_XOR, 3, OPERAND_REG, 1, 1, 0 };
        export_source.instructions[2] = (Instruction){ OP_MUL, 3, OPERAND_CONST, 0, 1, 0x9e3779b97f4a7c15ull };
        export_source.instructions[3] = (Instruction){ OP_MOV, 2, OPERAND_REG, 3, 1, 0 };
        export_source.instructions[4] = (Instruction){ OP_XOR, 4, OPERAND_CONST, 0, 1, 0xd1b54a32d192ed03ull };
        export_source.instructions[5] = (Instruction){ OP_MUL, 4, OPERAND_CONST, 0, 1, 0xbf58476d1ce4e5b9ull };
        export_source.instructions[6] = (Instruction){ OP_ADD, 4, OPERAND_REG, 3, 1, 0 };
        export_source.id = candidate_id(&export_source);
        prune_candidate_for_export(&export_source, &export_pruned);
        if (!self_check(export_pruned.instruction_count == 4, "export pruning removes dead instructions")) return 0;
        if (!self_check(eval_candidate(&export_source, 123, 456) == eval_candidate(&export_pruned, 123, 456),
                        "export pruning preserves output")) return 0;
    }

    printf("generation tests: pass\n");
    return 1;
}

static int run_selection_self_tests(void) {
    Candidate a;
    Candidate b;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.fail_flags = FAIL_COLLISION;
    b.fail_flags = 0;
    a.quick_score = 1000000;
    b.quick_score = -1000000;
    a.deep_score = 1000000;
    b.deep_score = -1000000;
    a.instruction_count = b.instruction_count = 8;
    a.id = 1;
    b.id = 2;
    if (!self_check(compare_candidates(&a, &b) > 0, "selection prioritizes fail flags")) return 0;

    a.fail_flags = FAIL_COLLISION;
    b.fail_flags = FAIL_ZERO;
    a.quick_score = -1000000;
    b.quick_score = 1000000;
    a.deep_score = -1000000;
    b.deep_score = 1000000;
    if (!self_check(compare_candidates(&a, &b) < 0, "selection weighs failure severity")) return 0;

    a.fail_flags = FAIL_COLLISION;
    b.fail_flags = FAIL_COLLISION | FAIL_AVALANCHE;
    if (!self_check(compare_candidates(&a, &b) < 0, "selection prefers fewer failure flags")) return 0;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.quick_score = 1000;
    b.quick_score = 10;
    a.deep_score = 100;
    b.deep_score = 200;
    a.instruction_count = b.instruction_count = 8;
    a.id = 1;
    b.id = 2;
    if (!self_check(compare_candidates(&a, &b) > 0, "selection prioritizes deep score")) return 0;

    a.deep_score = INT64_MIN;
    b.deep_score = -1;
    a.quick_score = 1000000;
    b.quick_score = -1000000;
    if (!self_check(compare_candidates(&a, &b) < 0, "selection avoids pending deep lock-in")) return 0;

    a.deep_score = INT64_MIN;
    b.deep_score = INT64_MIN;
    a.quick_score = 100;
    b.quick_score = 200;
    if (!self_check(compare_candidates(&a, &b) > 0, "selection falls back to quick score")) return 0;

    a.quick_score = b.quick_score = 100;
    a.instruction_count = 10;
    b.instruction_count = 8;
    if (!self_check(compare_candidates(&a, &b) > 0, "selection prefers shorter programs")) return 0;

    printf("selection tests: pass\n");
    return 1;
}

static void score_candidate_range(ScoreTask *task) {
    for (uint32_t i = task->start; i < task->end; i++) {
        Candidate *candidate = &task->candidates[i];
        ScoreResult score = score_candidate(candidate, task->scratch, task->seed, task->deep, task->quality);
        if (task->deep) {
            candidate->deep_score = score.score;
            candidate->fail_flags |= score.fail_flags;
        } else {
            candidate->quick_score = score.score;
            candidate->fail_flags = score.fail_flags;
            candidate->speed_hint = score.eval_count / (candidate->instruction_count ? candidate->instruction_count : 1);
        }
    }
}

#ifdef _WIN32
static unsigned __stdcall score_thread_main(void *arg) {
    ScoreWorker *worker = (ScoreWorker *)arg;
    for (;;) {
        WaitForSingleObject(worker->start_event, INFINITE);
        if (InterlockedCompareExchange(&worker->should_stop, 0, 0)) {
            break;
        }
        score_candidate_range(&worker->task);
        SetEvent(worker->done_event);
    }
    return 0;
}
#endif

uint32_t default_thread_count(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (info.dwNumberOfProcessors == 0) return 1;
    return info.dwNumberOfProcessors > MAX_SCORE_THREADS ? MAX_SCORE_THREADS : info.dwNumberOfProcessors;
#else
    return 1;
#endif
}

uint32_t clamp_thread_count(uint64_t requested, uint32_t candidate_count) {
    uint64_t count = requested;
    if (count == 0) count = default_thread_count();
    if (count > MAX_SCORE_THREADS) count = MAX_SCORE_THREADS;
#ifndef _WIN32
    if (count > 1) count = 1;
#endif
    if (candidate_count != 0 && count > candidate_count) count = candidate_count;
    if (count == 0) count = 1;
    return (uint32_t)count;
}

void score_pool_destroy(ScorePool *pool) {
#ifdef _WIN32
    if (pool->workers) {
        for (uint32_t i = 0; i < pool->thread_count; i++) {
            ScoreWorker *worker = &pool->workers[i];
            if (worker->thread) {
                InterlockedExchange(&worker->should_stop, 1);
                SetEvent(worker->start_event);
                WaitForSingleObject(worker->thread, INFINITE);
                CloseHandle(worker->thread);
            }
            if (worker->start_event) CloseHandle(worker->start_event);
            if (worker->done_event) CloseHandle(worker->done_event);
        }
    }
#endif
    free(pool->workers);
    free(pool->scratches);
    memset(pool, 0, sizeof(*pool));
}

int score_pool_init(ScorePool *pool, uint32_t requested_threads, uint32_t candidate_count) {
    memset(pool, 0, sizeof(*pool));
    pool->thread_count = clamp_thread_count(requested_threads, candidate_count);
    pool->workers = (ScoreWorker *)calloc(pool->thread_count, sizeof(*pool->workers));
    pool->scratches = (ScoreScratch *)calloc(pool->thread_count, sizeof(*pool->scratches));
    if (!pool->workers || !pool->scratches) {
        score_pool_destroy(pool);
        return 0;
    }
    for (uint32_t i = 0; i < pool->thread_count; i++) {
        pool->workers[i].task.scratch = &pool->scratches[i];
    }
#ifdef _WIN32
    if (pool->thread_count > 1) {
        for (uint32_t i = 0; i < pool->thread_count; i++) {
            ScoreWorker *worker = &pool->workers[i];
            worker->start_event = CreateEventA(NULL, FALSE, FALSE, NULL);
            worker->done_event = CreateEventA(NULL, TRUE, FALSE, NULL);
            worker->thread = (HANDLE)_beginthreadex(NULL, 0, score_thread_main, worker, 0, NULL);
            if (!worker->start_event || !worker->done_event || !worker->thread) {
                score_pool_destroy(pool);
                return 0;
            }
        }
    }
#endif
    return 1;
}

int score_pool_score(ScorePool *pool, Candidate *candidates, uint32_t candidate_count, uint64_t seed, int deep, QualityMode quality) {
    uint32_t active_threads = clamp_thread_count(pool->thread_count, candidate_count);
    if (active_threads == 1) {
        ScoreTask *task = &pool->workers[0].task;
        task->candidates = candidates;
        task->start = 0;
        task->end = candidate_count;
        task->seed = seed;
        task->deep = deep;
        task->quality = quality;
        task->scratch = &pool->scratches[0];
        score_candidate_range(task);
        return 1;
    }

#ifdef _WIN32
    HANDLE done_events[MAX_SCORE_THREADS];
    for (uint32_t t = 0; t < active_threads; t++) {
        ScoreWorker *worker = &pool->workers[t];
        ScoreTask *task = &worker->task;
        task->candidates = candidates;
        task->start = (candidate_count * t) / active_threads;
        task->end = (candidate_count * (t + 1)) / active_threads;
        task->seed = seed;
        task->deep = deep;
        task->quality = quality;
        task->scratch = &pool->scratches[t];
        ResetEvent(worker->done_event);
        done_events[t] = worker->done_event;
        SetEvent(worker->start_event);
    }
    return WaitForMultipleObjects(active_threads, done_events, TRUE, INFINITE) != WAIT_FAILED;
#else
    (void)active_threads;
    ScoreTask *task = &pool->workers[0].task;
    task->candidates = candidates;
    task->start = 0;
    task->end = candidate_count;
    task->seed = seed;
    task->deep = deep;
    task->quality = quality;
    task->scratch = &pool->scratches[0];
    score_candidate_range(task);
    return 1;
#endif
}

static int generation_limit_reached(const RunOptions *options, uint64_t generation) {
    return options->generations != 0 && generation >= options->generations;
}

static int seconds_limit_reached(const RunOptions *options, double start_seconds) {
    return options->have_seconds && elapsed_wall_seconds_since(start_seconds) >= (double)options->seconds;
}

static const char *stop_reason_for(const RunOptions *options, uint64_t generation, double elapsed_seconds) {
    int hit_generation = generation_limit_reached(options, generation);
    int hit_seconds = options->have_seconds && elapsed_seconds >= (double)options->seconds;
    if (g_stop_requested) return "interrupted";
    if (hit_generation && hit_seconds) return "generation and time limit";
    if (hit_generation) return "generation limit";
    if (hit_seconds) return "time limit";
    return "stopped";
}

static uint32_t crossover_count_for_options(const RunOptions *options) {
    return options->no_crossover ? 0u : CROSSOVER_COUNT;
}

static uint32_t novelty_lane_for_options(const RunOptions *options) {
    if (options->no_novelty) return 0;
    return options->have_novelty_lane ? options->novelty_lane : DEFAULT_NOVELTY_LANE;
}

static int candidate_is_starter_lineage(const Candidate *candidate) {
    return candidate->source == SOURCE_STARTER;
}

static uint32_t starter_cap_for_options(const RunOptions *options) {
    if (!options->starter_cap_enabled) return SURVIVOR_COUNT;
    return options->starter_cap > SURVIVOR_COUNT ? SURVIVOR_COUNT : options->starter_cap;
}

static uint64_t starter_cap_after_for_options(const RunOptions *options) {
    return options->starter_cap_enabled ? options->starter_cap_after : 0u;
}

static uint32_t count_starter_survivors(const Candidate *population) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < SURVIVOR_COUNT; i++) {
        if (candidate_is_starter_lineage(&population[i])) count++;
    }
    return count;
}

static uint32_t apply_starter_survivor_cap(Candidate *population, uint64_t generation,
                                           uint32_t cap, uint64_t cap_after,
                                           uint32_t *before_count, uint32_t *after_count) {
    *before_count = count_starter_survivors(population);
    *after_count = *before_count;
    if (cap >= SURVIVOR_COUNT || generation <= cap_after || *before_count <= cap) {
        return 0;
    }

    uint32_t starter_seen = 0;
    uint32_t displaced = 0;
    for (uint32_t i = 0; i < SURVIVOR_COUNT; i++) {
        if (!candidate_is_starter_lineage(&population[i])) continue;
        starter_seen++;
        if (starter_seen <= cap) continue;

        uint32_t replacement = POPULATION_SIZE;
        for (uint32_t j = SURVIVOR_COUNT; j < POPULATION_SIZE; j++) {
            if (!candidate_is_starter_lineage(&population[j])) {
                replacement = j;
                break;
            }
        }
        if (replacement == POPULATION_SIZE) break;
        Candidate tmp = population[i];
        population[i] = population[replacement];
        population[replacement] = tmp;
        displaced++;
    }
    *after_count = count_starter_survivors(population);
    return displaced;
}

static uint64_t refresh_window_for_options(const RunOptions *options) {
    if (options->no_refresh) return 0;
    return options->refresh_window ? options->refresh_window : STAGNATION_REFRESH_GENERATIONS;
}

static uint32_t refresh_immigrants_for_options(const RunOptions *options) {
    if (options->no_refresh) return IMMIGRANT_COUNT;
    return options->refresh_immigrants ? options->refresh_immigrants : STAGNATION_IMMIGRANT_COUNT;
}

static void print_run_header(const RunOptions *options, uint32_t score_threads) {
    uint32_t crossover_count = crossover_count_for_options(options);
    uint32_t novelty_lane = novelty_lane_for_options(options);
    printf("\n%s%sHash Forge run%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sseed%s         %llu\n", c_dim(), c_reset(), (unsigned long long)options->seed);
    printf("  %sgenerations%s  ", c_dim(), c_reset());
    if (options->generations) printf("%llu\n", (unsigned long long)options->generations);
    else printf("unlimited\n");
    printf("  %sseconds%s      ", c_dim(), c_reset());
    if (options->have_seconds) printf("%llu\n", (unsigned long long)options->seconds);
    else printf("unlimited\n");
    printf("  %squality%s      %s\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("  %sthreads%s      %u scoring worker%s%s\n",
           c_dim(), c_reset(), score_threads, score_threads == 1 ? "" : "s",
           options->auto_threads ? " (auto)" : "");
    printf("  %spopulation%s   %u candidates, %u survivors, %u novelty, %u crossover, %u immigrants\n",
           c_dim(), c_reset(), POPULATION_SIZE, SURVIVOR_COUNT, novelty_lane, crossover_count, IMMIGRANT_COUNT);
    printf("  %schampions%s    %s\n", c_dim(), c_reset(), options->no_champions ? "disabled" : "enabled");
    printf("\n%s%6s  %7s  %10s  %10s  %12s  %12s  %8s  %3s  %16s%s\n",
           c_dim(), "gen", "time", "progress", "candidates", "quick", "deep", "flags", "len", "best id", c_reset());
}

static void format_progress(const RunOptions *options, uint64_t generation, double elapsed, char *buffer, size_t size) {
    if (options->generations) {
        unsigned pct = (unsigned)((generation * 100u) / options->generations);
        if (pct > 100u) pct = 100u;
        snprintf(buffer, size, "%3u%% gen", pct);
    } else if (options->have_seconds) {
        unsigned pct = (unsigned)((elapsed * 100.0) / (double)options->seconds);
        if (pct > 100u) pct = 100u;
        snprintf(buffer, size, "%3u%% sec", pct);
    } else {
        snprintf(buffer, size, "open");
    }
}

static void print_progress_line(const RunOptions *options, uint64_t generation, double elapsed, const Candidate *candidate, uint64_t quick_evals, uint64_t deep_evals) {
    char deep_text[32];
    char progress[24];
    uint64_t total_evals = quick_evals + deep_evals;
    double rate = elapsed > 0.0 ? (double)total_evals / elapsed : 0.0;
    const char *flag_color = candidate->fail_flags ? c_yellow() : c_green();
    if (candidate->deep_score == INT64_MIN) {
        strcpy(deep_text, "pending");
    } else {
        sprintf(deep_text, "%lld", (long long)candidate->deep_score);
    }
    format_progress(options, generation, elapsed, progress, sizeof(progress));
    printf("%s%6llu%s  %7.2fs  %10s  %10llu  %12lld  %12s  %s0x%02x%s  %3u  %s%016llx%s  %s%.0f/s%s\n",
           c_bold(),
           (unsigned long long)generation,
           c_reset(),
           elapsed,
           progress,
           (unsigned long long)total_evals,
           (long long)candidate->quick_score,
           deep_text,
           flag_color,
           candidate->fail_flags,
           c_reset(),
           candidate->instruction_count,
           c_cyan(),
           (unsigned long long)candidate->id,
           c_reset(),
           c_dim(),
           rate,
           c_reset());
}

static void print_final_report(const Candidate *candidate, const RunOptions *options, const RunReport *report, int exported) {
    const struct ImprovementEvent *last_improvement = last_improvement_event(&report->improvements);
    printf("\n%s%sRun complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %sstop reason%s        %s\n", c_dim(), c_reset(), report->stop_reason);
    printf("  %selapsed%s            %.3fs\n", c_dim(), c_reset(), report->elapsed_seconds);
    printf("  %sgenerations%s        %llu\n", c_dim(), c_reset(), (unsigned long long)report->run_generation);
    printf("  %squality%s            %s\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("  %sthreads%s            %u scoring worker%s\n", c_dim(), c_reset(), report->threads, report->threads == 1 ? "" : "s");
    printf("  %squick evals%s        %llu\n", c_dim(), c_reset(), (unsigned long long)report->quick_candidates_evaluated);
    printf("  %sdeep evals%s         %llu\n", c_dim(), c_reset(), (unsigned long long)report->deep_candidates_evaluated);
    printf("  %stotal evaluated%s    %llu candidate hash functions\n", c_dim(), c_reset(),
           (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));
    printf("  %sunique last gen%s     %u/%u candidates\n", c_dim(), c_reset(), report->last_unique_candidates, POPULATION_SIZE);
    printf("  %sduplicate repairs%s   %llu (%llu fresh replacements)\n", c_dim(), c_reset(),
           (unsigned long long)report->duplicate_repairs,
           (unsigned long long)report->duplicate_random_replacements);
    printf("  %sstagnation refresh%s  %llu (%llu extra immigrants)\n", c_dim(), c_reset(),
           (unsigned long long)report->stagnation_refreshes,
           (unsigned long long)report->adaptive_random_immigrants);
    printf("  %snovelty lane%s       %u (%llu admitted, last best %llu avg %llu)\n",
           c_dim(), c_reset(),
           report->novelty_lane,
           (unsigned long long)report->novelty_candidates_admitted,
           (unsigned long long)report->last_best_novelty_score,
           (unsigned long long)report->last_avg_novelty_score);
    printf("  %sstarter cap%s        %s cap %u after %llu (%llu displaced, last %u -> %u)\n",
           c_dim(), c_reset(),
           report->starter_cap_enabled ? "enabled" : "disabled",
           report->starter_cap,
           (unsigned long long)report->starter_cap_after,
           (unsigned long long)report->starter_cap_displacements,
           report->last_starter_survivors_before_cap,
           report->last_starter_survivors_after_cap);
    printf("  %simprovements%s       %llu", c_dim(), c_reset(),
           (unsigned long long)report->improvements.total_count);
    if (last_improvement) {
        printf(" (last gen %llu at %.3fs)",
               (unsigned long long)last_improvement->run_generation,
               last_improvement->elapsed_seconds);
    }
    printf("\n");
    printf("  %schampion starters%s   %u\n", c_dim(), c_reset(), report->champion_starters_loaded);
    printf("  %sexport selection%s    %s\n", c_dim(), c_reset(), export_selection_name(report->export_selection));
    printf("  %squick tracker%s       %s%016llx%s  q=%lld d=%lld flags=0x%x\n",
           c_dim(), c_reset(), c_cyan(), (unsigned long long)report->best_quick_seen.id, c_reset(),
           (long long)report->best_quick_seen.quick_score,
           (long long)report->best_quick_seen.deep_score,
           report->best_quick_seen.fail_flags);
    if (report->has_best_deep_seen) {
        printf("  %sdeep-seen tracker%s   %s%016llx%s  q=%lld d=%lld flags=0x%x\n",
               c_dim(), c_reset(), c_cyan(), (unsigned long long)report->best_deep_seen.id, c_reset(),
               (long long)report->best_deep_seen.quick_score,
               (long long)report->best_deep_seen.deep_score,
               report->best_deep_seen.fail_flags);
    } else {
        printf("  %sdeep-seen tracker%s   none\n", c_dim(), c_reset());
    }
    printf("  %sbest id%s            %s%016llx%s\n", c_dim(), c_reset(), c_cyan(), (unsigned long long)candidate->id, c_reset());
    printf("  %sbest source%s        %s\n", c_dim(), c_reset(), candidate_source_name(candidate->source));
    printf("  %sbest quick%s         %lld\n", c_dim(), c_reset(), (long long)candidate->quick_score);
    printf("  %sbest deep%s          %lld\n", c_dim(), c_reset(), (long long)candidate->deep_score);
    printf("  %sbest flags%s         %s0x%x%s (", c_dim(), c_reset(), candidate->fail_flags ? c_yellow() : c_green(), candidate->fail_flags, c_reset());
    print_fail_flags(stdout, candidate->fail_flags);
    printf(")\n");
    printf("  %soutput%s             %s\n", c_dim(), c_reset(), exported ? "out/best.c, out/best.txt, out/report.md, out/improvements.csv, out/runs/*, out/summary.txt" : "export failed");
}

int command_self_test(void) {
    if (!run_vm_self_tests()) return 1;
    if (!run_calibration_self_tests()) return 1;
    if (!run_generation_self_tests()) return 1;
    if (!run_selection_self_tests()) return 1;
    printf("self-test: pass\n");
    return 0;
}

static uint32_t choose_auto_thread_count(Candidate *population, uint64_t seed) {
    uint32_t raw_choices[] = { 1, 2, 4, 8, 16, 32 };
    uint32_t choices[sizeof(raw_choices) / sizeof(raw_choices[0])];
    uint32_t choice_count = 0;
    uint32_t best_threads = 1;
    double best_rate = 0.0;
    const double seconds_per_choice = 0.04;

    for (uint32_t i = 0; i < sizeof(raw_choices) / sizeof(raw_choices[0]); i++) {
        uint32_t clamped = clamp_thread_count(raw_choices[i], POPULATION_SIZE);
        if (choice_count == 0 || choices[choice_count - 1] != clamped) {
            choices[choice_count++] = clamped;
        }
    }

    for (uint32_t i = 0; i < choice_count; i++) {
        ScorePool pool;
        if (!score_pool_init(&pool, choices[i], POPULATION_SIZE)) {
            continue;
        }

        uint64_t evaluated = 0;
        double start = wall_seconds_now();
        double now = start;
        do {
            if (!score_pool_score(&pool, population, POPULATION_SIZE, mix_seed(seed, choices[i], 77), 0, QUALITY_QUICK)) {
                score_pool_destroy(&pool);
                break;
            }
            evaluated += POPULATION_SIZE;
            now = wall_seconds_now();
        } while (now - start < seconds_per_choice);

        double elapsed = now - start;
        double rate = elapsed > 0.0 ? (double)evaluated / elapsed : 0.0;
        if (rate > best_rate) {
            best_rate = rate;
            best_threads = pool.thread_count;
        }
        score_pool_destroy(&pool);
    }

    return best_threads;
}

int run_evolution(const RunOptions *options, int print_status, Candidate *best_out, RunReport *report_out) {
    Candidate *population = (Candidate *)calloc(POPULATION_SIZE, sizeof(*population));
    Candidate *next = (Candidate *)calloc(POPULATION_SIZE, sizeof(*next));
    if (!population || !next) {
        fprintf(stderr, "failed to allocate run memory\n");
        free(population);
        free(next);
        return 1;
    }

    Rng rng = { options->seed };
    for (uint32_t i = 0; i < POPULATION_SIZE; i++) {
        random_candidate(&population[i], &rng);
    }
    if (!options->no_starter) {
        seed_starter_population(population, &rng);
    }
    uint32_t champion_starters_loaded = 0;
    if (!options->no_champions) {
        champion_starters_loaded = load_champion_starters(population, &rng, options->no_starter ? 0u : STARTER_COUNT);
    }

    uint64_t generation = 0;
    Candidate best_seen;
    memset(&best_seen, 0, sizeof(best_seen));
    best_seen.quick_score = INT64_MIN;
    best_seen.deep_score = INT64_MIN;
    Candidate best_deep_seen;
    memset(&best_deep_seen, 0, sizeof(best_deep_seen));
    best_deep_seen.quick_score = INT64_MIN;
    best_deep_seen.deep_score = INT64_MIN;
    int has_best_deep_seen = 0;
    struct ImprovementLog improvements;
    memset(&improvements, 0, sizeof(improvements));
    uint64_t quick_candidates_evaluated = 0;
    uint64_t deep_candidates_evaluated = 0;
    uint64_t duplicate_repairs = 0;
    uint64_t duplicate_random_replacements = 0;
    uint64_t stagnation_refreshes = 0;
    uint64_t adaptive_random_immigrants = 0;
    uint64_t novelty_candidates_admitted = 0;
    uint64_t last_best_novelty_score = 0;
    uint64_t last_avg_novelty_score = 0;
    uint32_t starter_cap = starter_cap_for_options(options);
    uint64_t starter_cap_after = starter_cap_after_for_options(options);
    uint64_t starter_cap_displacements = 0;
    uint32_t last_starter_survivors_before_cap = 0;
    uint32_t last_starter_survivors_after_cap = 0;
    uint64_t last_improvement_generation = 0;
    uint32_t last_unique_candidates = POPULATION_SIZE;
    uint32_t score_threads = options->auto_threads
        ? choose_auto_thread_count(population, options->seed)
        : clamp_thread_count(options->threads, POPULATION_SIZE);
    uint32_t crossover_count = crossover_count_for_options(options);
    uint32_t configured_novelty_lane = novelty_lane_for_options(options);
    uint64_t refresh_window = refresh_window_for_options(options);
    uint32_t refresh_immigrants = refresh_immigrants_for_options(options);
    uint32_t deep_every = deep_every_for_quality(options->quality);
    uint32_t deep_top_n = deep_top_n_for_quality(options->quality);
    ScorePool score_pool;
    double last_status_elapsed = -1.0;
    if (!score_pool_init(&score_pool, score_threads, POPULATION_SIZE)) {
        fprintf(stderr, "failed to start scoring worker pool\n");
        free(population);
        free(next);
        return 1;
    }
    score_threads = score_pool.thread_count;
    double start_seconds = wall_seconds_now();

    if (print_status) {
        print_run_header(options, score_threads);
    }

    while (!g_stop_requested &&
           !generation_limit_reached(options, generation) &&
           !seconds_limit_reached(options, start_seconds)) {
        generation++;
        if (!score_pool_score(&score_pool, population, POPULATION_SIZE, options->seed, 0, options->quality)) {
            fprintf(stderr, "failed to score population with %u thread(s)\n", score_threads);
            score_pool_destroy(&score_pool);
            free(population);
            free(next);
            return 1;
        }
        quick_candidates_evaluated += POPULATION_SIZE;
        qsort(population, POPULATION_SIZE, sizeof(population[0]), compare_candidates);
        last_unique_candidates = count_unique_candidate_ids(population, POPULATION_SIZE);

        int deep_generation = generation % deep_every == 0 || generation == 1 || generation_limit_reached(options, generation);
        if (deep_generation) {
            if (!score_pool_score(&score_pool, population, deep_top_n, options->seed, 1, options->quality)) {
                fprintf(stderr, "failed to deep-score candidates with %u thread(s)\n", score_threads);
                score_pool_destroy(&score_pool);
                free(population);
                free(next);
                return 1;
            }
            deep_candidates_evaluated += deep_top_n;
            for (uint32_t i = 0; i < deep_top_n; i++) {
                update_best_deep_seen(&best_deep_seen, &has_best_deep_seen, &population[i]);
            }
            qsort(population, POPULATION_SIZE, sizeof(population[0]), compare_candidates);
        }

        double now_elapsed = elapsed_wall_seconds_since(start_seconds);
        if (best_seen.quick_score == INT64_MIN ||
            compare_candidates(&population[0], &best_seen) < 0) {
            uint8_t improvement_reason = improvement_reason_for(&best_seen, &population[0]);
            best_seen = population[0];
            last_improvement_generation = generation;
            append_improvement_event(&improvements, &best_seen, generation, now_elapsed,
                                     quick_candidates_evaluated + deep_candidates_evaluated,
                                     improvement_reason);
        }

        if (print_status && (generation == 1 ||
            deep_generation ||
            generation_limit_reached(options, generation) ||
            seconds_limit_reached(options, start_seconds) ||
            last_status_elapsed < 0.0 ||
            now_elapsed - last_status_elapsed >= 0.25)) {
            print_progress_line(options, generation, now_elapsed, &population[0],
                                quick_candidates_evaluated, deep_candidates_evaluated);
            last_status_elapsed = now_elapsed;
        }

        starter_cap_displacements += apply_starter_survivor_cap(population, generation, starter_cap, starter_cap_after,
                                                                 &last_starter_survivors_before_cap,
                                                                 &last_starter_survivors_after_cap);

        for (uint32_t i = 0; i < SURVIVOR_COUNT; i++) {
            next[i] = population[i];
            int uniqueness = ensure_unique_candidate(&next[i], next, i, &rng);
            if (uniqueness) duplicate_repairs++;
            if (uniqueness == 2) duplicate_random_replacements++;
        }
        uint32_t immigrant_count = IMMIGRANT_COUNT;
        if (refresh_window &&
            generation > last_improvement_generation &&
            generation - last_improvement_generation >= refresh_window) {
            immigrant_count = refresh_immigrants;
            stagnation_refreshes++;
            if (refresh_immigrants > IMMIGRANT_COUNT) {
                adaptive_random_immigrants += refresh_immigrants - IMMIGRANT_COUNT;
            }
            last_improvement_generation = generation;
        }
        uint32_t immigrant_start = POPULATION_SIZE > immigrant_count ? POPULATION_SIZE - immigrant_count : SURVIVOR_COUNT;
        if (immigrant_start < SURVIVOR_COUNT) immigrant_start = SURVIVOR_COUNT;
        uint32_t available_child_slots = immigrant_start > SURVIVOR_COUNT ? immigrant_start - SURVIVOR_COUNT : 0u;
        uint32_t crossover_slots = crossover_count < available_child_slots ? crossover_count : available_child_slots;
        uint32_t novelty_limit = available_child_slots > crossover_slots ? available_child_slots - crossover_slots : 0u;
        uint32_t novelty_count = configured_novelty_lane < novelty_limit ? configured_novelty_lane : novelty_limit;
        uint32_t novelty_start = SURVIVOR_COUNT;
        uint32_t novelty_end = novelty_start + novelty_count;
        uint32_t crossover_start = immigrant_start - crossover_slots;
        if (crossover_start < novelty_end) crossover_start = novelty_end;

        last_best_novelty_score = 0;
        last_avg_novelty_score = 0;
        uint64_t novelty_score_total = 0;
        uint32_t novelty_score_count = 0;
        uint32_t selected_novelty_parents[POPULATION_SIZE];
        uint32_t selected_novelty_count = 0;
        for (uint32_t i = novelty_start; i < novelty_end; i++) {
            uint32_t best_parent = POPULATION_SIZE;
            uint64_t best_novelty = 0;
            for (uint32_t p = SURVIVOR_COUNT; p < POPULATION_SIZE; p++) {
                int already_selected = 0;
                for (uint32_t s = 0; s < selected_novelty_count; s++) {
                    if (selected_novelty_parents[s] == p) {
                        already_selected = 1;
                        break;
                    }
                }
                if (already_selected) continue;
                uint64_t novelty = candidate_novelty_score(&population[p], population, SURVIVOR_COUNT);
                if (best_parent == POPULATION_SIZE ||
                    novelty > best_novelty ||
                    (novelty == best_novelty && compare_candidates(&population[p], &population[best_parent]) < 0)) {
                    best_parent = p;
                    best_novelty = novelty;
                }
            }
            if (best_parent == POPULATION_SIZE) {
                best_parent = SURVIVOR_COUNT + rng_range(&rng, POPULATION_SIZE - SURVIVOR_COUNT);
                best_novelty = candidate_novelty_score(&population[best_parent], population, SURVIVOR_COUNT);
            }
            selected_novelty_parents[selected_novelty_count++] = best_parent;
            mutate_candidate(&next[i], &population[best_parent], &rng);
            next[i].source = SOURCE_NOVELTY;
            int uniqueness = ensure_unique_candidate(&next[i], next, i, &rng);
            if (uniqueness) duplicate_repairs++;
            if (uniqueness == 2) duplicate_random_replacements++;
            next[i].source = SOURCE_NOVELTY;
            novelty_candidates_admitted++;
            novelty_score_total += best_novelty;
            novelty_score_count++;
            if (best_novelty > last_best_novelty_score) last_best_novelty_score = best_novelty;
        }
        if (novelty_score_count) {
            last_avg_novelty_score = novelty_score_total / novelty_score_count;
        }

        for (uint32_t i = novelty_end; i < crossover_start; i++) {
            uint32_t r = rng_range(&rng, SURVIVOR_COUNT * SURVIVOR_COUNT);
            uint32_t parent_index = r / SURVIVOR_COUNT;
            if (parent_index >= SURVIVOR_COUNT) parent_index = SURVIVOR_COUNT - 1;
            mutate_candidate(&next[i], &population[parent_index], &rng);
            int uniqueness = ensure_unique_candidate(&next[i], next, i, &rng);
            if (uniqueness) duplicate_repairs++;
            if (uniqueness == 2) duplicate_random_replacements++;
        }
        for (uint32_t i = crossover_start; i < immigrant_start; i++) {
            uint32_t a = rng_range(&rng, SURVIVOR_COUNT);
            uint32_t b = rng_range(&rng, SURVIVOR_COUNT);
            crossover_candidate(&next[i], &population[a], &population[b], &rng);
            int uniqueness = ensure_unique_candidate(&next[i], next, i, &rng);
            if (uniqueness) duplicate_repairs++;
            if (uniqueness == 2) duplicate_random_replacements++;
        }
        for (uint32_t i = immigrant_start; i < POPULATION_SIZE; i++) {
            random_candidate(&next[i], &rng);
            next[i].generation = (uint32_t)generation;
            next[i].id = candidate_id(&next[i]);
            int uniqueness = ensure_unique_candidate(&next[i], next, i, &rng);
            if (uniqueness) duplicate_repairs++;
            if (uniqueness == 2) duplicate_random_replacements++;
        }

        Candidate *tmp = population;
        population = next;
        next = tmp;
    }

    if (!score_pool_score(&score_pool, &best_seen, 1, options->seed, 1, options->quality)) {
        fprintf(stderr, "failed to score final best candidate\n");
        score_pool_destroy(&score_pool);
        free(population);
        free(next);
        return 1;
    }
    deep_candidates_evaluated += 1;
    update_best_deep_seen(&best_deep_seen, &has_best_deep_seen, &best_seen);

    Candidate export_best_seen = best_seen;
    uint8_t export_selection = EXPORT_SELECTION_QUICK;
    if (has_best_deep_seen) {
        if (best_deep_seen.id == best_seen.id) {
            export_selection = EXPORT_SELECTION_SAME;
            export_best_seen = best_seen;
        } else if (deep_export_candidate_better(&best_deep_seen, &best_seen)) {
            export_selection = EXPORT_SELECTION_DEEP_SEEN;
            export_best_seen = best_deep_seen;
        }
    }

    double elapsed = elapsed_wall_seconds_since(start_seconds);
    RunReport report = {
        generation,
        quick_candidates_evaluated,
        deep_candidates_evaluated,
        duplicate_repairs,
        duplicate_random_replacements,
        stagnation_refreshes,
        adaptive_random_immigrants,
        elapsed,
        stop_reason_for(options, generation, elapsed),
        score_threads,
        last_unique_candidates,
        champion_starters_loaded,
        crossover_count,
        configured_novelty_lane,
        novelty_candidates_admitted,
        last_best_novelty_score,
        last_avg_novelty_score,
        options->starter_cap_enabled ? 1u : 0u,
        starter_cap,
        starter_cap_after,
        starter_cap_displacements,
        last_starter_survivors_before_cap,
        last_starter_survivors_after_cap,
        candidate_is_starter_lineage(&export_best_seen) ? 1u : 0u,
        refresh_window,
        refresh_immigrants,
        best_seen,
        best_deep_seen,
        has_best_deep_seen ? 1u : 0u,
        export_selection,
        improvements
    };

    if (best_out) *best_out = export_best_seen;
    if (report_out) *report_out = report;

    free(population);
    free(next);
    score_pool_destroy(&score_pool);
    return 0;
}

int command_run(const RunOptions *options) {
    Candidate best_seen;
    RunReport report;
    if (run_evolution(options, 1, &best_seen, &report) != 0) {
        return 1;
    }
    int exported = export_best(&best_seen, options, &report);
    print_final_report(&best_seen, options, &report, exported);
    return exported ? 0 : 1;
}
