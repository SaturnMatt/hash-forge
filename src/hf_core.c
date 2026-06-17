#include "hf_core.h"


const char *reg_names[REG_COUNT] = { "key", "seed", "hash", "a", "b" };
volatile int g_stop_requested = 0;
int g_color_enabled = 1;

const char *c_reset(void) { return g_color_enabled ? "\x1b[0m" : ""; }
const char *c_dim(void) { return g_color_enabled ? "\x1b[2m" : ""; }
const char *c_bold(void) { return g_color_enabled ? "\x1b[1m" : ""; }
const char *c_green(void) { return g_color_enabled ? "\x1b[32m" : ""; }
const char *c_yellow(void) { return g_color_enabled ? "\x1b[33m" : ""; }
const char *c_red(void) { return g_color_enabled ? "\x1b[31m" : ""; }
const char *c_magenta(void) { return g_color_enabled ? "\x1b[35m" : ""; }
const char *c_cyan(void) { return g_color_enabled ? "\x1b[36m" : ""; }

void init_console_output(void) {
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

double wall_seconds_now(void) {
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

double elapsed_wall_seconds_since(double start_seconds) {
    return wall_seconds_now() - start_seconds;
}

uint64_t splitmix64_next(Rng *rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

uint32_t rng_range(Rng *rng, uint32_t limit) {
    return (uint32_t)(splitmix64_next(rng) % limit);
}

uint64_t mix_seed(uint64_t a, uint64_t b, uint64_t c) {
    Rng rng = { a ^ (b * 0x9e3779b97f4a7c15ull) ^ (c * 0xbf58476d1ce4e5b9ull) };
    return splitmix64_next(&rng);
}

uint64_t rotl64(uint64_t x, unsigned n) {
    return (x << n) | (x >> (64u - n));
}

uint64_t rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

int popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full;
    return (int)((x * 0x0101010101010101ull) >> 56);
}

const char *op_name(uint8_t op) {
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

const char *quality_name(QualityMode quality) {
    switch (quality) {
    case QUALITY_QUICK: return "quick";
    case QUALITY_DEEP: return "deep";
    case QUALITY_NORMAL:
    default: return "normal";
    }
}

const char *candidate_source_name(uint8_t source) {
    switch (source) {
    case SOURCE_STARTER: return "starter";
    case SOURCE_CHAMPION: return "champion";
    case SOURCE_NOVELTY: return "novelty";
    case SOURCE_RANDOM:
    default: return "random";
    }
}

int score_iterations_for_quality(QualityMode quality, int deep) {
    if (quality == QUALITY_QUICK) return deep ? 64 : 8;
    if (quality == QUALITY_DEEP) return deep ? 256 : 32;
    return deep ? 128 : 16;
}

uint32_t deep_every_for_quality(QualityMode quality) {
    if (quality == QUALITY_QUICK) return 50;
    if (quality == QUALITY_DEEP) return 10;
    return DEEP_EVERY;
}

uint32_t deep_top_n_for_quality(QualityMode quality) {
    if (quality == QUALITY_QUICK) return 4;
    if (quality == QUALITY_DEEP) return 16;
    return DEEP_TOP_N;
}

uint32_t score_hash_evals_per_candidate(QualityMode quality, int deep) {
    int iterations = score_iterations_for_quality(quality, deep);
    int bucket_iterations = deep ? iterations * 2 : iterations;
    int bucket_step = deep ? 1 : 3;
    int bucket_groups = 0;
    for (int buckets = 2; buckets <= 64; buckets += bucket_step) bucket_groups++;
    int bit_step = deep ? 1 : 4;
    int avalanche_bits = 0;
    for (int bit = 0; bit < 64; bit += bit_step) avalanche_bits++;

    uint32_t evals = 4;
    evals += (uint32_t)(iterations * 13);
    evals += (uint32_t)(2 * bucket_groups * bucket_iterations);
    evals += (uint32_t)(5 * avalanche_bits * iterations * 2);
    evals += (uint32_t)(iterations * 6);
    evals += (uint32_t)(iterations * 4);
    return evals;
}

