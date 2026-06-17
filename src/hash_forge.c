#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#define HF_MKDIR(path) _mkdir(path)
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <sys/stat.h>
#define HF_MKDIR(path) mkdir(path, 0755)
#endif

#define REG_COUNT 5
#define MAX_INSTRUCTIONS 32
#define MIN_PROGRAM_LEN 8
#define MAX_PROGRAM_LEN 16
#define POPULATION_SIZE 256
#define SURVIVOR_COUNT 32
#define CROSSOVER_COUNT 16
#define IMMIGRANT_COUNT 8
#define DEEP_EVERY 25
#define DEEP_TOP_N 8
#define COLLISION_TABLE_SIZE 65536u
#define MAX_SCORE_THREADS 32
#define MAX_BENCH_THREAD_OPTIONS 16

#define FAIL_ZERO       0x01u
#define FAIL_COLLISION  0x02u
#define FAIL_BUCKET     0x04u
#define FAIL_AVALANCHE  0x08u
#define FAIL_NO_HASH    0x10u

typedef enum OpCode {
    OP_MOV,
    OP_ADD,
    OP_MUL,
    OP_XOR,
    OP_SHL,
    OP_SHR,
    OP_ROTL,
    OP_ROTR,
    OP_COUNT
} OpCode;

typedef enum OperandKind {
    OPERAND_REG,
    OPERAND_CONST
} OperandKind;

typedef enum QualityMode {
    QUALITY_QUICK,
    QUALITY_NORMAL,
    QUALITY_DEEP
} QualityMode;

typedef struct Rng {
    uint64_t state;
} Rng;

typedef struct Instruction {
    uint8_t op;
    uint8_t dst;
    uint8_t operand_kind;
    uint8_t operand_reg;
    uint8_t shift;
    uint64_t constant;
} Instruction;

typedef struct Candidate {
    uint64_t id;
    uint64_t parent_id;
    uint32_t generation;
    uint32_t instruction_count;
    Instruction instructions[MAX_INSTRUCTIONS];
    int64_t quick_score;
    int64_t deep_score;
    uint32_t fail_flags;
    uint32_t speed_hint;
} Candidate;

typedef struct ScoreScratch {
    uint64_t collision_keys[COLLISION_TABLE_SIZE];
    uint8_t collision_used[COLLISION_TABLE_SIZE];
    int bucket_counts[64];
    int bit_counts[64];
} ScoreScratch;

typedef struct ScoreResult {
    int64_t score;
    uint32_t fail_flags;
    uint32_t eval_count;
} ScoreResult;

typedef struct RunOptions {
    uint64_t seed;
    uint64_t generations;
    uint64_t seconds;
    uint32_t threads;
    QualityMode quality;
    int have_seed;
    int have_seconds;
    int have_threads;
    int auto_threads;
} RunOptions;

typedef struct RunReport {
    uint64_t run_generation;
    uint64_t quick_candidates_evaluated;
    uint64_t deep_candidates_evaluated;
    double elapsed_seconds;
    const char *stop_reason;
    uint32_t threads;
} RunReport;

typedef struct BenchOptions {
    uint64_t seed;
    uint64_t seconds;
    uint32_t threads[MAX_BENCH_THREAD_OPTIONS];
    uint32_t thread_count;
    int have_seed;
    int have_seconds;
} BenchOptions;

typedef struct BenchResult {
    uint32_t requested_threads;
    uint32_t actual_threads;
    uint64_t quick_candidates;
    uint64_t deep_candidates;
    double quick_seconds;
    double deep_seconds;
    double quick_rate;
    double deep_rate;
} BenchResult;

typedef struct ScoreTask {
    Candidate *candidates;
    uint32_t start;
    uint32_t end;
    uint64_t seed;
    int deep;
    QualityMode quality;
    ScoreScratch *scratch;
} ScoreTask;

typedef struct ScoreWorker {
    ScoreTask task;
#ifdef _WIN32
    HANDLE thread;
    HANDLE start_event;
    HANDLE done_event;
    volatile LONG should_stop;
#endif
} ScoreWorker;

typedef struct ScorePool {
    uint32_t thread_count;
    ScoreWorker *workers;
    ScoreScratch *scratches;
} ScorePool;

static const char *reg_names[REG_COUNT] = { "key", "seed", "hash", "a", "b" };
static volatile int g_stop_requested = 0;
static int g_color_enabled = 1;

static const char *c_reset(void) { return g_color_enabled ? "\x1b[0m" : ""; }
static const char *c_dim(void) { return g_color_enabled ? "\x1b[2m" : ""; }
static const char *c_bold(void) { return g_color_enabled ? "\x1b[1m" : ""; }
static const char *c_green(void) { return g_color_enabled ? "\x1b[32m" : ""; }
static const char *c_yellow(void) { return g_color_enabled ? "\x1b[33m" : ""; }
static const char *c_red(void) { return g_color_enabled ? "\x1b[31m" : ""; }
static const char *c_cyan(void) { return g_color_enabled ? "\x1b[36m" : ""; }

static void init_console_output(void) {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out == INVALID_HANDLE_VALUE || !GetConsoleMode(out, &mode)) {
        g_color_enabled = 0;
        return;
    }
    SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

static double elapsed_seconds_since(clock_t start_clock) {
    return (double)(clock() - start_clock) / (double)CLOCKS_PER_SEC;
}

