#ifndef HF_CORE_H
#define HF_CORE_H

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
#define DEFAULT_NOVELTY_LANE 16
#define STARTER_COUNT 8
#define STAGNATION_REFRESH_GENERATIONS 50
#define STAGNATION_IMMIGRANT_COUNT 64
#define DEEP_EVERY 25
#define DEEP_TOP_N 8
#define COLLISION_TABLE_SIZE 65536u
#define MAX_SCORE_THREADS 32
#define MAX_BENCH_THREAD_OPTIONS 16
#define MAX_HISTORY_ROWS 4096
#define MAX_COMPARE_SEEDS 64
#define MAX_CHAMPIONS 128
#define MAX_CHAMPION_STARTERS 8
#define CHAMPION_FINGERPRINT_SEED 123ull
#define MAX_IMPROVEMENT_EVENTS 512
#define BASELINE_CASE_COUNT 3

#define FAIL_ZERO       0x01u
#define FAIL_COLLISION  0x02u
#define FAIL_BUCKET     0x04u
#define FAIL_AVALANCHE  0x08u
#define FAIL_NO_HASH    0x10u
#define FAIL_DIFFERENTIAL 0x20u
#define FAIL_SENSITIVITY 0x40u

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

typedef enum CandidateSource {
    SOURCE_RANDOM,
    SOURCE_STARTER,
    SOURCE_CHAMPION,
    SOURCE_NOVELTY
} CandidateSource;

typedef enum ImprovementReason {
    IMPROVEMENT_FIRST,
    IMPROVEMENT_QUICK,
    IMPROVEMENT_DEEP,
    IMPROVEMENT_FLAGS,
    IMPROVEMENT_TIE_BREAK
} ImprovementReason;

typedef enum ExportSelection {
    EXPORT_SELECTION_SAME,
    EXPORT_SELECTION_QUICK,
    EXPORT_SELECTION_DEEP_SEEN
} ExportSelection;

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
    uint8_t source;
} Candidate;

typedef struct ScoreScratch {
    uint64_t collision_keys[COLLISION_TABLE_SIZE];
    uint64_t collision_input_keys[COLLISION_TABLE_SIZE];
    uint64_t collision_input_seeds[COLLISION_TABLE_SIZE];
    uint32_t collision_seen[COLLISION_TABLE_SIZE];
    uint32_t collision_epoch;
    int bucket_counts[64];
    int bit_counts[64];
} ScoreScratch;

typedef struct ScoreResult {
    int64_t score;
    int64_t zero_score;
    int64_t collision_score;
    int64_t bucket_score;
    int64_t avalanche_score;
    int64_t differential_score;
    int64_t sensitivity_score;
    int64_t size_penalty;
    uint32_t fail_flags;
    uint32_t eval_count;
} ScoreResult;

typedef uint64_t (*HashEvalFn)(void *ctx, uint64_t key, uint64_t seed);

typedef struct HashSubject {
    HashEvalFn eval;
    void *ctx;
} HashSubject;

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
    int no_starter;
    int no_refresh;
    int no_champions;
    int no_crossover;
    int no_novelty;
    int have_novelty_lane;
    int starter_cap_enabled;
    uint32_t novelty_lane;
    uint32_t starter_cap;
    uint64_t starter_cap_after;
    uint64_t refresh_window;
    uint32_t refresh_immigrants;
} RunOptions;

typedef struct RunReport {
    uint64_t run_generation;
    uint64_t quick_candidates_evaluated;
    uint64_t deep_candidates_evaluated;
    uint64_t duplicate_repairs;
    uint64_t duplicate_random_replacements;
    uint64_t stagnation_refreshes;
    uint64_t adaptive_random_immigrants;
    double elapsed_seconds;
    const char *stop_reason;
    uint32_t threads;
    uint32_t last_unique_candidates;
    uint32_t champion_starters_loaded;
    uint32_t crossover_children_per_generation;
    uint32_t novelty_lane;
    uint64_t novelty_candidates_admitted;
    uint64_t last_best_novelty_score;
    uint64_t last_avg_novelty_score;
    uint32_t starter_cap_enabled;
    uint32_t starter_cap;
    uint64_t starter_cap_after;
    uint64_t starter_cap_displacements;
    uint32_t last_starter_survivors_before_cap;
    uint32_t last_starter_survivors_after_cap;
    uint32_t final_winner_starter;
    uint64_t refresh_window;
    uint32_t refresh_immigrants;
    Candidate best_quick_seen;
    Candidate best_deep_seen;
    uint32_t has_best_deep_seen;
    uint8_t export_selection;
    struct ImprovementLog {
        struct ImprovementEvent {
            uint64_t run_generation;
            double elapsed_seconds;
            uint64_t candidate_id;
            uint64_t parent_id;
            uint32_t candidate_generation;
            uint32_t instruction_count;
            int64_t quick_score;
            int64_t deep_score;
            uint32_t fail_flags;
            uint64_t total_candidates;
            uint8_t source;
            uint8_t reason;
            uint8_t deep_known;
        } events[MAX_IMPROVEMENT_EVENTS];
        uint32_t count;
        uint64_t total_count;
        uint64_t omitted_count;
    } improvements;
} RunReport;

