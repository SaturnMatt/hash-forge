#include "hf_core.h"

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
    printf("  %s run --seed <u64> [--generations <n>] [--seconds <n>] [--threads <n|auto>] [--quality <quick|normal|deep>] [--panel] [--no-color] [--panel-rate-ms <n>] [--no-starter] [--no-refresh] [--no-champions] [--no-crossover] [--no-novelty|--novelty-lane <n>] [--starter-cap <n>] [--starter-cap-after <n>] [--no-starter-cap]\n", program);
    printf("  %s compare [--seed <u64>] [--seeds <n>] [--generations <n>] [--threads <n>] [--quality <quick|normal|deep>]\n", program);
    printf("  %s policy [--seed <u64>] [--seeds <n>|--seed-list <csv>] [--generations <n>] [--threads <n>] [--quality <quick|normal|deep>]\n", program);
    printf("  %s bench --seconds <n> [--seed <u64>] [--threads <n[,n...]>] [--quality <quick|normal|deep>]\n", program);
    printf("  %s baselines [--seed <u64>] [--quality <quick|normal|deep>] [--quick|--deep]\n", program);
    printf("  %s champions\n", program);
    printf("  %s history [--top <n>]\n", program);
    printf("  %s artifacts [--out-dir <out-subdir>]\n", program);
    printf("  %s prune [--dry-run] [--keep-runs <n>] [--keep-days <n>] [--out-dir <out-subdir>] [--yes]\n", program);
    printf("  %s db <init|import-champions|add-latest|top|rescore|verify> [--limit <n>]\n", program);
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
        } else if (strcmp(argv[i], "--no-starter") == 0) {
            options->no_starter = 1;
        } else if (strcmp(argv[i], "--no-refresh") == 0) {
            options->no_refresh = 1;
        } else if (strcmp(argv[i], "--no-champions") == 0) {
            options->no_champions = 1;
        } else if (strcmp(argv[i], "--no-crossover") == 0) {
            options->no_crossover = 1;
        } else if (strcmp(argv[i], "--no-novelty") == 0) {
            options->no_novelty = 1;
            options->have_novelty_lane = 1;
            options->novelty_lane = 0;
        } else if (strcmp(argv[i], "--panel") == 0) {
            options->panel = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            options->no_color = 1;
            g_color_enabled = 0;
        } else if (strcmp(argv[i], "--panel-rate-ms") == 0 && i + 1 < argc) {
            uint64_t panel_rate_ms = 0;
            if (!parse_u64(argv[++i], &panel_rate_ms) ||
                panel_rate_ms < MIN_PANEL_RATE_MS ||
                panel_rate_ms > MAX_PANEL_RATE_MS) {
                fprintf(stderr, "invalid --panel-rate-ms value; use %u..%u\n", MIN_PANEL_RATE_MS, MAX_PANEL_RATE_MS);
                return 0;
            }
            options->panel_rate_ms = (uint32_t)panel_rate_ms;
            options->have_panel_rate_ms = 1;
        } else if (strcmp(argv[i], "--novelty-lane") == 0 && i + 1 < argc) {
            uint64_t novelty_lane = 0;
            if (!parse_u64(argv[++i], &novelty_lane) || novelty_lane > POPULATION_SIZE) {
                fprintf(stderr, "invalid --novelty-lane value\n");
                return 0;
            }
            options->novelty_lane = (uint32_t)novelty_lane;
            options->have_novelty_lane = 1;
            options->no_novelty = novelty_lane == 0;
        } else if (strcmp(argv[i], "--starter-cap") == 0 && i + 1 < argc) {
            uint64_t starter_cap = 0;
            if (!parse_u64(argv[++i], &starter_cap) || starter_cap > SURVIVOR_COUNT) {
                fprintf(stderr, "invalid --starter-cap value\n");
                return 0;
            }
            options->starter_cap = (uint32_t)starter_cap;
            options->starter_cap_enabled = 1;
        } else if (strcmp(argv[i], "--starter-cap-after") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->starter_cap_after)) {
                fprintf(stderr, "invalid --starter-cap-after value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--no-starter-cap") == 0) {
            options->starter_cap_enabled = 0;
            options->starter_cap = 0;
            options->starter_cap_after = 0;
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
    if (!options->have_panel_rate_ms) {
        options->panel_rate_ms = DEFAULT_PANEL_RATE_MS;
    }
    return 1;
}