static double wall_seconds_now(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    LARGE_INTEGER counter;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static uint64_t splitmix64_next(Rng *rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t rng_range(Rng *rng, uint32_t limit) {
    return (uint32_t)(splitmix64_next(rng) % limit);
}

static uint64_t mix_seed(uint64_t a, uint64_t b, uint64_t c) {
    Rng rng = { a ^ (b * 0x9e3779b97f4a7c15ull) ^ (c * 0xbf58476d1ce4e5b9ull) };
    return splitmix64_next(&rng);
}

static uint64_t rotl64(uint64_t x, unsigned n) {
    return (x << n) | (x >> (64u - n));
}

static uint64_t rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

static const char *op_name(uint8_t op) {
    switch (op) {
    case OP_MOV: return "MOV";
    case OP_ADD: return "ADD";
    case OP_MUL: return "MUL";
    case OP_XOR: return "XOR";
    case OP_SHL: return "SHL";
    case OP_SHR: return "SHR";
    case OP_ROTL: return "ROTL";
    case OP_ROTR: return "ROTR";
    default: return "?";
    }
}

static const char *quality_name(QualityMode quality) {
    switch (quality) {
    case QUALITY_QUICK: return "quick";
    case QUALITY_DEEP: return "deep";
    case QUALITY_NORMAL:
    default: return "normal";
    }
}

static int score_iterations_for_quality(QualityMode quality, int deep) {
    if (quality == QUALITY_QUICK) return deep ? 64 : 8;
    if (quality == QUALITY_DEEP) return deep ? 256 : 32;
    return deep ? 128 : 16;
}

static uint32_t deep_every_for_quality(QualityMode quality) {
    if (quality == QUALITY_QUICK) return 50;
    if (quality == QUALITY_DEEP) return 10;
    return DEEP_EVERY;
}

static uint32_t deep_top_n_for_quality(QualityMode quality) {
    if (quality == QUALITY_QUICK) return 4;
    if (quality == QUALITY_DEEP) return 16;
    return DEEP_TOP_N;
}

static uint64_t operand_value(const Instruction *ins, const uint64_t r[REG_COUNT]) {
    return ins->operand_kind == OPERAND_CONST ? ins->constant : r[ins->operand_reg];
}

static uint64_t eval_candidate(const Candidate *candidate, uint64_t key, uint64_t seed) {
    uint64_t r[REG_COUNT] = { key, seed, 0, 0, 0 };

    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        const Instruction *ins = &candidate->instructions[i];
        uint64_t value = operand_value(ins, r);
        uint64_t *dst = &r[ins->dst];
        switch (ins->op) {
        case OP_MOV: *dst = value; break;
        case OP_ADD: *dst += value; break;
        case OP_MUL: *dst *= value; break;
        case OP_XOR: *dst ^= value; break;
        case OP_SHL: *dst <<= ins->shift; break;
        case OP_SHR: *dst >>= ins->shift; break;
        case OP_ROTL: *dst = rotl64(*dst, ins->shift); break;
        case OP_ROTR: *dst = rotr64(*dst, ins->shift); break;
        default: break;
        }
    }

    return r[2];
}

static uint64_t candidate_id(const Candidate *candidate) {
    uint64_t h = 1469598103934665603ull;
    h ^= candidate->instruction_count;
    h *= 1099511628211ull;
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        const Instruction *ins = &candidate->instructions[i];
        h ^= ins->op + 0x100u * ins->dst + 0x10000u * ins->operand_kind;
        h *= 1099511628211ull;
        h ^= ins->operand_reg + 0x100u * ins->shift;
        h *= 1099511628211ull;
        h ^= ins->constant;
        h *= 1099511628211ull;
    }
    return h;
}

static int writes_hash(const Candidate *candidate) {
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (candidate->instructions[i].dst == 2) {
            return 1;
        }
    }
    return 0;
}

static uint32_t fail_flag_count(uint32_t flags) {
    uint32_t count = 0;
    while (flags) {
        count += flags & 1u;
        flags >>= 1;
    }
    return count;
}

static uint32_t fail_severity(uint32_t flags) {
    uint32_t severity = fail_flag_count(flags);
    if (flags & FAIL_NO_HASH) severity += 1000;
    if (flags & FAIL_ZERO) severity += 500;
    if (flags & FAIL_BUCKET) severity += 200;
    if (flags & FAIL_AVALANCHE) severity += 100;
    if (flags & FAIL_COLLISION) severity += 50;
    return severity;
}

static uint64_t random_constant(Rng *rng) {
    return splitmix64_next(rng) | 1ull;
}

static uint8_t random_opcode(Rng *rng) {
    static const uint8_t weighted_ops[] = {
        OP_MOV,
        OP_ADD, OP_ADD,
        OP_MUL, OP_MUL,
        OP_XOR, OP_XOR, OP_XOR,
        OP_SHL,
        OP_SHR,
        OP_ROTL, OP_ROTL,
        OP_ROTR, OP_ROTR
    };
    return weighted_ops[rng_range(rng, (uint32_t)(sizeof(weighted_ops) / sizeof(weighted_ops[0])))];
}

static void random_instruction(Instruction *ins, Rng *rng, int force_hash_bias) {
    ins->op = random_opcode(rng);
    ins->dst = (uint8_t)(force_hash_bias ? 2 : rng_range(rng, REG_COUNT));
    ins->operand_kind = (uint8_t)rng_range(rng, 2);
    ins->operand_reg = (uint8_t)rng_range(rng, REG_COUNT);
    ins->shift = (uint8_t)(1 + rng_range(rng, 63));
    ins->constant = random_constant(rng);

    if (ins->op == OP_SHL || ins->op == OP_SHR || ins->op == OP_ROTL || ins->op == OP_ROTR) {
        ins->operand_kind = OPERAND_CONST;
    }
    if (ins->op == OP_MUL) {
        ins->constant |= 1ull;
    }
}

static void random_candidate(Candidate *candidate, Rng *rng) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = MIN_PROGRAM_LEN + rng_range(rng, MAX_PROGRAM_LEN - MIN_PROGRAM_LEN + 1);
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        random_instruction(&candidate->instructions[i], rng, i == 0 || rng_range(rng, 3) == 0);
    }
    if (!writes_hash(candidate)) {
        candidate->instructions[0].dst = 2;
    }
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void mutate_candidate(Candidate *child, const Candidate *parent, Rng *rng) {
    *child = *parent;
    child->parent_id = parent->id;
    child->generation = parent->generation + 1;
    child->quick_score = 0;
    child->deep_score = INT64_MIN;
    child->fail_flags = 0;

    uint32_t mutations = 1 + rng_range(rng, 3);
    for (uint32_t m = 0; m < mutations; m++) {
        uint32_t action = rng_range(rng, 8);
        if (action == 0 && child->instruction_count < MAX_PROGRAM_LEN) {
            uint32_t at = rng_range(rng, child->instruction_count + 1);
            memmove(&child->instructions[at + 1], &child->instructions[at],
                    (child->instruction_count - at) * sizeof(child->instructions[0]));
            random_instruction(&child->instructions[at], rng, rng_range(rng, 3) == 0);
            child->instruction_count++;
        } else if (action == 1 && child->instruction_count > MIN_PROGRAM_LEN) {
            uint32_t at = rng_range(rng, child->instruction_count);
            memmove(&child->instructions[at], &child->instructions[at + 1],
                    (child->instruction_count - at - 1) * sizeof(child->instructions[0]));
            child->instruction_count--;
        } else if (action == 2 && child->instruction_count > 1) {
            uint32_t a = rng_range(rng, child->instruction_count);
            uint32_t b = rng_range(rng, child->instruction_count);
            Instruction tmp = child->instructions[a];
            child->instructions[a] = child->instructions[b];
            child->instructions[b] = tmp;
        } else {
            Instruction *ins = &child->instructions[rng_range(rng, child->instruction_count)];
            switch (rng_range(rng, 5)) {
            case 0: ins->op = random_opcode(rng); break;
            case 1: ins->dst = (uint8_t)rng_range(rng, REG_COUNT); break;
            case 2:
                ins->operand_kind = (uint8_t)rng_range(rng, 2);
                ins->operand_reg = (uint8_t)rng_range(rng, REG_COUNT);
                break;
            case 3: ins->constant = random_constant(rng); break;
            case 4: ins->shift = (uint8_t)(1 + rng_range(rng, 63)); break;
            }
            if (ins->op == OP_SHL || ins->op == OP_SHR || ins->op == OP_ROTL || ins->op == OP_ROTR) {
                ins->operand_kind = OPERAND_CONST;
            }
            if (ins->op == OP_MUL) {
                ins->constant |= 1ull;
            }
        }
    }

    if (!writes_hash(child)) {
        child->instructions[rng_range(rng, child->instruction_count)].dst = 2;
    }
    child->id = candidate_id(child);
}