typedef struct PolicyOptions {
    uint64_t seed;
    uint64_t seeds[MAX_COMPARE_SEEDS];
    uint64_t generations;
    uint32_t seed_count;
    uint32_t threads;
    QualityMode quality;
    int have_threads;
    int have_seed_list;
} PolicyOptions;

typedef struct PolicyResult {
    const char *policy;
    uint64_t seed;
    uint64_t best_id;
    uint8_t source;
    int64_t quick_score;
    int64_t deep_score;
    int64_t audit_worst;
    uint32_t flags;
    uint32_t audit_flags;
    uint64_t total_candidates;
    uint64_t run_generation;
    uint64_t improvement_count;
    uint64_t last_improvement_generation;
    uint64_t stagnation_refreshes;
    uint32_t last_unique_candidates;
    uint32_t no_starter;
    uint32_t no_refresh;
    uint32_t no_crossover;
    uint32_t no_novelty;
    uint32_t starter_cap_enabled;
    uint32_t starter_cap;
    uint64_t starter_cap_after;
    uint64_t starter_cap_displacements;
    uint32_t final_winner_starter;
    uint32_t novelty_lane;
    uint64_t novelty_candidates_admitted;
    uint64_t last_best_novelty_score;
    uint64_t refresh_window;
    uint32_t refresh_immigrants;
} PolicyResult;

typedef struct PolicySummary {
    const char *policy;
    uint32_t trials;
    uint32_t wins;
    uint32_t clean_runs;
    uint32_t clean_audits;
    int64_t deep_total;
    int64_t quick_total;
    int64_t best_deep;
    uint64_t total_candidates;
    uint64_t last_improvement_total;
    uint64_t refresh_total;
} PolicySummary;

typedef struct BenchOptions {
    uint64_t seed;
    uint64_t seconds;
    uint32_t threads[MAX_BENCH_THREAD_OPTIONS];
    uint32_t thread_count;
    QualityMode quality;
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
    double quick_hash_rate;
    double deep_hash_rate;
} BenchResult;

typedef struct BaselineOptions {
    uint64_t seed;
    QualityMode quality;
    int deep;
} BaselineOptions;

typedef struct BaselineResult {
    const char *name;
    const char *note;
    ScoreResult score;
} BaselineResult;

typedef uint64_t (*BaselineHashFn)(uint64_t key, uint64_t seed);

typedef struct BaselineCase {
    const char *name;
    BaselineHashFn fn;
    const char *note;
    uint64_t id;
} BaselineCase;

typedef struct BaselineEvalCtx {
    BaselineHashFn fn;
} BaselineEvalCtx;

typedef struct HistoryOptions {
    uint32_t top;
} HistoryOptions;

typedef struct ArtifactOptions {
    char out_dir[260];
} ArtifactOptions;

typedef struct PruneOptions {
    char out_dir[260];
    uint32_t keep_runs;
    uint32_t keep_days;
    int dry_run;
    int yes;
} PruneOptions;

typedef struct DbOptions {
    char action[32];
    uint32_t limit;
} DbOptions;

typedef struct CompareOptions {
    uint64_t seed;
    uint64_t generations;
    uint32_t seed_count;
    uint32_t threads;
    QualityMode quality;
    int have_threads;
} CompareOptions;

typedef struct CompareResult {
    const char *policy;
    uint64_t seed;
    uint64_t best_id;
    int64_t quick_score;
    int64_t deep_score;
    uint32_t flags;
    uint64_t total_candidates;
    uint64_t stagnation_refreshes;
    uint32_t last_unique_candidates;
} CompareResult;

typedef struct CompareSummary {
    const char *policy;
    uint32_t trials;
    uint32_t wins;
    uint32_t clean_runs;
    int64_t deep_total;
    int64_t quick_total;
    uint64_t unique_total;
    uint64_t refresh_total;
} CompareSummary;

typedef struct HistoryRow {
    long long unix_time;
    uint64_t seed;
    uint64_t run_generation;
    double elapsed_seconds;
    char stop_reason[64];
    char quality[16];
    uint32_t threads;
    uint64_t best_id;
    uint32_t candidate_generation;
    uint32_t instruction_count;
    int64_t quick_score;
    int64_t deep_score;
    uint32_t flags;
    uint64_t total_candidates;
    uint32_t starter_candidates;
    uint32_t refresh_enabled;
    uint32_t crossover_children;
} HistoryRow;

typedef struct ChampionRecord {
    Candidate candidate;
    uint64_t seed;
    long long unix_time;
    char quality[16];
    char source_report[260];
    uint64_t fingerprint;
    int64_t audit_worst;
    int64_t audit_average;
    uint32_t audit_flags;
    char path[260];
    int malformed;
} ChampionRecord;

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

extern const char *reg_names[REG_COUNT];
extern volatile int g_stop_requested;
extern int g_color_enabled;
extern const BaselineCase baseline_cases[];
extern const uint32_t baseline_case_count;