static int parse_bench_options(int argc, char **argv, BenchOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = 123;
    options->quality = QUALITY_NORMAL;
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

static int parse_compare_options(int argc, char **argv, CompareOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = 123;
    options->seed_count = 3;
    options->generations = 25;
    options->quality = QUALITY_NORMAL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            uint64_t seed_count = 0;
            if (!parse_u64(argv[++i], &seed_count) || seed_count == 0 || seed_count > MAX_COMPARE_SEEDS) {
                fprintf(stderr, "invalid --seeds value\n");
                return 0;
            }
            options->seed_count = (uint32_t)seed_count;
        } else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->generations) || options->generations == 0) {
                fprintf(stderr, "invalid --generations value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            uint64_t threads = 0;
            if (!parse_u64(argv[++i], &threads) || threads == 0) {
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
    if (!options->have_threads) {
        options->threads = default_thread_count();
    }
    return 1;
}

static int parse_policy_seed_list(const char *text, PolicyOptions *options) {
    const char *at = text;
    options->seed_count = 0;
    while (*at) {
        char *end = NULL;
        unsigned long long seed = strtoull(at, &end, 0);
        if (end == at) return 0;
        if (options->seed_count >= MAX_COMPARE_SEEDS) {
            fprintf(stderr, "too many --seed-list entries; max is %u\n", MAX_COMPARE_SEEDS);
            return 0;
        }
        options->seeds[options->seed_count++] = (uint64_t)seed;
        if (*end == '\0') break;
        if (*end != ',') return 0;
        at = end + 1;
    }
    if (options->seed_count == 0) return 0;
    options->seed = options->seeds[0];
    options->have_seed_list = 1;
    return 1;
}

static int parse_policy_options(int argc, char **argv, PolicyOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = 123;
    options->seed_count = 3;
    options->generations = 25;
    options->quality = QUALITY_NORMAL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 0;
            }
            if (!options->have_seed_list && options->seed_count == 0) options->seed_count = 3;
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            uint64_t seed_count = 0;
            if (!parse_u64(argv[++i], &seed_count) || seed_count == 0 || seed_count > MAX_COMPARE_SEEDS) {
                fprintf(stderr, "invalid --seeds value\n");
                return 0;
            }
            if (!options->have_seed_list) {
                options->seed_count = (uint32_t)seed_count;
            }
        } else if (strcmp(argv[i], "--seed-list") == 0 && i + 1 < argc) {
            if (!parse_policy_seed_list(argv[++i], options)) {
                fprintf(stderr, "invalid --seed-list value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->generations) || options->generations == 0) {
                fprintf(stderr, "invalid --generations value\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            uint64_t threads = 0;
            if (!parse_u64(argv[++i], &threads) || threads == 0) {
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
    if (!options->have_threads) {
        options->threads = default_thread_count();
    }
    return 1;
}

static int parse_baseline_options(int argc, char **argv, BaselineOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = 123;
    options->quality = QUALITY_DEEP;
    options->deep = 1;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 0;
            }
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
        } else if (strcmp(argv[i], "--quick") == 0) {
            options->deep = 0;
        } else if (strcmp(argv[i], "--deep") == 0) {
            options->deep = 1;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

static int parse_history_options(int argc, char **argv, HistoryOptions *options) {
    memset(options, 0, sizeof(*options));
    options->top = 10;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
            uint64_t top = 0;
            if (!parse_u64(argv[++i], &top) || top == 0 || top > MAX_HISTORY_ROWS) {
                fprintf(stderr, "invalid --top value\n");
                return 0;
            }
            options->top = (uint32_t)top;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

static int parse_artifact_options(int argc, char **argv, ArtifactOptions *options) {
    memset(options, 0, sizeof(*options));
    strcpy(options->out_dir, "out");
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            const char *out_dir = argv[++i];
            if (strlen(out_dir) >= sizeof(options->out_dir)) {
                fprintf(stderr, "invalid --out-dir value\n");
                return 0;
            }
            strcpy(options->out_dir, out_dir);
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

static int parse_prune_options(int argc, char **argv, PruneOptions *options) {
    memset(options, 0, sizeof(*options));
    strcpy(options->out_dir, "out");
    options->keep_runs = 200;
    options->keep_days = 14;
    options->dry_run = 1;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            options->dry_run = 1;
        } else if (strcmp(argv[i], "--yes") == 0) {
            options->yes = 1;
            options->dry_run = 0;
        } else if (strcmp(argv[i], "--keep-runs") == 0 && i + 1 < argc) {
            uint64_t keep_runs = 0;
            if (!parse_u64(argv[++i], &keep_runs) || keep_runs > MAX_HISTORY_ROWS) {
                fprintf(stderr, "invalid --keep-runs value\n");
                return 0;
            }
            options->keep_runs = (uint32_t)keep_runs;
        } else if (strcmp(argv[i], "--keep-days") == 0 && i + 1 < argc) {
            uint64_t keep_days = 0;
            if (!parse_u64(argv[++i], &keep_days) || keep_days > 36500u) {
                fprintf(stderr, "invalid --keep-days value\n");
                return 0;
            }
            options->keep_days = (uint32_t)keep_days;
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            const char *out_dir = argv[++i];
            if (strlen(out_dir) >= sizeof(options->out_dir)) {
                fprintf(stderr, "invalid --out-dir value\n");
                return 0;
            }
            strcpy(options->out_dir, out_dir);
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

static int parse_db_options(int argc, char **argv, DbOptions *options) {
    memset(options, 0, sizeof(*options));
    options->limit = 20;
    if (argc < 3) {
        fprintf(stderr, "db requires an action\n");
        return 0;
    }
    if (strlen(argv[2]) >= sizeof(options->action)) {
        fprintf(stderr, "invalid db action\n");
        return 0;
    }
    strcpy(options->action, argv[2]);
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            uint64_t limit = 0;
            if (!parse_u64(argv[++i], &limit) || limit == 0 || limit > MAX_HISTORY_ROWS) {
                fprintf(stderr, "invalid --limit value\n");
                return 0;
            }
            options->limit = (uint32_t)limit;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
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

    if (strcmp(argv[1], "compare") == 0) {
        CompareOptions options;
        if (!parse_compare_options(argc, argv, &options)) {
            return 2;
        }
        return command_compare(&options);
    }

    if (strcmp(argv[1], "policy") == 0) {
        PolicyOptions options;
        if (!parse_policy_options(argc, argv, &options)) {
            return 2;
        }
        return command_policy(&options);
    }

    if (strcmp(argv[1], "baselines") == 0) {
        BaselineOptions options;
        if (!parse_baseline_options(argc, argv, &options)) {
            return 2;
        }
        return command_baselines(&options);
    }

    if (strcmp(argv[1], "champions") == 0) {
        if (argc != 2) {
            fprintf(stderr, "champions takes no arguments\n");
            return 2;
        }
        return command_champions();
    }

    if (strcmp(argv[1], "history") == 0) {
        HistoryOptions options;
        if (!parse_history_options(argc, argv, &options)) {
            return 2;
        }
        return command_history(&options);
    }

    if (strcmp(argv[1], "artifacts") == 0) {
        ArtifactOptions options;
        if (!parse_artifact_options(argc, argv, &options)) {
            return 2;
        }
        return command_artifacts(&options);
    }

    if (strcmp(argv[1], "prune") == 0) {
        PruneOptions options;
        if (!parse_prune_options(argc, argv, &options)) {
            return 2;
        }
        return command_prune(&options);
    }

    if (strcmp(argv[1], "db") == 0) {
        DbOptions options;
        if (!parse_db_options(argc, argv, &options)) {
            return 2;
        }
        return command_db(&options);
    }

    if (strcmp(argv[1], "export-best") == 0) {
        return command_export_best();
    }

    print_usage(argv[0]);
    return 2;
}