static void crossover_candidate(Candidate *child, const Candidate *a, const Candidate *b, Rng *rng) {
    memset(child, 0, sizeof(*child));
    uint32_t prefix = 1 + rng_range(rng, a->instruction_count);
    if (prefix > MAX_PROGRAM_LEN) prefix = MAX_PROGRAM_LEN;

    for (uint32_t i = 0; i < prefix; i++) {
        child->instructions[i] = a->instructions[i];
    }
    child->instruction_count = prefix;

    uint32_t suffix_start = rng_range(rng, b->instruction_count);
    for (uint32_t i = suffix_start; i < b->instruction_count && child->instruction_count < MAX_PROGRAM_LEN; i++) {
        child->instructions[child->instruction_count++] = b->instructions[i];
    }
    while (child->instruction_count < MIN_PROGRAM_LEN) {
        random_instruction(&child->instructions[child->instruction_count++], rng, rng_range(rng, 3) == 0);
    }

    if (!writes_hash(child)) {
        child->instructions[rng_range(rng, child->instruction_count)].dst = 2;
    }
    child->parent_id = a->id ^ rotl64(b->id, 17);
    child->generation = (a->generation > b->generation ? a->generation : b->generation) + 1;
    child->quick_score = 0;
    child->deep_score = INT64_MIN;
    child->fail_flags = 0;
    child->id = candidate_id(child);
}

static int collision_add(ScoreScratch *scratch, uint64_t h) {
    uint32_t mask = COLLISION_TABLE_SIZE - 1u;
    uint32_t at = (uint32_t)(h ^ (h >> 32)) & mask;
    for (;;) {
        if (!scratch->collision_used[at]) {
            scratch->collision_used[at] = 1;
            scratch->collision_keys[at] = h;
            return 0;
        }
        if (scratch->collision_keys[at] == h) {
            return 1;
        }
        at = (at + 1u) & mask;
    }
}

