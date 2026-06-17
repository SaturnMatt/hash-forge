#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define HF_MKDIR(path) _mkdir(path)
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
#define DEEP_EVERY 25
#define DEEP_TOP_N 8
#define COLLISION_TABLE_SIZE 65536u

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
    int have_seed;
} RunOptions;

static const char *reg_names[REG_COUNT] = { "key", "seed", "hash", "a", "b" };
static volatile int g_stop_requested = 0;

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

static uint64_t random_constant(Rng *rng) {
    return splitmix64_next(rng) | 1ull;
}

static void random_instruction(Instruction *ins, Rng *rng, int force_hash_bias) {
    ins->op = (uint8_t)rng_range(rng, OP_COUNT);
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
            case 0: ins->op = (uint8_t)rng_range(rng, OP_COUNT); break;
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

static ScoreResult score_candidate(const Candidate *candidate, ScoreScratch *scratch, uint64_t run_seed, int deep) {
    const int iterations = deep ? 128 : 16;
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
    if (a->fail_flags != b->fail_flags) return a->fail_flags < b->fail_flags ? -1 : 1;
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score ? -1 : 1;
    if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score ? -1 : 1;
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

static void print_candidate_preview(const Candidate *candidate) {
    printf("  ");
    for (uint32_t i = 0; i < candidate->instruction_count && i < 4; i++) {
        print_instruction(stdout, &candidate->instructions[i], 0);
        if (i + 1 < candidate->instruction_count && i < 3) printf(" | ");
    }
    if (candidate->instruction_count > 4) printf(" | ...");
    printf("\n");
}

static int ensure_out_dir(void) {
    if (HF_MKDIR("out") != 0) {
        /* Existing directory is fine; file creation below will catch real errors. */
    }
    return 1;
}

static int export_best(const Candidate *candidate, uint64_t seed, uint64_t generation) {
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
    fprintf(txt, "run_generation: %llu\n", (unsigned long long)generation);
    fprintf(txt, "seed: %llu\n", (unsigned long long)seed);
    fprintf(txt, "quick_score: %lld\n", (long long)candidate->quick_score);
    fprintf(txt, "deep_score: %lld\n", (long long)candidate->deep_score);
    fprintf(txt, "fail_flags: 0x%x\n", candidate->fail_flags);
    fprintf(txt, "instruction_count: %u\n\n", candidate->instruction_count);
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        fprintf(txt, "%02u: ", i);
        print_instruction(txt, &candidate->instructions[i], 0);
        fprintf(txt, "\n");
    }
    fclose(txt);

    FILE *summary = fopen("out/summary.txt", "wb");
    if (summary) {
        fprintf(summary, "hash-forge best candidate\n");
        fprintf(summary, "id=%llu generation=%u quick=%lld deep=%lld flags=0x%x\n",
                (unsigned long long)candidate->id, candidate->generation,
                (long long)candidate->quick_score, (long long)candidate->deep_score,
                candidate->fail_flags);
        fclose(summary);
    }
    return 1;
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

static void score_population(Candidate population[POPULATION_SIZE], ScoreScratch *scratch, uint64_t seed, int deep) {
    for (uint32_t i = 0; i < POPULATION_SIZE; i++) {
        ScoreResult score = score_candidate(&population[i], scratch, seed, deep);
        population[i].quick_score = score.score;
        population[i].fail_flags = score.fail_flags;
        population[i].speed_hint = score.eval_count / (population[i].instruction_count ? population[i].instruction_count : 1);
        if (deep) {
            population[i].deep_score = score.score;
        }
    }
}

static int command_self_test(void) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) {
        fprintf(stderr, "failed to allocate score scratch\n");
        return 1;
    }

    Candidate bad;
    Candidate good;
    make_constant_bad(&bad);
    make_reasonable_baseline(&good);

    ScoreResult bad_score = score_candidate(&bad, scratch, 1234, 1);
    ScoreResult good_score = score_candidate(&good, scratch, 1234, 1);
    free(scratch);

    printf("bad_constant score=%lld flags=0x%x\n", (long long)bad_score.score, bad_score.fail_flags);
    printf("baseline_mixer score=%lld flags=0x%x\n", (long long)good_score.score, good_score.fail_flags);

    if (bad_score.fail_flags == 0) {
        fprintf(stderr, "self-test failed: bad hash did not fail\n");
        return 1;
    }
    if (good_score.fail_flags & (FAIL_ZERO | FAIL_BUCKET | FAIL_NO_HASH)) {
        fprintf(stderr, "self-test failed: baseline mixer failed core flags=0x%x\n", good_score.fail_flags);
        return 1;
    }
    if (good_score.score <= bad_score.score) {
        fprintf(stderr, "self-test failed: baseline did not beat bad hash\n");
        return 1;
    }

    printf("self-test: pass\n");
    return 0;
}