const char *c_bold(void);
const char *c_cyan(void);
const char *c_dim(void);
const char *c_green(void);
const char *c_reset(void);
const char *c_yellow(void);
const char *candidate_source_name(uint8_t source);
uint32_t deep_every_for_quality(QualityMode quality);
uint32_t deep_top_n_for_quality(QualityMode quality);
double elapsed_wall_seconds_since(double start_seconds);
void init_console_output(void);
uint64_t mix_seed(uint64_t a, uint64_t b, uint64_t c);
const char *op_name(uint8_t op);
int popcount64(uint64_t x);
const char *quality_name(QualityMode quality);
uint32_t rng_range(Rng *rng, uint32_t limit);
uint64_t rotl64(uint64_t x, unsigned n);
uint64_t rotr64(uint64_t x, unsigned n);
uint32_t score_hash_evals_per_candidate(QualityMode quality, int deep);
int score_iterations_for_quality(QualityMode quality, int deep);
uint64_t splitmix64_next(Rng *rng);
double wall_seconds_now(void);
uint32_t clamp_thread_count(uint64_t requested, uint32_t candidate_count);
int command_run(const RunOptions *options);
int command_self_test(void);
uint32_t default_thread_count(void);
int run_evolution(const RunOptions *options, int print_status, Candidate *best_out, RunReport *report_out);
void score_pool_destroy(ScorePool *pool);
int score_pool_init(ScorePool *pool, uint32_t requested_threads, uint32_t candidate_count);
int score_pool_score(ScorePool *pool, Candidate *candidates, uint32_t candidate_count, uint64_t seed, int deep, QualityMode quality);
int command_baselines(const BaselineOptions *options);
int command_bench(const BenchOptions *options);
int command_champions(void);
int command_compare(const CompareOptions *options);
int command_history(const HistoryOptions *options);
int command_artifacts(const ArtifactOptions *options);
int command_prune(const PruneOptions *options);
int command_db(const DbOptions *options);
int command_policy(const PolicyOptions *options);
int candidate_is_valid(const Candidate *candidate);
int ensure_out_dir(void);
int export_best(const Candidate *candidate, const RunOptions *options, const RunReport *report);
uint32_t load_champion_records(ChampionRecord *records, uint32_t capacity, uint64_t fingerprint, uint32_t *rescored);
uint32_t load_champion_starters(Candidate *population, Rng *rng, uint32_t start_index);
void make_compact_starter(Candidate *candidate);
void make_constant_bad(Candidate *candidate);
void make_key_only_bad(Candidate *candidate);
void make_program(Candidate *candidate, const Instruction *instructions, uint32_t instruction_count);
void make_reasonable_baseline(Candidate *candidate);
void make_seed_only_bad(Candidate *candidate);
void make_xor_only_bad(Candidate *candidate);
void print_fail_flags(FILE *out, uint32_t flags);
void seed_starter_population(Candidate *population, Rng *rng);
void append_improvement_event(struct ImprovementLog *log, const Candidate *candidate,
                                     uint64_t run_generation, double elapsed_seconds,
                                     uint64_t total_candidates, uint8_t reason);
int compare_candidates(const void *a_ptr, const void *b_ptr);
int deep_export_candidate_better(const Candidate *a, const Candidate *b);
const char *export_selection_name(uint8_t selection);
uint8_t improvement_reason_for(const Candidate *previous, const Candidate *next);
const char *improvement_reason_name(uint8_t reason);
const struct ImprovementEvent *last_improvement_event(const struct ImprovementLog *log);
ScoreResult score_baseline_case(const BaselineCase *baseline, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality);
ScoreResult score_candidate(const Candidate *candidate, ScoreScratch *scratch, uint64_t run_seed, int deep, QualityMode quality);
void update_best_deep_seen(Candidate *best_deep_seen, int *has_best_deep_seen, const Candidate *candidate);
uint64_t candidate_id(const Candidate *candidate);
uint64_t candidate_novelty_score(const Candidate *candidate, const Candidate *survivors, uint32_t survivor_count);
uint32_t count_unique_candidate_ids(const Candidate *candidates, uint32_t count);
void crossover_candidate(Candidate *child, const Candidate *a, const Candidate *b, Rng *rng);
uint64_t current_scoring_fingerprint(void);
int ensure_unique_candidate(Candidate *candidate, const Candidate *existing, uint32_t existing_count, Rng *rng);
uint64_t eval_candidate(const Candidate *candidate, uint64_t key, uint64_t seed);
uint64_t eval_candidate_subject(void *ctx, uint64_t key, uint64_t seed);
uint64_t eval_baseline_subject(void *ctx, uint64_t key, uint64_t seed);
uint32_t fail_severity(uint32_t flags);
void finalize_candidate(Candidate *candidate, Rng *rng);
void mutate_candidate(Candidate *child, const Candidate *parent, Rng *rng);
void prune_candidate_for_export(const Candidate *candidate, Candidate *out);
void random_candidate(Candidate *candidate, Rng *rng);
void random_instruction(Instruction *ins, Rng *rng, int force_hash_bias);
void repair_instruction(Instruction *ins, Rng *rng);
int writes_hash(const Candidate *candidate);

#endif