static int64_t score_zero(const Candidate *candidate, uint32_t *flags) {
    uint64_t out[4];
    out[0] = eval_candidate(candidate, 0, 0);
    out[1] = eval_candidate(candidate, 0, 1);
    out[2] = eval_candidate(candidate, 1, 0);
    out[3] = eval_candidate(candidate, 1, 1);

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

static int64_t score_collisions(const Candidate *candidate, ScoreScratch *scratch, uint64_t seed, int iterations, uint32_t *flags, uint32_t *evals) {
    memset(scratch->collision_used, 0, sizeof(scratch->collision_used));
    Rng rng = { seed };
    int collisions = 0;
    int outputs = 0;
    uint64_t constant = splitmix64_next(&rng);
    uint64_t seq1 = splitmix64_next(&rng);
    uint64_t seq2 = splitmix64_next(&rng);
    uint64_t rec1 = splitmix64_next(&rng);
    uint64_t rec2 = splitmix64_next(&rng);
    uint64_t rec3 = splitmix64_next(&rng);

#define ADD_HASH(expr) do { collisions += collision_add(scratch, (expr)); outputs++; } while (0)
    for (int i = 0; i < iterations; i++) {
        uint64_t r1 = splitmix64_next(&rng);
        uint64_t r2 = splitmix64_next(&rng);
        uint64_t s = (uint64_t)i;
        ADD_HASH(eval_candidate(candidate, 0, r1));
        ADD_HASH(eval_candidate(candidate, r1, 0));
        ADD_HASH(eval_candidate(candidate, constant, r1));
        ADD_HASH(eval_candidate(candidate, r1, constant));
        ADD_HASH(eval_candidate(candidate, r1, r2));
        ADD_HASH(eval_candidate(candidate, r2, r1));
        ADD_HASH(eval_candidate(candidate, s, 0));
        ADD_HASH(eval_candidate(candidate, 0, s));
        ADD_HASH(eval_candidate(candidate, seq1, seq2));
        ADD_HASH(eval_candidate(candidate, seq2, seq1));
        rec1 = eval_candidate(candidate, rec1, constant);
        rec2 = eval_candidate(candidate, constant, rec2);
        rec3 = eval_candidate(candidate, rec3, rec3);
        ADD_HASH(rec1);
        ADD_HASH(rec2);
        ADD_HASH(rec3);
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

static int64_t score_buckets(const Candidate *candidate, ScoreScratch *scratch, uint64_t seed, int iterations, int deep, uint32_t *flags, uint32_t *evals) {
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
                uint64_t h = eval_candidate(candidate, key, s);
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

static int64_t score_avalanche(const Candidate *candidate, ScoreScratch *scratch, uint64_t seed, int iterations, int deep, uint32_t *flags, uint32_t *evals) {
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
                uint64_t diff = eval_candidate(candidate, key, s) ^ eval_candidate(candidate, key2, s2);
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

static ScoreResult score_candidate(const Candidate *candidate, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality) {
    const int iterations = score_iterations_for_quality(quality, deep);
    ScoreResult result = { 0, 0, 0 };
    if (!writes_hash(candidate)) {
        result.fail_flags |= FAIL_NO_HASH;
        result.score -= 1000000;
    }

    result.score += score_zero(candidate, &result.fail_flags);
    result.eval_count += 4;
    result.score += score_collisions(candidate, scratch, mix_seed(run_seed, candidate->id, 11), iterations, &result.fail_flags, &result.eval_count);
    result.score += score_buckets(candidate, scratch, mix_seed(run_seed, candidate->id, 22), deep ? iterations * 2 : iterations, deep, &result.fail_flags, &result.eval_count);
    result.score += score_avalanche(candidate, scratch, mix_seed(run_seed, candidate->id, 33), iterations, deep, &result.fail_flags, &result.eval_count);
    result.score -= (int64_t)candidate->instruction_count * 20;
    return result;
}

static int compare_candidates(const void *a_ptr, const void *b_ptr) {
    const Candidate *a = (const Candidate *)a_ptr;
    const Candidate *b = (const Candidate *)b_ptr;
    uint32_t a_severity = fail_severity(a->fail_flags);
    uint32_t b_severity = fail_severity(b->fail_flags);
    if (a_severity != b_severity) return a_severity < b_severity ? -1 : 1;
    if (a->fail_flags != b->fail_flags) return a->fail_flags < b->fail_flags ? -1 : 1;
    if (a->deep_score != INT64_MIN || b->deep_score != INT64_MIN) {
        if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score ? -1 : 1;
    }
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score ? -1 : 1;
    if (a->instruction_count != b->instruction_count) return a->instruction_count < b->instruction_count ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

static void print_instruction(FILE *out, const Instruction *ins, int c_syntax) {
    const char *dst = reg_names[ins->dst];
    if (!c_syntax) {
        if (ins->op == OP_SHL || ins->op == OP_SHR || ins->op == OP_ROTL || ins->op == OP_ROTR) {
            fprintf(out, "%s %s %u", op_name(ins->op), dst, (unsigned)ins->shift);
        } else if (ins->operand_kind == OPERAND_REG) {
            fprintf(out, "%s %s %s", op_name(ins->op), dst, reg_names[ins->operand_reg]);
        } else {
            fprintf(out, "%s %s 0x%llx", op_name(ins->op), dst, (unsigned long long)ins->constant);
        }
        return;
    }

    if (ins->op == OP_MOV) {
        if (ins->operand_kind == OPERAND_REG) fprintf(out, "    %s = %s;\n", dst, reg_names[ins->operand_reg]);
        else fprintf(out, "    %s = UINT64_C(0x%llx);\n", dst, (unsigned long long)ins->constant);
    } else if (ins->op == OP_ADD || ins->op == OP_MUL || ins->op == OP_XOR) {
        const char *op = ins->op == OP_ADD ? "+=" : (ins->op == OP_MUL ? "*=" : "^=");
        if (ins->operand_kind == OPERAND_REG) fprintf(out, "    %s %s %s;\n", dst, op, reg_names[ins->operand_reg]);
        else fprintf(out, "    %s %s UINT64_C(0x%llx);\n", dst, op, (unsigned long long)ins->constant);
    } else if (ins->op == OP_SHL) {
        fprintf(out, "    %s <<= %uu;\n", dst, (unsigned)ins->shift);
    } else if (ins->op == OP_SHR) {
        fprintf(out, "    %s >>= %uu;\n", dst, (unsigned)ins->shift);
    } else if (ins->op == OP_ROTL) {
        fprintf(out, "    %s = hf_rotl64(%s, %uu);\n", dst, dst, (unsigned)ins->shift);
    } else if (ins->op == OP_ROTR) {
        fprintf(out, "    %s = hf_rotr64(%s, %uu);\n", dst, dst, (unsigned)ins->shift);
    }
}

static int ensure_out_dir(void) {
    if (HF_MKDIR("out") != 0) {
        /* Existing directory is fine; file creation below will catch real errors. */
    }
    return 1;
}

static void print_fail_flags(FILE *out, uint32_t flags) {
    int wrote = 0;
    if (flags == 0) {
        fprintf(out, "none");
        return;
    }
    if (flags & FAIL_ZERO) { fprintf(out, "%sZERO", wrote ? ", " : ""); wrote = 1; }
    if (flags & FAIL_COLLISION) { fprintf(out, "%sCOLLISION", wrote ? ", " : ""); wrote = 1; }
    if (flags & FAIL_BUCKET) { fprintf(out, "%sBUCKET", wrote ? ", " : ""); wrote = 1; }
    if (flags & FAIL_AVALANCHE) { fprintf(out, "%sAVALANCHE", wrote ? ", " : ""); wrote = 1; }
    if (flags & FAIL_NO_HASH) { fprintf(out, "%sNO_HASH", wrote ? ", " : ""); wrote = 1; }
}

static void make_reasonable_baseline(Candidate *candidate);
static void make_constant_bad(Candidate *candidate);
static void make_key_only_bad(Candidate *candidate);
static void make_seed_only_bad(Candidate *candidate);
static void make_xor_only_bad(Candidate *candidate);

static void count_ops(const Candidate *candidate, uint32_t counts[OP_COUNT]) {
    memset(counts, 0, OP_COUNT * sizeof(counts[0]));
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (candidate->instructions[i].op < OP_COUNT) {
            counts[candidate->instructions[i].op]++;
        }
    }
}

static int write_markdown_report(const Candidate *candidate, const RunOptions *options, const RunReport *report) {
    FILE *md = fopen("out/report.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/report.md\n");
        return 0;
    }

    fprintf(md, "# hash-forge run report\n\n");
    fprintf(md, "## Run settings\n\n");
    fprintf(md, "- Seed: `%llu`\n", (unsigned long long)options->seed);
    fprintf(md, "- Requested generations: `%s", options->generations ? "" : "unlimited");
    if (options->generations) fprintf(md, "%llu", (unsigned long long)options->generations);
    fprintf(md, "`\n");
    fprintf(md, "- Requested seconds: `%s", options->have_seconds ? "" : "unlimited");
    if (options->have_seconds) fprintf(md, "%llu", (unsigned long long)options->seconds);
    fprintf(md, "`\n");
    fprintf(md, "- Actual generations completed: `%llu`\n", (unsigned long long)report->run_generation);
    fprintf(md, "- Elapsed seconds: `%.3f`\n", report->elapsed_seconds);
    fprintf(md, "- Stop reason: `%s`\n", report->stop_reason);
    fprintf(md, "- Quality: `%s`\n", quality_name(options->quality));
    fprintf(md, "- Scoring threads: `%u`\n\n", report->threads);

    fprintf(md, "## Evaluation totals\n\n");
    fprintf(md, "- Quick candidates evaluated: `%llu`\n", (unsigned long long)report->quick_candidates_evaluated);
    fprintf(md, "- Deep candidates evaluated: `%llu`\n", (unsigned long long)report->deep_candidates_evaluated);
    fprintf(md, "- Total hash functions evaluated: `%llu`\n\n",
            (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));

    fprintf(md, "## Population settings\n\n");
    fprintf(md, "- Population size: `%u`\n", POPULATION_SIZE);
    fprintf(md, "- Survivor count: `%u`\n", SURVIVOR_COUNT);
    fprintf(md, "- Crossover children per generation: `%u`\n", CROSSOVER_COUNT);
    fprintf(md, "- Random immigrants per generation: `%u`\n", IMMIGRANT_COUNT);
    fprintf(md, "- Scoring threads: `%u`\n", report->threads);
    fprintf(md, "- Deep score cadence: every `%u` generations\n", deep_every_for_quality(options->quality));
    fprintf(md, "- Deep score top N: `%u`\n", deep_top_n_for_quality(options->quality));
    fprintf(md, "- Instruction count range: `%u..%u`\n\n", MIN_PROGRAM_LEN, MAX_PROGRAM_LEN);

    fprintf(md, "## Best candidate\n\n");
    fprintf(md, "- ID: `%llx`\n", (unsigned long long)candidate->id);
    fprintf(md, "- Parent ID: `%llx`\n", (unsigned long long)candidate->parent_id);
    fprintf(md, "- Candidate generation: `%u`\n", candidate->generation);
    fprintf(md, "- Instruction count: `%u`\n", candidate->instruction_count);
    fprintf(md, "- Quick score: `%lld`\n", (long long)candidate->quick_score);
    fprintf(md, "- Final deep score: `%lld`\n", (long long)candidate->deep_score);
    fprintf(md, "- Fail flags: `0x%x` (", candidate->fail_flags);
    print_fail_flags(md, candidate->fail_flags);
    fprintf(md, ")\n\n");

    uint32_t op_counts[OP_COUNT];
    count_ops(candidate, op_counts);
    fprintf(md, "## Operator histogram\n\n");
    fprintf(md, "| op | count |\n");
    fprintf(md, "|---|---:|\n");
    for (uint32_t i = 0; i < OP_COUNT; i++) {
        fprintf(md, "| %s | %u |\n", op_name((uint8_t)i), op_counts[i]);
    }
    fprintf(md, "\n");

    fprintf(md, "## Baseline comparison\n\n");
    ScoreScratch *comparison_scratch = (ScoreScratch *)calloc(1, sizeof(*comparison_scratch));
    if (comparison_scratch) {
        typedef void (*ComparisonMaker)(Candidate *);
        typedef struct ComparisonCase {
            const char *name;
            ComparisonMaker maker;
        } ComparisonCase;
        ComparisonCase cases[] = {
            { "best_candidate", NULL },
            { "baseline_mixer", make_reasonable_baseline },
            { "bad_constant", make_constant_bad },
            { "bad_key_only", make_key_only_bad },
            { "bad_seed_only", make_seed_only_bad },
            { "bad_xor_only", make_xor_only_bad }
        };
        fprintf(md, "| candidate | deep score | flags |\n");
        fprintf(md, "|---|---:|---|\n");
        for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            Candidate compare_candidate;
            if (cases[i].maker) {
                cases[i].maker(&compare_candidate);
            } else {
                compare_candidate = *candidate;
            }
            ScoreResult score = score_candidate(&compare_candidate, comparison_scratch, options->seed, 1, options->quality);
            fprintf(md, "| %s | %lld | `0x%x` (", cases[i].name, (long long)score.score, score.fail_flags);
            print_fail_flags(md, score.fail_flags);
            fprintf(md, ") |\n");
        }
        free(comparison_scratch);
    } else {
        fprintf(md, "Skipped: failed to allocate comparison scratch space.\n");
    }
    fprintf(md, "\n");

    fprintf(md, "## VM instruction listing\n\n");
    fprintf(md, "```txt\n");
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        fprintf(md, "%02u: ", i);
        print_instruction(md, &candidate->instructions[i], 0);
        fprintf(md, "\n");
    }
    fprintf(md, "```\n\n");

    fprintf(md, "## Exported C\n\n");
    fprintf(md, "The standalone exported C function is written to `out/best.c`.\n\n");
    fprintf(md, "## Output files\n\n");
    fprintf(md, "- `out/best.c`: standalone C hash function\n");
    fprintf(md, "- `out/best.txt`: compact machine-readable-ish best candidate details\n");
    fprintf(md, "- `out/report.md`: full human-readable run report\n");
    fprintf(md, "- `out/summary.txt`: terse run summary\n\n");

    fprintf(md, "## Interpretation note\n\n");
    fprintf(md, "Scores and flags are exploratory quality signals for non-cryptographic hash search. ");
    fprintf(md, "They are not cryptographic proof and should be treated as candidates for further testing.\n");
    fclose(md);
    return 1;
}

static int export_best(const Candidate *candidate, const RunOptions *options, const RunReport *report) {
    ensure_out_dir();
    FILE *c = fopen("out/best.c", "wb");
    if (!c) {
        fprintf(stderr, "failed to open out/best.c\n");
        return 0;
    }

    fprintf(c, "#include <stdint.h>\n\n");
    fprintf(c, "static inline uint64_t hf_rotl64(uint64_t x, unsigned n) {\n");
    fprintf(c, "    return (x << n) | (x >> (64u - n));\n");
    fprintf(c, "}\n\n");
    fprintf(c, "static inline uint64_t hf_rotr64(uint64_t x, unsigned n) {\n");
    fprintf(c, "    return (x >> n) | (x << (64u - n));\n");
    fprintf(c, "}\n\n");
    fprintf(c, "uint64_t hash_forge_best(uint64_t key, uint64_t seed) {\n");
    fprintf(c, "    uint64_t hash = 0;\n");
    fprintf(c, "    uint64_t a = 0;\n");
    fprintf(c, "    uint64_t b = 0;\n\n");
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        print_instruction(c, &candidate->instructions[i], 1);
    }
    fprintf(c, "\n    return hash;\n}\n");
    fclose(c);

    FILE *txt = fopen("out/best.txt", "wb");
    if (!txt) {
        fprintf(stderr, "failed to open out/best.txt\n");
        return 0;
    }
    fprintf(txt, "id: %llu\n", (unsigned long long)candidate->id);
    fprintf(txt, "parent_id: %llu\n", (unsigned long long)candidate->parent_id);
    fprintf(txt, "generation: %u\n", candidate->generation);
    fprintf(txt, "run_generation: %llu\n", (unsigned long long)report->run_generation);
    fprintf(txt, "seed: %llu\n", (unsigned long long)options->seed);
    fprintf(txt, "quality: %s\n", quality_name(options->quality));
    fprintf(txt, "quick_score: %lld\n", (long long)candidate->quick_score);
    fprintf(txt, "deep_score: %lld\n", (long long)candidate->deep_score);
    fprintf(txt, "fail_flags: 0x%x\n", candidate->fail_flags);
    fprintf(txt, "quick_candidates_evaluated: %llu\n", (unsigned long long)report->quick_candidates_evaluated);
    fprintf(txt, "deep_candidates_evaluated: %llu\n", (unsigned long long)report->deep_candidates_evaluated);
    fprintf(txt, "total_candidates_evaluated: %llu\n",
            (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));
    fprintf(txt, "threads: %u\n", report->threads);
    fprintf(txt, "instruction_count: %u\n\n", candidate->instruction_count);
    uint32_t op_counts[OP_COUNT];
    count_ops(candidate, op_counts);
    fprintf(txt, "operator_histogram:\n");
    for (uint32_t i = 0; i < OP_COUNT; i++) {
        fprintf(txt, "  %s: %u\n", op_name((uint8_t)i), op_counts[i]);
    }
    fprintf(txt, "\n");
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        fprintf(txt, "%02u: ", i);
        print_instruction(txt, &candidate->instructions[i], 0);
        fprintf(txt, "\n");
    }
    fclose(txt);

    FILE *summary = fopen("out/summary.txt", "wb");
    if (summary) {
        fprintf(summary, "hash-forge best candidate\n");
        fprintf(summary, "id=%llu generation=%u run_generation=%llu quick=%lld deep=%lld flags=0x%x elapsed_seconds=%.3f stop_reason=%s quality=%s threads=%u quick_candidates=%llu deep_candidates=%llu total_candidates=%llu\n",
                (unsigned long long)candidate->id, candidate->generation,
                (unsigned long long)report->run_generation,
                (long long)candidate->quick_score, (long long)candidate->deep_score,
                candidate->fail_flags, report->elapsed_seconds, report->stop_reason, quality_name(options->quality), report->threads,
                (unsigned long long)report->quick_candidates_evaluated,
                (unsigned long long)report->deep_candidates_evaluated,
                (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));
        fclose(summary);
    }
    return write_markdown_report(candidate, options, report);
}