static int command_run(const RunOptions *options) {
    Candidate *population = (Candidate *)calloc(POPULATION_SIZE, sizeof(*population));
    Candidate *next = (Candidate *)calloc(POPULATION_SIZE, sizeof(*next));
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!population || !next || !scratch) {
        fprintf(stderr, "failed to allocate run memory\n");
        free(population);
        free(next);
        free(scratch);
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

    while (!g_stop_requested && (options->generations == 0 || generation < options->generations)) {
        generation++;
        score_population(population, scratch, options->seed, 0);
        qsort(population, POPULATION_SIZE, sizeof(population[0]), compare_candidates);

        if (generation % DEEP_EVERY == 0 || generation == 1 || generation == options->generations) {
            for (uint32_t i = 0; i < DEEP_TOP_N; i++) {
                ScoreResult deep = score_candidate(&population[i], scratch, options->seed, 1);
                population[i].deep_score = deep.score;
                population[i].fail_flags |= deep.fail_flags;
            }
            qsort(population, POPULATION_SIZE, sizeof(population[0]), compare_candidates);
        }

        if (population[0].fail_flags < best_seen.fail_flags ||
            best_seen.quick_score == INT64_MIN ||
            (population[0].fail_flags == best_seen.fail_flags && population[0].quick_score > best_seen.quick_score)) {
            best_seen = population[0];
        }

        printf("gen=%llu best=%lld deep=%lld len=%u flags=0x%x id=%llx eval_hint=%u\n",
               (unsigned long long)generation,
               (long long)population[0].quick_score,
               (long long)population[0].deep_score,
               population[0].instruction_count,
               population[0].fail_flags,
               (unsigned long long)population[0].id,
               population[0].speed_hint);
        print_candidate_preview(&population[0]);

        for (uint32_t i = 0; i < SURVIVOR_COUNT; i++) {
            next[i] = population[i];
        }
        for (uint32_t i = SURVIVOR_COUNT; i < POPULATION_SIZE; i++) {
            uint32_t r = rng_range(&rng, SURVIVOR_COUNT * SURVIVOR_COUNT);
            uint32_t parent_index = r / SURVIVOR_COUNT;
            if (parent_index >= SURVIVOR_COUNT) parent_index = SURVIVOR_COUNT - 1;
            mutate_candidate(&next[i], &population[parent_index], &rng);
        }

        Candidate *tmp = population;
        population = next;
        next = tmp;
    }

    ScoreResult final_deep = score_candidate(&best_seen, scratch, options->seed, 1);
    best_seen.deep_score = final_deep.score;
    best_seen.fail_flags |= final_deep.fail_flags;

    int exported = export_best(&best_seen, options->seed, generation);
    printf("exported=%s out/best.c id=%llx quick=%lld deep=%lld flags=0x%x\n",
           exported ? "yes" : "no",
           (unsigned long long)best_seen.id,
           (long long)best_seen.quick_score,
           (long long)best_seen.deep_score,
           best_seen.fail_flags);

    free(population);
    free(next);
    free(scratch);
    return exported ? 0 : 1;
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

static void print_usage(const char *program) {
    printf("usage:\n");
    printf("  %s self-test\n", program);
    printf("  %s run --seed <u64> [--generations <n>]\n", program);
    printf("  %s export-best\n", program);
}

static int parse_run_options(int argc, char **argv, RunOptions *options) {
    memset(options, 0, sizeof(*options));
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
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    if (!options->have_seed) {
        fprintf(stderr, "run requires --seed <u64>\n");
        return 0;
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

    if (strcmp(argv[1], "export-best") == 0) {
        return command_export_best();
    }

    print_usage(argv[0]);
    return 2;
}
