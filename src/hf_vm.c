#include "hf_core.h"

static uint64_t operand_value(const Instruction *ins, const uint64_t r[REG_COUNT]) {
    return ins->operand_kind == OPERAND_CONST ? ins->constant : r[ins->operand_reg];
}

uint64_t eval_candidate(const Candidate *candidate, uint64_t key, uint64_t seed) {
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

uint64_t eval_candidate_subject(void *ctx, uint64_t key, uint64_t seed) {
    return eval_candidate((const Candidate *)ctx, key, seed);
}

static uint64_t splitmix64_mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static uint64_t murmur3_fmix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

static uint64_t baseline_splitmix64_pair(uint64_t key, uint64_t seed) {
    uint64_t x = splitmix64_mix(key + 0x9e3779b97f4a7c15ull);
    x ^= seed + 0xbf58476d1ce4e5b9ull;
    return splitmix64_mix(x);
}

static uint64_t baseline_murmur3_fmix64_pair(uint64_t key, uint64_t seed) {
    uint64_t a = murmur3_fmix64(key ^ 0x9e3779b97f4a7c15ull);
    uint64_t b = murmur3_fmix64(seed ^ 0xbf58476d1ce4e5b9ull);
    return murmur3_fmix64(a ^ rotl64(b, 31) ^ 0x94d049bb133111ebull);
}

static uint64_t baseline_fnv1a64_pair(uint64_t key, uint64_t seed) {
    uint64_t h = 14695981039346656037ull;
    for (int word = 0; word < 2; word++) {
        uint64_t x = word == 0 ? key : seed;
        for (int i = 0; i < 8; i++) {
            h ^= (x >> (i * 8)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

uint64_t eval_baseline_subject(void *ctx, uint64_t key, uint64_t seed) {
    return ((const BaselineEvalCtx *)ctx)->fn(key, seed);
}

const BaselineCase baseline_cases[] = {
    {
        "splitmix64_finalizer",
        baseline_splitmix64_pair,
        "SplitMix64-style finalizer over combined key and seed.",
        0x51a1c0de00000001ull
    },
    {
        "murmur3_fmix64",
        baseline_murmur3_fmix64_pair,
        "MurmurHash3 64-bit finalizer over combined key and seed.",
        0x51a1c0de00000002ull
    },
    {
        "fnv1a64_pair",
        baseline_fnv1a64_pair,
        "FNV-1a over little-endian key and seed bytes.",
        0x51a1c0de00000003ull
    }
};

const uint32_t baseline_case_count = BASELINE_CASE_COUNT;

ScoreResult score_baseline_case(const BaselineCase *baseline, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality);

uint64_t candidate_id(const Candidate *candidate) {
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

static uint64_t hash_u64_step(uint64_t h, uint64_t x) {
    h ^= x;
    h *= 1099511628211ull;
    return h;
}

uint64_t current_scoring_fingerprint(void) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    uint64_t h = 1469598103934665603ull;
    const char *salt = getenv("HASH_FORGE_SCORE_FINGERPRINT_SALT");
    if (!scratch) return 0;
    h = hash_u64_step(h, CHAMPION_FINGERPRINT_SEED);
    h = hash_u64_step(h, QUALITY_DEEP);
    for (uint32_t i = 0; i < baseline_case_count; i++) {
        ScoreResult score = score_baseline_case(&baseline_cases[i], scratch, CHAMPION_FINGERPRINT_SEED, 1, QUALITY_DEEP);
        h = hash_u64_step(h, baseline_cases[i].id);
        h = hash_u64_step(h, (uint64_t)score.score);
        h = hash_u64_step(h, score.fail_flags);
    }
    if (salt) {
        while (*salt) {
            h = hash_u64_step(h, (unsigned char)*salt++);
        }
    }
    free(scratch);
    return h;
}

int writes_hash(const Candidate *candidate) {
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (candidate->instructions[i].dst == 2) {
            return 1;
        }
    }
    return 0;
}

static void trim_trailing_dead_instructions(Candidate *candidate) {
    while (candidate->instruction_count > MIN_PROGRAM_LEN &&
           candidate->instructions[candidate->instruction_count - 1].dst != 2) {
        candidate->instruction_count--;
    }
}

void finalize_candidate(Candidate *candidate, Rng *rng) {
    if (!writes_hash(candidate)) {
        candidate->instructions[rng_range(rng, candidate->instruction_count)].dst = 2;
    }
    trim_trailing_dead_instructions(candidate);
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void prune_candidate_for_export(const Candidate *candidate, Candidate *out) {
    uint8_t keep[MAX_INSTRUCTIONS];
    uint32_t live = 1u << 2;
    memset(keep, 0, sizeof(keep));

    for (uint32_t i = candidate->instruction_count; i > 0; i--) {
        const Instruction *ins = &candidate->instructions[i - 1];
        uint32_t dst_bit = 1u << ins->dst;
        if ((live & dst_bit) == 0) continue;
        keep[i - 1] = 1;
        live &= ~dst_bit;
        if (ins->op != OP_MOV) live |= dst_bit;
        if (ins->operand_kind == OPERAND_REG) live |= 1u << ins->operand_reg;
    }

    *out = *candidate;
    out->instruction_count = 0;
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (keep[i]) {
            out->instructions[out->instruction_count++] = candidate->instructions[i];
        }
    }
    out->id = candidate_id(out);
}

static uint32_t fail_flag_count(uint32_t flags) {
    uint32_t count = 0;
    while (flags) {
        count += flags & 1u;
        flags >>= 1;
    }
    return count;
}

uint32_t fail_severity(uint32_t flags) {
    uint32_t severity = fail_flag_count(flags);
    if (flags & FAIL_NO_HASH) severity += 1000;
    if (flags & FAIL_ZERO) severity += 500;
    if (flags & FAIL_BUCKET) severity += 200;
    if (flags & FAIL_DIFFERENTIAL) severity += 150;
    if (flags & FAIL_SENSITIVITY) severity += 125;
    if (flags & FAIL_AVALANCHE) severity += 100;
    if (flags & FAIL_COLLISION) severity += 50;
    return severity;
}

static uint64_t candidate_structure_fingerprint(const Candidate *candidate) {
    uint64_t h = 0x6a09e667f3bcc909ull;
    h = hash_u64_step(h, candidate->instruction_count);
    h = hash_u64_step(h, candidate->source);
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        const Instruction *ins = &candidate->instructions[i];
        uint64_t shape = (uint64_t)ins->op |
            ((uint64_t)ins->dst << 8) |
            ((uint64_t)ins->operand_kind << 16) |
            ((uint64_t)ins->operand_reg << 24) |
            ((uint64_t)ins->shift << 32);
        if (ins->dst == 2) shape ^= 0x9e3779b97f4a7c15ull;
        if (ins->operand_kind == OPERAND_CONST) shape ^= splitmix64_mix(ins->constant);
        h = hash_u64_step(h, shape);
    }
    return h;
}

uint64_t candidate_novelty_score(const Candidate *candidate, const Candidate *survivors, uint32_t survivor_count) {
    uint64_t fp = candidate_structure_fingerprint(candidate);
    uint32_t best_distance = 0;
    for (uint32_t i = 0; i < survivor_count; i++) {
        uint64_t other = candidate_structure_fingerprint(&survivors[i]);
        uint32_t distance = (uint32_t)popcount64(fp ^ other);
        if (distance > best_distance) best_distance = distance;
    }
    return (uint64_t)best_distance;
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

void repair_instruction(Instruction *ins, Rng *rng) {
    if (ins->dst >= REG_COUNT) ins->dst %= REG_COUNT;
    if (ins->operand_reg >= REG_COUNT) ins->operand_reg %= REG_COUNT;
    if (ins->shift == 0 || ins->shift >= 64) ins->shift = (uint8_t)(1 + rng_range(rng, 63));

    if (ins->op == OP_SHL || ins->op == OP_SHR || ins->op == OP_ROTL || ins->op == OP_ROTR) {
        ins->operand_kind = OPERAND_CONST;
    }
    if (ins->op == OP_MUL && ins->operand_kind == OPERAND_CONST) {
        ins->constant |= 1ull;
        if (ins->constant == 1ull) ins->constant = random_constant(rng);
    }
    if ((ins->op == OP_ADD || ins->op == OP_XOR) && ins->operand_kind == OPERAND_CONST && ins->constant == 0) {
        ins->constant = random_constant(rng);
    }
    if (ins->op == OP_MOV && ins->operand_kind == OPERAND_REG && ins->operand_reg == ins->dst) {
        ins->operand_reg = (uint8_t)((ins->dst + 1u + rng_range(rng, REG_COUNT - 1u)) % REG_COUNT);
    }
}

void random_instruction(Instruction *ins, Rng *rng, int force_hash_bias) {
    ins->op = random_opcode(rng);
    ins->dst = (uint8_t)(force_hash_bias ? 2 : rng_range(rng, REG_COUNT));
    ins->operand_kind = (uint8_t)rng_range(rng, 2);
    ins->operand_reg = (uint8_t)rng_range(rng, REG_COUNT);
    ins->shift = (uint8_t)(1 + rng_range(rng, 63));
    ins->constant = random_constant(rng);

    repair_instruction(ins, rng);
}

void random_candidate(Candidate *candidate, Rng *rng) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->source = SOURCE_RANDOM;
    candidate->instruction_count = MIN_PROGRAM_LEN + rng_range(rng, MAX_PROGRAM_LEN - MIN_PROGRAM_LEN + 1);
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        random_instruction(&candidate->instructions[i], rng, i == 0 || rng_range(rng, 3) == 0);
    }
    finalize_candidate(candidate, rng);
}

void mutate_candidate(Candidate *child, const Candidate *parent, Rng *rng) {
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
            repair_instruction(ins, rng);
        }
    }

    finalize_candidate(child, rng);
}