static void make_reasonable_baseline(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 19;
    Instruction p[] = {
        { OP_MOV, 2, OPERAND_REG, 0, 1, 0 },
        { OP_ADD, 2, OPERAND_CONST, 0, 1, 0x9e3779b97f4a7c15ull },
        { OP_MUL, 2, OPERAND_CONST, 0, 1, 0xbf58476d1ce4e5b9ull },
        { OP_MOV, 3, OPERAND_REG, 1, 1, 0 },
        { OP_ADD, 3, OPERAND_CONST, 0, 1, 0xd1b54a32d192ed03ull },
        { OP_MUL, 3, OPERAND_CONST, 0, 1, 0x94d049bb133111ebull },
        { OP_XOR, 2, OPERAND_REG, 3, 1, 0 },
        { OP_MOV, 3, OPERAND_REG, 2, 1, 0 },
        { OP_SHR, 3, OPERAND_CONST, 0, 30, 0 },
        { OP_XOR, 2, OPERAND_REG, 3, 1, 0 },
        { OP_MUL, 2, OPERAND_CONST, 0, 1, 0xbf58476d1ce4e5b9ull },
        { OP_MOV, 3, OPERAND_REG, 2, 1, 0 },
        { OP_SHR, 3, OPERAND_CONST, 0, 27, 0 },
        { OP_XOR, 2, OPERAND_REG, 3, 1, 0 },
        { OP_MUL, 2, OPERAND_CONST, 0, 1, 0x94d049bb133111ebull },
        { OP_MOV, 3, OPERAND_REG, 2, 1, 0 },
        { OP_SHR, 3, OPERAND_CONST, 0, 31, 0 },
        { OP_XOR, 2, OPERAND_REG, 3, 1, 0 },
        { OP_XOR, 2, OPERAND_CONST, 0, 1, 0xd1b54a32d192ed03ull }
    };
    memcpy(candidate->instructions, p, sizeof(p));
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void make_constant_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_CONST, 0, 1, 1 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void make_key_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 0, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void make_seed_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 1, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void make_xor_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 2;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 0, 1, 0 };
    candidate->instructions[1] = (Instruction){ OP_XOR, 2, OPERAND_REG, 1, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static void make_program(Candidate *candidate, const Instruction *instructions, uint32_t instruction_count) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = instruction_count;
    memcpy(candidate->instructions, instructions, instruction_count * sizeof(instructions[0]));
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

static int candidate_is_valid(const Candidate *candidate) {
    if (candidate->instruction_count < MIN_PROGRAM_LEN || candidate->instruction_count > MAX_PROGRAM_LEN) return 0;
    if (!writes_hash(candidate)) return 0;
    if (candidate->id != candidate_id(candidate)) return 0;
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        const Instruction *ins = &candidate->instructions[i];
        if (ins->op >= OP_COUNT) return 0;
        if (ins->dst >= REG_COUNT) return 0;
        if (ins->operand_kind > OPERAND_CONST) return 0;
        if (ins->operand_reg >= REG_COUNT) return 0;
        if (ins->shift == 0 || ins->shift >= 64) return 0;
        if ((ins->op == OP_SHL || ins->op == OP_SHR || ins->op == OP_ROTL || ins->op == OP_ROTR) &&
            ins->operand_kind != OPERAND_CONST) return 0;
        if (ins->op == OP_MUL && ins->operand_kind == OPERAND_CONST && (ins->constant & 1ull) == 0) return 0;
    }
    return 1;
}

static int self_check(int condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "self-test failed: %s\n", name);
        return 0;
    }
    return 1;
}

static int run_vm_self_tests(void) {
    Candidate c;
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

    if (!self_check((baseline_score.fail_flags & (FAIL_ZERO | FAIL_BUCKET | FAIL_NO_HASH)) == 0,
                    "baseline mixer core flags")) {
        free(scratch);
        return 0;
    }

    CalibrationCase cases[] = {
        { "bad_constant", make_constant_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE },
        { "bad_key_only", make_key_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE },
        { "bad_seed_only", make_seed_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE },
        { "bad_xor_only", make_xor_only_bad, FAIL_ZERO | FAIL_COLLISION | FAIL_AVALANCHE }
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

    Rng op_rng = { 0x55aa55aa55aa55aaull };
    for (uint32_t i = 0; i < 512; i++) {
        Instruction ins;
        random_instruction(&ins, &op_rng, 0);
        if (ins.op < OP_COUNT) op_seen[ins.op]++;
    }
    for (uint32_t i = 0; i < OP_COUNT; i++) {
        if (!self_check(op_seen[i] > 0, "weighted opcode sampler reaches every op")) return 0;
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
    if (!self_check(compare_candidates(&a, &b) > 0, "selection prefers available deep score")) return 0;

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

static uint32_t default_thread_count(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (info.dwNumberOfProcessors == 0) return 1;
    return info.dwNumberOfProcessors > MAX_SCORE_THREADS ? MAX_SCORE_THREADS : info.dwNumberOfProcessors;
#else
    return 1;
#endif
}

static uint32_t clamp_thread_count(uint64_t requested, uint32_t candidate_count) {
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

static void score_pool_destroy(ScorePool *pool) {
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

static int score_pool_init(ScorePool *pool, uint32_t requested_threads, uint32_t candidate_count) {
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

static int score_pool_score(ScorePool *pool, Candidate *candidates, uint32_t candidate_count, uint64_t seed, int deep, QualityMode quality) {
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

static int seconds_limit_reached(const RunOptions *options, clock_t start_clock) {
    return options->have_seconds && elapsed_seconds_since(start_clock) >= (double)options->seconds;
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

static void print_run_header(const RunOptions *options, uint32_t score_threads) {
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
    printf("  %spopulation%s   %u candidates, %u survivors, %u crossover, %u immigrants\n",
           c_dim(), c_reset(), POPULATION_SIZE, SURVIVOR_COUNT, CROSSOVER_COUNT, IMMIGRANT_COUNT);
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
    printf("  %sbest id%s            %s%016llx%s\n", c_dim(), c_reset(), c_cyan(), (unsigned long long)candidate->id, c_reset());
    printf("  %sbest quick%s         %lld\n", c_dim(), c_reset(), (long long)candidate->quick_score);
    printf("  %sbest deep%s          %lld\n", c_dim(), c_reset(), (long long)candidate->deep_score);
    printf("  %sbest flags%s         %s0x%x%s (", c_dim(), c_reset(), candidate->fail_flags ? c_yellow() : c_green(), candidate->fail_flags, c_reset());
    print_fail_flags(stdout, candidate->fail_flags);
    printf(")\n");
    printf("  %soutput%s             %s\n", c_dim(), c_reset(), exported ? "out/best.c, out/best.txt, out/report.md, out/summary.txt" : "export failed");
}

static int command_self_test(void) {
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

static int command_run(const RunOptions *options) {
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

    uint64_t generation = 0;
    Candidate best_seen;
    memset(&best_seen, 0, sizeof(best_seen));
    best_seen.quick_score = INT64_MIN;
    best_seen.deep_score = INT64_MIN;
    uint64_t quick_candidates_evaluated = 0;
    uint64_t deep_candidates_evaluated = 0;
    uint32_t score_threads = options->auto_threads
        ? choose_auto_thread_count(population, options->seed)
        : clamp_thread_count(options->threads, POPULATION_SIZE);
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
    clock_t start_clock = clock();

    print_run_header(options, score_threads);

    while (!g_stop_requested &&
           !generation_limit_reached(options, generation) &&
           !seconds_limit_reached(options, start_clock)) {
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
            qsort(population, POPULATION_SIZE, sizeof(population[0]), compare_candidates);
        }

        if (population[0].fail_flags < best_seen.fail_flags ||
            best_seen.quick_score == INT64_MIN ||
            (population[0].fail_flags == best_seen.fail_flags && population[0].quick_score > best_seen.quick_score)) {
            best_seen = population[0];
        }

        double now_elapsed = elapsed_seconds_since(start_clock);
        if (generation == 1 ||
            deep_generation ||
            generation_limit_reached(options, generation) ||
            seconds_limit_reached(options, start_clock) ||
            last_status_elapsed < 0.0 ||
            now_elapsed - last_status_elapsed >= 0.25) {
            print_progress_line(options, generation, now_elapsed, &population[0],
                                quick_candidates_evaluated, deep_candidates_evaluated);
            last_status_elapsed = now_elapsed;
        }

        for (uint32_t i = 0; i < SURVIVOR_COUNT; i++) {
            next[i] = population[i];
        }
        uint32_t immigrant_start = POPULATION_SIZE > IMMIGRANT_COUNT ? POPULATION_SIZE - IMMIGRANT_COUNT : SURVIVOR_COUNT;
        if (immigrant_start < SURVIVOR_COUNT) immigrant_start = SURVIVOR_COUNT;
        uint32_t crossover_start = immigrant_start > CROSSOVER_COUNT ? immigrant_start - CROSSOVER_COUNT : SURVIVOR_COUNT;
        if (crossover_start < SURVIVOR_COUNT) crossover_start = SURVIVOR_COUNT;
        for (uint32_t i = SURVIVOR_COUNT; i < crossover_start; i++) {
            uint32_t r = rng_range(&rng, SURVIVOR_COUNT * SURVIVOR_COUNT);
            uint32_t parent_index = r / SURVIVOR_COUNT;
            if (parent_index >= SURVIVOR_COUNT) parent_index = SURVIVOR_COUNT - 1;
            mutate_candidate(&next[i], &population[parent_index], &rng);
        }
        for (uint32_t i = crossover_start; i < immigrant_start; i++) {
            uint32_t a = rng_range(&rng, SURVIVOR_COUNT);
            uint32_t b = rng_range(&rng, SURVIVOR_COUNT);
            crossover_candidate(&next[i], &population[a], &population[b], &rng);
        }
        for (uint32_t i = immigrant_start; i < POPULATION_SIZE; i++) {
            random_candidate(&next[i], &rng);
            next[i].generation = (uint32_t)generation;
            next[i].id = candidate_id(&next[i]);
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

    double elapsed = elapsed_seconds_since(start_clock);
    RunReport report = {
        generation,
        quick_candidates_evaluated,
        deep_candidates_evaluated,
        elapsed,
        stop_reason_for(options, generation, elapsed),
        score_threads
    };

    int exported = export_best(&best_seen, options, &report);
    print_final_report(&best_seen, options, &report, exported);

    free(population);
    free(next);
    score_pool_destroy(&score_pool);
    return exported ? 0 : 1;
}

static int write_bench_report(const BenchOptions *options, const BenchResult *results, uint32_t result_count) {
    ensure_out_dir();
    FILE *md = fopen("out/bench.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/bench.md\n");
        return 0;
    }

    fprintf(md, "# hash-forge benchmark report\n\n");
    fprintf(md, "## Settings\n\n");
    fprintf(md, "- Seed: `%llu`\n", (unsigned long long)options->seed);
    fprintf(md, "- Seconds per thread option: `%llu`\n", (unsigned long long)options->seconds);
    fprintf(md, "- Population size: `%u`\n", POPULATION_SIZE);
    fprintf(md, "- Deep sample size: `%u`\n\n", DEEP_TOP_N);

    fprintf(md, "## Results\n\n");
    fprintf(md, "| requested threads | actual threads | quick candidates | quick/sec | deep candidates | deep/sec |\n");
    fprintf(md, "|---:|---:|---:|---:|---:|---:|\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(md, "| %u | %u | %llu | %.0f | %llu | %.0f |\n",
                results[i].requested_threads,
                results[i].actual_threads,
                (unsigned long long)results[i].quick_candidates,
                results[i].quick_rate,
                (unsigned long long)results[i].deep_candidates,
                results[i].deep_rate);
    }

    if (result_count > 0) {
        uint32_t best_quick = 0;
        uint32_t best_deep = 0;
        for (uint32_t i = 1; i < result_count; i++) {
            if (results[i].quick_rate > results[best_quick].quick_rate) best_quick = i;
            if (results[i].deep_rate > results[best_deep].deep_rate) best_deep = i;
        }
        fprintf(md, "\n## Interpretation\n\n");
        fprintf(md, "- Best quick throughput: `%u` threads at `%.0f` candidates/sec.\n",
                results[best_quick].actual_threads, results[best_quick].quick_rate);
        fprintf(md, "- Best deep throughput: `%u` threads at `%.0f` candidates/sec.\n",
                results[best_deep].actual_threads, results[best_deep].deep_rate);
        fprintf(md, "- Benchmark rates are machine-local guidance, not a quality score.\n");
    }

    fclose(md);
    return 1;
}

static int run_bench_phase(ScorePool *pool, Candidate *candidates, uint32_t candidate_count, uint64_t seed, int deep, double seconds, uint64_t *evaluated, double *elapsed) {
    double start = wall_seconds_now();
    double now = start;
    *evaluated = 0;

    do {
        if (!score_pool_score(pool, candidates, candidate_count, seed, deep, QUALITY_NORMAL)) {
            return 0;
        }
        *evaluated += candidate_count;
        now = wall_seconds_now();
    } while (now - start < seconds);

    *elapsed = now - start;
    if (*elapsed <= 0.0) *elapsed = 0.000001;
    return 1;
}

static int command_bench(const BenchOptions *options) {
    Candidate *population = (Candidate *)calloc(POPULATION_SIZE, sizeof(*population));
    BenchResult results[MAX_BENCH_THREAD_OPTIONS];
    if (!population) {
        fprintf(stderr, "failed to allocate benchmark population\n");
        return 1;
    }

    Rng rng = { options->seed };
    for (uint32_t i = 0; i < POPULATION_SIZE; i++) {
        random_candidate(&population[i], &rng);
    }

    double phase_seconds = (double)options->seconds / 2.0;
    if (phase_seconds < 0.001) phase_seconds = 0.001;

    printf("\n%s%sHash Forge benchmark%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sseed%s         %llu\n", c_dim(), c_reset(), (unsigned long long)options->seed);
    printf("  %sseconds%s      %llu total per thread option\n", c_dim(), c_reset(), (unsigned long long)options->seconds);
    printf("  %spopulation%s   %u candidates\n", c_dim(), c_reset(), POPULATION_SIZE);
    printf("\n%s%9s  %7s  %14s  %12s  %14s  %12s%s\n",
           c_dim(), "requested", "actual", "quick evals", "quick/sec", "deep evals", "deep/sec", c_reset());

    uint32_t result_count = 0;
    for (uint32_t i = 0; i < options->thread_count; i++) {
        uint32_t requested = options->threads[i];
        ScorePool pool;
        if (!score_pool_init(&pool, requested, POPULATION_SIZE)) {
            fprintf(stderr, "failed to start scoring worker pool for %u thread(s)\n", requested);
            free(population);
            return 1;
        }

        BenchResult *result = &results[result_count++];
        memset(result, 0, sizeof(*result));
        result->requested_threads = requested;
        result->actual_threads = pool.thread_count;

        if (!run_bench_phase(&pool, population, POPULATION_SIZE, mix_seed(options->seed, requested, 1), 0,
                             phase_seconds, &result->quick_candidates, &result->quick_seconds)) {
            score_pool_destroy(&pool);
            free(population);
            return 1;
        }
        if (!run_bench_phase(&pool, population, DEEP_TOP_N, mix_seed(options->seed, requested, 2), 1,
                             phase_seconds, &result->deep_candidates, &result->deep_seconds)) {
            score_pool_destroy(&pool);
            free(population);
            return 1;
        }
        score_pool_destroy(&pool);

        result->quick_rate = (double)result->quick_candidates / result->quick_seconds;
        result->deep_rate = (double)result->deep_candidates / result->deep_seconds;
        printf("%s%9u%s  %7u  %14llu  %s%12.0f%s  %14llu  %s%12.0f%s\n",
               c_bold(), result->requested_threads, c_reset(),
               result->actual_threads,
               (unsigned long long)result->quick_candidates,
               c_green(), result->quick_rate, c_reset(),
               (unsigned long long)result->deep_candidates,
               c_green(), result->deep_rate, c_reset());
    }

    int wrote = write_bench_report(options, results, result_count);
    printf("\n%s%sBenchmark complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? "out/bench.md" : "export failed");

    free(population);
    return wrote ? 0 : 1;
}

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out = (uint64_t)value;
    return 1;
}

static int parse_thread_list(const char *text, BenchOptions *options) {
    const char *at = text;
    options->thread_count = 0;

    while (*at) {
        char *end = NULL;
        unsigned long value = strtoul(at, &end, 0);
        if (end == at || value == 0) {
            return 0;
        }
        if (options->thread_count >= MAX_BENCH_THREAD_OPTIONS) {
            fprintf(stderr, "too many --threads entries; max is %u\n", MAX_BENCH_THREAD_OPTIONS);
            return 0;
        }
        options->threads[options->thread_count++] = clamp_thread_count(value, POPULATION_SIZE);
        if (*end == '\0') {
            return 1;
        }
        if (*end != ',') {
            return 0;
        }
        at = end + 1;
    }

    return options->thread_count > 0;
}

static void print_usage(const char *program) {
    printf("usage:\n");
    printf("  %s self-test\n", program);
    printf("  %s run --seed <u64> [--generations <n>] [--seconds <n>] [--threads <n|auto>] [--quality <quick|normal|deep>]\n", program);
    printf("  %s bench --seconds <n> [--seed <u64>] [--threads <n[,n...]>]\n", program);
    printf("  %s export-best\n", program);
}

static int parse_run_options(int argc, char **argv, RunOptions *options) {
    memset(options, 0, sizeof(*options));
    options->quality = QUALITY_NORMAL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 0;
            }
            options->have_seed = 1;
        } else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->generations)) {
                fprintf(stderr, "invalid --generations value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seconds) || options->seconds == 0) {
                fprintf(stderr, "invalid --seconds value\n");
                return 0;
            }
            options->have_seconds = 1;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            uint64_t threads = 0;
            const char *value = argv[++i];
            if (strcmp(value, "auto") == 0) {
                options->threads = 0;
                options->auto_threads = 1;
                options->have_threads = 1;
                continue;
            }
            if (!parse_u64(value, &threads) || threads == 0) {
                fprintf(stderr, "invalid --threads value\n");
                return 0;
            }
            options->threads = clamp_thread_count(threads, POPULATION_SIZE);
            options->have_threads = 1;
        } else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            const char *quality = argv[++i];
            if (strcmp(quality, "quick") == 0) {
                options->quality = QUALITY_QUICK;
            } else if (strcmp(quality, "normal") == 0) {
                options->quality = QUALITY_NORMAL;
            } else if (strcmp(quality, "deep") == 0) {
                options->quality = QUALITY_DEEP;
            } else {
                fprintf(stderr, "invalid --quality value\n");
                return 0;
            }
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    if (!options->have_seed) {
        fprintf(stderr, "run requires --seed <u64>\n");
        return 0;
    }
    if (!options->have_threads) {
        options->threads = default_thread_count();
    }
    return 1;
}

static int parse_bench_options(int argc, char **argv, BenchOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = 123;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 0;
            }
            options->have_seed = 1;
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seconds) || options->seconds == 0) {
                fprintf(stderr, "invalid --seconds value\n");
                return 0;
            }
            options->have_seconds = 1;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            if (!parse_thread_list(argv[++i], options)) {
                fprintf(stderr, "invalid --threads list\n");
                return 0;
            }
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    if (!options->have_seconds) {
        fprintf(stderr, "bench requires --seconds <n>\n");
        return 0;
    }
    if (options->thread_count == 0) {
        uint32_t defaults[] = { 1, 2, 4, 8, 16, 32 };
        for (uint32_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
            uint32_t clamped = clamp_thread_count(defaults[i], POPULATION_SIZE);
            if (options->thread_count == 0 || options->threads[options->thread_count - 1] != clamped) {
                options->threads[options->thread_count++] = clamped;
            }
        }
    }
    return 1;
}

static int command_export_best(void) {
    FILE *file = fopen("out/best.c", "rb");
    if (!file) {
        fprintf(stderr, "no exported candidate found; run evolution first\n");
        return 2;
    }
    fclose(file);
    printf("best candidate is available at out/best.c\n");
    return 0;
}

int main(int argc, char **argv) {
    init_console_output();

    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "self-test") == 0) {
        return command_self_test();
    }

    if (strcmp(argv[1], "run") == 0) {
        RunOptions options;
        if (!parse_run_options(argc, argv, &options)) {
            return 2;
        }
        return command_run(&options);
    }

    if (strcmp(argv[1], "bench") == 0) {
        BenchOptions options;
        if (!parse_bench_options(argc, argv, &options)) {
            return 2;
        }
        return command_bench(&options);
    }

    if (strcmp(argv[1], "export-best") == 0) {
        return command_export_best();
    }

    print_usage(argv[0]);
    return 2;
}