void crossover_candidate(Candidate *child, const Candidate *a, const Candidate *b, Rng *rng) {
    memset(child, 0, sizeof(*child));
    child->source = (a->source == SOURCE_CHAMPION || b->source == SOURCE_CHAMPION) ? SOURCE_CHAMPION :
        ((a->source == SOURCE_STARTER || b->source == SOURCE_STARTER) ? SOURCE_STARTER : SOURCE_RANDOM);
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

    child->parent_id = a->id ^ rotl64(b->id, 17);
    child->generation = (a->generation > b->generation ? a->generation : b->generation) + 1;
    child->quick_score = 0;
    child->fail_flags = 0;
    finalize_candidate(child, rng);
}

static int candidate_id_exists(const Candidate *candidates, uint32_t count, uint64_t id) {
    for (uint32_t i = 0; i < count; i++) {
        if (candidates[i].id == id) return 1;
    }
    return 0;
}

uint32_t count_unique_candidate_ids(const Candidate *candidates, uint32_t count) {
    uint32_t unique = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!candidate_id_exists(candidates, i, candidates[i].id)) unique++;
    }
    return unique;
}

int ensure_unique_candidate(Candidate *candidate, const Candidate *existing, uint32_t existing_count, Rng *rng) {
    int changed = 0;
    for (uint32_t attempt = 0; attempt < 8 && candidate_id_exists(existing, existing_count, candidate->id); attempt++) {
        Candidate parent = *candidate;
        mutate_candidate(candidate, &parent, rng);
        changed = 1;
    }
    if (candidate_id_exists(existing, existing_count, candidate->id)) {
        for (uint32_t attempt = 0; attempt < 8 && candidate_id_exists(existing, existing_count, candidate->id); attempt++) {
            random_candidate(candidate, rng);
            changed = 2;
        }
    }
    return changed;
}

