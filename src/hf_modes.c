#include "hf_core.h"

static int compare_result_better(const CompareResult *a, const CompareResult *b) {
    uint32_t a_severity = fail_severity(a->flags);
    uint32_t b_severity = fail_severity(b->flags);
    if (a_severity != b_severity) return a_severity < b_severity;
    if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score;
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score;
    return a->total_candidates > b->total_candidates;
}

static int write_compare_report(const CompareOptions *options, const CompareResult *results, uint32_t result_count, const CompareSummary *summaries, uint32_t summary_count) {
    ensure_out_dir();
    FILE *md = fopen("out/compare.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/compare.md\n");
        return 0;
    }

    fprintf(md, "# hash-forge policy comparison\n\n");
    fprintf(md, "## Settings\n\n");
    fprintf(md, "- First seed: `%llu`\n", (unsigned long long)options->seed);
    fprintf(md, "- Seed count: `%u`\n", options->seed_count);
    fprintf(md, "- Generations per trial: `%llu`\n", (unsigned long long)options->generations);
    fprintf(md, "- Threads: `%u`\n", options->threads);
    fprintf(md, "- Quality: `%s`\n\n", quality_name(options->quality));

    fprintf(md, "## Policy summary\n\n");
    fprintf(md, "| policy | trials | wins | clean runs | avg deep | avg quick | avg unique | avg refreshes |\n");
    fprintf(md, "|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (uint32_t i = 0; i < summary_count; i++) {
        const CompareSummary *s = &summaries[i];
        int64_t avg_deep = s->trials ? s->deep_total / (int64_t)s->trials : 0;
        int64_t avg_quick = s->trials ? s->quick_total / (int64_t)s->trials : 0;
        uint64_t avg_unique = s->trials ? s->unique_total / s->trials : 0;
        uint64_t avg_refresh = s->trials ? s->refresh_total / s->trials : 0;
        fprintf(md, "| %s | %u | %u | %u | %lld | %lld | %llu | %llu |\n",
                s->policy,
                s->trials,
                s->wins,
                s->clean_runs,
                (long long)avg_deep,
                (long long)avg_quick,
                (unsigned long long)avg_unique,
                (unsigned long long)avg_refresh);
    }

    fprintf(md, "\n## Trial results\n\n");
    fprintf(md, "| policy | seed | deep | quick | flags | flag names | total candidates | refreshes | unique last gen | best id |\n");
    fprintf(md, "|---|---:|---:|---:|---|---|---:|---:|---:|---|\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(md, "| %s | %llu | %lld | %lld | `0x%x` | ",
                results[i].policy,
                (unsigned long long)results[i].seed,
                (long long)results[i].deep_score,
                (long long)results[i].quick_score,
                results[i].flags);
        print_fail_flags(md, results[i].flags);
        fprintf(md, " | %llu | %llu | %u | `%llx` |\n",
                (unsigned long long)results[i].total_candidates,
                (unsigned long long)results[i].stagnation_refreshes,
                results[i].last_unique_candidates,
                (unsigned long long)results[i].best_id);
    }

    fprintf(md, "\n## Interpretation\n\n");
    fprintf(md, "This is a deterministic short-run policy comparison. Treat it as local tuning evidence, not proof that one policy dominates all future runs.\n");
    fclose(md);
    return 1;
}

int command_compare(const CompareOptions *options) {
    static const struct {
        const char *name;
        int no_starter;
        int no_refresh;
    } policies[] = {
        { "default", 0, 0 },
        { "no-starter", 1, 0 },
        { "no-refresh", 0, 1 },
        { "bare", 1, 1 }
    };

    CompareResult results[MAX_COMPARE_SEEDS * 4];
    CompareSummary summaries[4];
    uint32_t result_count = 0;
    uint32_t best_index = 0;
    memset(summaries, 0, sizeof(summaries));
    for (uint32_t p = 0; p < sizeof(policies) / sizeof(policies[0]); p++) {
        summaries[p].policy = policies[p].name;
    }

    printf("\n%s%sHash Forge policy compare%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sseeds%s        %llu..%llu\n", c_dim(), c_reset(),
           (unsigned long long)options->seed,
           (unsigned long long)(options->seed + options->seed_count - 1u));
    printf("  %sgenerations%s  %llu\n", c_dim(), c_reset(), (unsigned long long)options->generations);
    printf("  %sthreads%s      %u\n", c_dim(), c_reset(), options->threads);
    printf("  %squality%s      %s\n\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("%s%-11s  %8s  %12s  %12s  %5s  %10s  %8s  %16s%s\n",
           c_dim(), "policy", "seed", "deep", "quick", "flags", "candidates", "unique", "best id", c_reset());

    for (uint32_t s = 0; s < options->seed_count; s++) {
        uint32_t seed_best_start = result_count;
        uint32_t seed_best_index = result_count;
        for (uint32_t p = 0; p < sizeof(policies) / sizeof(policies[0]); p++) {
            RunOptions run_options;
            memset(&run_options, 0, sizeof(run_options));
            run_options.seed = options->seed + s;
            run_options.generations = options->generations;
            run_options.threads = options->threads;
            run_options.quality = options->quality;
            run_options.have_seed = 1;
            run_options.have_threads = 1;
            run_options.no_starter = policies[p].no_starter;
            run_options.no_refresh = policies[p].no_refresh;

            Candidate best;
            RunReport report;
            if (run_evolution(&run_options, 0, &best, &report) != 0) {
                return 1;
            }

            CompareResult *result = &results[result_count++];
            result->policy = policies[p].name;
            result->seed = run_options.seed;
            result->best_id = best.id;
            result->quick_score = best.quick_score;
            result->deep_score = best.deep_score;
            result->flags = best.fail_flags;
            result->total_candidates = report.quick_candidates_evaluated + report.deep_candidates_evaluated;
            result->stagnation_refreshes = report.stagnation_refreshes;
            result->last_unique_candidates = report.last_unique_candidates;
            if (result_count == 1 || compare_result_better(result, &results[best_index])) {
                best_index = result_count - 1;
            }
            if (result_count == seed_best_start + 1 || compare_result_better(result, &results[seed_best_index])) {
                seed_best_index = result_count - 1;
            }

            summaries[p].trials++;
            summaries[p].clean_runs += result->flags == 0 ? 1u : 0u;
            summaries[p].deep_total += result->deep_score;
            summaries[p].quick_total += result->quick_score;
            summaries[p].unique_total += result->last_unique_candidates;
            summaries[p].refresh_total += result->stagnation_refreshes;

            printf("%-11s  %8llu  %12lld  %12lld  0x%02x  %10llu  %8u  %s%016llx%s\n",
                   result->policy,
                   (unsigned long long)result->seed,
                   (long long)result->deep_score,
                   (long long)result->quick_score,
                   result->flags,
                   (unsigned long long)result->total_candidates,
                   result->last_unique_candidates,
                   c_cyan(), (unsigned long long)result->best_id, c_reset());
        }
        summaries[seed_best_index - seed_best_start].wins++;
    }

    printf("\n%s%-11s  %6s  %6s  %6s  %12s  %12s  %8s%s\n",
           c_dim(), "policy", "trials", "wins", "clean", "avg deep", "avg quick", "unique", c_reset());
    for (uint32_t i = 0; i < sizeof(policies) / sizeof(policies[0]); i++) {
        int64_t avg_deep = summaries[i].trials ? summaries[i].deep_total / (int64_t)summaries[i].trials : 0;
        int64_t avg_quick = summaries[i].trials ? summaries[i].quick_total / (int64_t)summaries[i].trials : 0;
        uint64_t avg_unique = summaries[i].trials ? summaries[i].unique_total / summaries[i].trials : 0;
        printf("%-11s  %6u  %6u  %6u  %12lld  %12lld  %8llu\n",
               summaries[i].policy,
               summaries[i].trials,
               summaries[i].wins,
               summaries[i].clean_runs,
               (long long)avg_deep,
               (long long)avg_quick,
               (unsigned long long)avg_unique);
    }

    int wrote = write_compare_report(options, results, result_count, summaries, (uint32_t)(sizeof(policies) / sizeof(policies[0])));
    printf("\n%s%sCompare complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %sbest policy%s  %s seed %llu deep %lld flags 0x%x\n",
           c_dim(), c_reset(),
           results[best_index].policy,
           (unsigned long long)results[best_index].seed,
           (long long)results[best_index].deep_score,
           results[best_index].flags);
    printf("  %soutput%s       %s\n", c_dim(), c_reset(), wrote ? "out/compare.md" : "export failed");
    return wrote ? 0 : 1;
}

static int policy_result_better(const PolicyResult *a, const PolicyResult *b) {
    uint32_t a_severity = fail_severity(a->flags);
    uint32_t b_severity = fail_severity(b->flags);
    if (a_severity != b_severity) return a_severity < b_severity;
    uint32_t a_audit_severity = fail_severity(a->audit_flags);
    uint32_t b_audit_severity = fail_severity(b->audit_flags);
    if (a_audit_severity != b_audit_severity) return a_audit_severity < b_audit_severity;
    if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score;
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score;
    return a->total_candidates > b->total_candidates;
}

static int audit_policy_candidate(const Candidate *candidate, uint64_t run_seed, QualityMode quality, int64_t *worst_score, uint32_t *combined_flags) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) return 0;
    int64_t worst = INT64_MAX;
    uint32_t flags = 0;
    for (uint32_t i = 0; i < 3; i++) {
        uint64_t audit_seed = mix_seed(run_seed, candidate->id, 2000u + i);
        ScoreResult score = score_candidate(candidate, scratch, audit_seed, 1, quality);
        if (score.score < worst) worst = score.score;
        flags |= score.fail_flags;
    }
    free(scratch);
    *worst_score = worst;
    *combined_flags = flags;
    return 1;
}

static int write_policy_outputs(const PolicyOptions *options, const PolicyResult *results, uint32_t result_count,
                                const PolicySummary *summaries, uint32_t summary_count) {
    ensure_out_dir();
    FILE *csv = fopen("out/policy.csv", "wb");
    if (!csv) {
        fprintf(stderr, "failed to open out/policy.csv\n");
        return 0;
    }
    fprintf(csv, "policy,seed,best_id,source,deep,quick,flags,audit_worst,audit_flags,total_candidates,run_generation,improvement_count,last_improvement_generation,last_unique,refreshes,no_starter,no_refresh,no_crossover,no_novelty,novelty_lane,novelty_candidates,last_best_novelty,starter_cap_enabled,starter_cap,starter_cap_after,starter_cap_displacements,final_winner_starter,refresh_window,refresh_immigrants\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(csv, "%s,%llu,%llx,%s,%lld,%lld,0x%x,%lld,0x%x,%llu,%llu,%llu,%llu,%u,%llu,%u,%u,%u,%u,%u,%llu,%llu,%u,%u,%llu,%llu,%u,%llu,%u\n",
                results[i].policy,
                (unsigned long long)results[i].seed,
                (unsigned long long)results[i].best_id,
                candidate_source_name(results[i].source),
                (long long)results[i].deep_score,
                (long long)results[i].quick_score,
                results[i].flags,
                (long long)results[i].audit_worst,
                results[i].audit_flags,
                (unsigned long long)results[i].total_candidates,
                (unsigned long long)results[i].run_generation,
                (unsigned long long)results[i].improvement_count,
                (unsigned long long)results[i].last_improvement_generation,
                results[i].last_unique_candidates,
                (unsigned long long)results[i].stagnation_refreshes,
                results[i].no_starter,
                results[i].no_refresh,
                results[i].no_crossover,
                results[i].no_novelty,
                results[i].novelty_lane,
                (unsigned long long)results[i].novelty_candidates_admitted,
                (unsigned long long)results[i].last_best_novelty_score,
                results[i].starter_cap_enabled,
                results[i].starter_cap,
                (unsigned long long)results[i].starter_cap_after,
                (unsigned long long)results[i].starter_cap_displacements,
                results[i].final_winner_starter,
                (unsigned long long)results[i].refresh_window,
                results[i].refresh_immigrants);
    }
    fclose(csv);

    FILE *md = fopen("out/policy.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/policy.md\n");
        return 0;
    }

    fprintf(md, "# hash-forge policy comparison\n\n");
    fprintf(md, "## Settings\n\n");
    fprintf(md, "- Seed count: `%u`\n", options->seed_count);
    fprintf(md, "- Generations per trial: `%llu`\n", (unsigned long long)options->generations);
    fprintf(md, "- Threads: `%u`\n", options->threads);
    fprintf(md, "- Quality: `%s`\n", quality_name(options->quality));
    fprintf(md, "- Champion starters: `disabled`\n\n");
    fprintf(md, "Seeds:");
    for (uint32_t i = 0; i < options->seed_count; i++) {
        uint64_t seed = options->have_seed_list ? options->seeds[i] : options->seed + i;
        fprintf(md, " `%llu`", (unsigned long long)seed);
    }
    fprintf(md, "\n\n");

    fprintf(md, "## Policy definitions\n\n");
    fprintf(md, "| policy | starter | refresh | crossover | novelty lane | starter cap | cap after | refresh window | refresh immigrants |\n");
    fprintf(md, "|---|---|---|---|---:|---:|---:|---:|---:|\n");
    for (uint32_t i = 0; i < summary_count; i++) {
        const PolicyResult *sample = NULL;
        for (uint32_t j = 0; j < result_count; j++) {
            if (strcmp(results[j].policy, summaries[i].policy) == 0) {
                sample = &results[j];
                break;
            }
        }
        if (!sample) continue;
        fprintf(md, "| %s | %s | %s | %s | %u | %u | %llu | %llu | %u |\n",
                summaries[i].policy,
                sample->no_starter ? "off" : "on",
                sample->no_refresh ? "off" : "on",
                sample->no_crossover ? "off" : "on",
                sample->novelty_lane,
                sample->starter_cap_enabled ? sample->starter_cap : SURVIVOR_COUNT,
                (unsigned long long)sample->starter_cap_after,
                (unsigned long long)sample->refresh_window,
                sample->refresh_immigrants);
    }

    fprintf(md, "\n## Policy summary\n\n");
    fprintf(md, "| policy | trials | wins | clean winners | clean audits | avg deep | best deep | avg total candidates | avg last improvement gen | avg refreshes |\n");
    fprintf(md, "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (uint32_t i = 0; i < summary_count; i++) {
        const PolicySummary *s = &summaries[i];
        int64_t avg_deep = s->trials ? s->deep_total / (int64_t)s->trials : 0;
        uint64_t avg_total = s->trials ? s->total_candidates / s->trials : 0;
        uint64_t avg_last_improvement = s->trials ? s->last_improvement_total / s->trials : 0;
        uint64_t avg_refresh = s->trials ? s->refresh_total / s->trials : 0;
        fprintf(md, "| %s | %u | %u | %u | %u | %lld | %lld | %llu | %llu | %llu |\n",
                s->policy,
                s->trials,
                s->wins,
                s->clean_runs,
                s->clean_audits,
                (long long)avg_deep,
                (long long)s->best_deep,
                (unsigned long long)avg_total,
                (unsigned long long)avg_last_improvement,
                (unsigned long long)avg_refresh);
    }

    fprintf(md, "\n## Trial results\n\n");
    fprintf(md, "| policy | seed | deep | quick | flags | audit worst | audit flags | candidates | improvements | last improvement gen | novelty admitted | best novelty | starter cap displacements | final starter | refreshes | unique | source | best id |\n");
    fprintf(md, "|---|---:|---:|---:|---|---:|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---|---|\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(md, "| %s | %llu | %lld | %lld | `0x%x` (",
                results[i].policy,
                (unsigned long long)results[i].seed,
                (long long)results[i].deep_score,
                (long long)results[i].quick_score,
                results[i].flags);
        print_fail_flags(md, results[i].flags);
        fprintf(md, ") | %lld | `0x%x` (",
                (long long)results[i].audit_worst,
                results[i].audit_flags);
        print_fail_flags(md, results[i].audit_flags);
        fprintf(md, ") | %llu | %llu | %llu | %llu | %llu | %llu | %s | %llu | %u | %s | `%llx` |\n",
                (unsigned long long)results[i].total_candidates,
                (unsigned long long)results[i].improvement_count,
                (unsigned long long)results[i].last_improvement_generation,
                (unsigned long long)results[i].novelty_candidates_admitted,
                (unsigned long long)results[i].last_best_novelty_score,
                (unsigned long long)results[i].starter_cap_displacements,
                results[i].final_winner_starter ? "yes" : "no",
                (unsigned long long)results[i].stagnation_refreshes,
                results[i].last_unique_candidates,
                candidate_source_name(results[i].source),
                (unsigned long long)results[i].best_id);
    }

    fprintf(md, "\n## Interpretation\n\n");
    fprintf(md, "Every policy is run against the same deterministic seed budget and generation count. ");
    fprintf(md, "Use clean flags first, audit flags second, then deep score and convergence telemetry when choosing longer experiments.\n");
    fclose(md);
    return 1;
}

int command_policy(const PolicyOptions *options) {
    static const struct {
        const char *name;
        int no_starter;
        int no_refresh;
        int no_crossover;
        int no_novelty;
        uint32_t novelty_lane;
        int starter_cap_enabled;
        uint32_t starter_cap;
        uint64_t starter_cap_after;
        uint64_t refresh_window;
        uint32_t refresh_immigrants;
    } policies[] = {
        { "default", 0, 0, 0, 0, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "starter-cap", 0, 0, 0, 0, 0, 1, 8, 10, 0, 0 },
        { "no-novelty", 0, 0, 0, 1, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "no-crossover", 0, 0, 1, 0, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "no-starter", 1, 0, 0, 0, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "no-refresh", 0, 1, 0, 0, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "bare", 1, 1, 1, 1, 0, 0, SURVIVOR_COUNT, 0, 0, 0 },
        { "refresh-strong", 0, 0, 0, 0, 0, 0, SURVIVOR_COUNT, 0, 20, 96 }
    };
    enum { POLICY_COUNT = sizeof(policies) / sizeof(policies[0]) };

    PolicyResult results[MAX_COMPARE_SEEDS * POLICY_COUNT];
    PolicySummary summaries[POLICY_COUNT];
    uint32_t result_count = 0;
    uint32_t best_index = 0;
    memset(summaries, 0, sizeof(summaries));
    for (uint32_t i = 0; i < POLICY_COUNT; i++) {
        summaries[i].policy = policies[i].name;
        summaries[i].best_deep = INT64_MIN;
    }

    printf("\n%s%sHash Forge policy comparison%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sseeds%s        %u\n", c_dim(), c_reset(), options->seed_count);
    printf("  %sgenerations%s  %llu\n", c_dim(), c_reset(), (unsigned long long)options->generations);
    printf("  %sthreads%s      %u\n", c_dim(), c_reset(), options->threads);
    printf("  %squality%s      %s\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("  %schampions%s    disabled\n\n", c_dim(), c_reset());
    printf("%s%-15s  %8s  %12s  %12s  %5s  %12s  %5s  %8s  %16s%s\n",
           c_dim(), "policy", "seed", "deep", "quick", "flags", "audit worst", "aflag", "improve", "best id", c_reset());

    for (uint32_t s = 0; s < options->seed_count; s++) {
        uint64_t seed = options->have_seed_list ? options->seeds[s] : options->seed + s;
        uint32_t seed_best_start = result_count;
        uint32_t seed_best_index = result_count;
        for (uint32_t p = 0; p < POLICY_COUNT; p++) {
            RunOptions run_options;
            memset(&run_options, 0, sizeof(run_options));
            run_options.seed = seed;
            run_options.generations = options->generations;
            run_options.threads = options->threads;
            run_options.quality = options->quality;
            run_options.have_seed = 1;
            run_options.have_threads = 1;
            run_options.no_starter = policies[p].no_starter;
            run_options.no_refresh = policies[p].no_refresh;
            run_options.no_crossover = policies[p].no_crossover;
            run_options.no_novelty = policies[p].no_novelty;
            run_options.novelty_lane = policies[p].novelty_lane;
            run_options.have_novelty_lane = policies[p].no_novelty || policies[p].novelty_lane;
            run_options.starter_cap_enabled = policies[p].starter_cap_enabled;
            run_options.starter_cap = policies[p].starter_cap;
            run_options.starter_cap_after = policies[p].starter_cap_after;
            run_options.no_champions = 1;
            run_options.refresh_window = policies[p].refresh_window;
            run_options.refresh_immigrants = policies[p].refresh_immigrants;

            Candidate best;
            RunReport report;
            if (run_evolution(&run_options, 0, &best, &report) != 0) {
                return 1;
            }

            int64_t audit_worst = INT64_MIN;
            uint32_t audit_flags = 0xffffffffu;
            if (!audit_policy_candidate(&best, seed, options->quality, &audit_worst, &audit_flags)) {
                audit_worst = best.deep_score;
                audit_flags = best.fail_flags;
            }
            const struct ImprovementEvent *last_improvement = last_improvement_event(&report.improvements);

            PolicyResult *result = &results[result_count++];
            result->policy = policies[p].name;
            result->seed = seed;
            result->best_id = best.id;
            result->source = best.source;
            result->quick_score = best.quick_score;
            result->deep_score = best.deep_score;
            result->flags = best.fail_flags;
            result->audit_worst = audit_worst;
            result->audit_flags = audit_flags;
            result->total_candidates = report.quick_candidates_evaluated + report.deep_candidates_evaluated;
            result->run_generation = report.run_generation;
            result->improvement_count = report.improvements.total_count;
            result->last_improvement_generation = last_improvement ? last_improvement->run_generation : 0u;
            result->stagnation_refreshes = report.stagnation_refreshes;
            result->last_unique_candidates = report.last_unique_candidates;
            result->no_starter = policies[p].no_starter ? 1u : 0u;
            result->no_refresh = policies[p].no_refresh ? 1u : 0u;
            result->no_crossover = policies[p].no_crossover ? 1u : 0u;
            result->no_novelty = policies[p].no_novelty ? 1u : 0u;
            result->novelty_lane = report.novelty_lane;
            result->novelty_candidates_admitted = report.novelty_candidates_admitted;
            result->last_best_novelty_score = report.last_best_novelty_score;
            result->starter_cap_enabled = report.starter_cap_enabled;
            result->starter_cap = report.starter_cap;
            result->starter_cap_after = report.starter_cap_after;
            result->starter_cap_displacements = report.starter_cap_displacements;
            result->final_winner_starter = report.final_winner_starter;
            result->refresh_window = report.refresh_window;
            result->refresh_immigrants = report.refresh_window ? report.refresh_immigrants : 0u;

            if (result_count == 1 || policy_result_better(result, &results[best_index])) {
                best_index = result_count - 1;
            }
            if (result_count == seed_best_start + 1 || policy_result_better(result, &results[seed_best_index])) {
                seed_best_index = result_count - 1;
            }

            summaries[p].trials++;
            summaries[p].clean_runs += result->flags == 0 ? 1u : 0u;
            summaries[p].clean_audits += result->audit_flags == 0 ? 1u : 0u;
            summaries[p].deep_total += result->deep_score;
            summaries[p].quick_total += result->quick_score;
            if (result->deep_score > summaries[p].best_deep) summaries[p].best_deep = result->deep_score;
            summaries[p].total_candidates += result->total_candidates;
            summaries[p].last_improvement_total += result->last_improvement_generation;
            summaries[p].refresh_total += result->stagnation_refreshes;

            printf("%-15s  %8llu  %12lld  %12lld  0x%02x  %12lld  0x%02x  %8llu  %s%016llx%s\n",
                   result->policy,
                   (unsigned long long)result->seed,
                   (long long)result->deep_score,
                   (long long)result->quick_score,
                   result->flags,
                   (long long)result->audit_worst,
                   result->audit_flags,
                   (unsigned long long)result->improvement_count,
                   c_cyan(), (unsigned long long)result->best_id, c_reset());
        }
        summaries[seed_best_index - seed_best_start].wins++;
    }

    int wrote = write_policy_outputs(options, results, result_count, summaries, POLICY_COUNT);
    printf("\n%s%sPolicy complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %sbest policy%s  %s seed %llu deep %lld flags 0x%x audit 0x%x\n",
           c_dim(), c_reset(),
           results[best_index].policy,
           (unsigned long long)results[best_index].seed,
           (long long)results[best_index].deep_score,
           results[best_index].flags,
           results[best_index].audit_flags);
    printf("  %soutput%s       %s\n", c_dim(), c_reset(), wrote ? "out/policy.md, out/policy.csv" : "export failed");
    return wrote ? 0 : 1;
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
    fprintf(md, "- Quality: `%s`\n", quality_name(options->quality));
    fprintf(md, "- Population size: `%u`\n", POPULATION_SIZE);
    fprintf(md, "- Deep sample size: `%u`\n", DEEP_TOP_N);
    fprintf(md, "- Quick hash evals per candidate: `%u`\n", score_hash_evals_per_candidate(options->quality, 0));
    fprintf(md, "- Deep hash evals per candidate: `%u`\n\n", score_hash_evals_per_candidate(options->quality, 1));

    fprintf(md, "## Results\n\n");
    fprintf(md, "| requested threads | actual threads | quick candidates | quick/sec | quick hash/sec | deep candidates | deep/sec | deep hash/sec |\n");
    fprintf(md, "|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(md, "| %u | %u | %llu | %.0f | %.0f | %llu | %.0f | %.0f |\n",
                results[i].requested_threads,
                results[i].actual_threads,
                (unsigned long long)results[i].quick_candidates,
                results[i].quick_rate,
                results[i].quick_hash_rate,
                (unsigned long long)results[i].deep_candidates,
                results[i].deep_rate,
                results[i].deep_hash_rate);
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
        fprintf(md, "- Best quick hash throughput: `%.0f` hash evals/sec.\n",
                results[best_quick].quick_hash_rate);
        fprintf(md, "- Best deep hash throughput: `%.0f` hash evals/sec.\n",
                results[best_deep].deep_hash_rate);
        fprintf(md, "- Benchmark rates are machine-local guidance, not a quality score.\n");
    }

    fclose(md);
    return 1;
}

static int run_bench_phase(ScorePool *pool, Candidate *candidates, uint32_t candidate_count, uint64_t seed, int deep, QualityMode quality, double seconds, uint64_t *evaluated, double *elapsed) {
    double start = wall_seconds_now();
    double now = start;
    *evaluated = 0;

    do {
        if (!score_pool_score(pool, candidates, candidate_count, seed, deep, quality)) {
            return 0;
        }
        *evaluated += candidate_count;
        now = wall_seconds_now();
    } while (now - start < seconds);

    *elapsed = now - start;
    if (*elapsed <= 0.0) *elapsed = 0.000001;
    return 1;
}

int command_bench(const BenchOptions *options) {
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
    printf("  %squality%s      %s\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("  %spopulation%s   %u candidates\n", c_dim(), c_reset(), POPULATION_SIZE);
    uint32_t quick_hashes_per_candidate = score_hash_evals_per_candidate(options->quality, 0);
    uint32_t deep_hashes_per_candidate = score_hash_evals_per_candidate(options->quality, 1);
    printf("  %shash evals%s   quick %u/candidate, deep %u/candidate\n",
           c_dim(), c_reset(), quick_hashes_per_candidate, deep_hashes_per_candidate);
    printf("\n%s%9s  %7s  %14s  %12s  %12s  %14s  %12s  %12s%s\n",
           c_dim(), "requested", "actual", "quick evals", "quick/sec", "qhash/sec", "deep evals", "deep/sec", "dhash/sec", c_reset());

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
                             options->quality, phase_seconds, &result->quick_candidates, &result->quick_seconds)) {
            score_pool_destroy(&pool);
            free(population);
            return 1;
        }
        if (!run_bench_phase(&pool, population, DEEP_TOP_N, mix_seed(options->seed, requested, 2), 1,
                             options->quality, phase_seconds, &result->deep_candidates, &result->deep_seconds)) {
            score_pool_destroy(&pool);
            free(population);
            return 1;
        }
        score_pool_destroy(&pool);

        result->quick_rate = (double)result->quick_candidates / result->quick_seconds;
        result->deep_rate = (double)result->deep_candidates / result->deep_seconds;
        result->quick_hash_rate = result->quick_rate * (double)quick_hashes_per_candidate;
        result->deep_hash_rate = result->deep_rate * (double)deep_hashes_per_candidate;
        printf("%s%9u%s  %7u  %14llu  %s%12.0f%s  %12.0f  %14llu  %s%12.0f%s  %12.0f\n",
               c_bold(), result->requested_threads, c_reset(),
               result->actual_threads,
               (unsigned long long)result->quick_candidates,
               c_green(), result->quick_rate, c_reset(),
               result->quick_hash_rate,
               (unsigned long long)result->deep_candidates,
               c_green(), result->deep_rate, c_reset(),
               result->deep_hash_rate);
    }

    int wrote = write_bench_report(options, results, result_count);
    printf("\n%s%sBenchmark complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? "out/bench.md" : "export failed");

    free(population);
    return wrote ? 0 : 1;
}

static int write_baselines_report(const BaselineOptions *options, const BaselineResult *results, uint32_t result_count) {
    ensure_out_dir();
    FILE *md = fopen("out/baselines.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/baselines.md\n");
        return 0;
    }

    fprintf(md, "# hash-forge established hash baselines\n\n");
    fprintf(md, "## Settings\n\n");
    fprintf(md, "- Seed: `%llu`\n", (unsigned long long)options->seed);
    fprintf(md, "- Quality: `%s`\n", quality_name(options->quality));
    fprintf(md, "- Depth: `%s`\n", options->deep ? "deep" : "quick");
    fprintf(md, "- Hash evals per reference: `%u`\n\n", score_hash_evals_per_candidate(options->quality, options->deep));

    fprintf(md, "## Results\n\n");
    fprintf(md, "| reference | score | flags | evals | note |\n");
    fprintf(md, "|---|---:|---|---:|---|\n");
    for (uint32_t i = 0; i < result_count; i++) {
        fprintf(md, "| %s | %lld | `0x%x` (",
                results[i].name,
                (long long)results[i].score.score,
                results[i].score.fail_flags);
        print_fail_flags(md, results[i].score.fail_flags);
        fprintf(md, ") | %u | %s |\n",
                results[i].score.eval_count,
                results[i].note);
    }

    fprintf(md, "\n## How to use this\n\n");
    fprintf(md, "Use these rows as non-cryptographic reference marks for the lab. ");
    fprintf(md, "A forged candidate should be compared by decoded fail flags first, then score, then speed and exported instruction count. ");
    fprintf(md, "These tests are exploratory quality signals; they are not cryptographic proof and they do not replace broader domain-specific validation.\n");

    fclose(md);
    return 1;
}

int command_baselines(const BaselineOptions *options) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    BaselineResult results[BASELINE_CASE_COUNT];
    if (!scratch) {
        fprintf(stderr, "failed to allocate baseline score scratch\n");
        return 1;
    }

    printf("\n%s%sHash Forge established baselines%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sseed%s       %llu\n", c_dim(), c_reset(), (unsigned long long)options->seed);
    printf("  %squality%s    %s\n", c_dim(), c_reset(), quality_name(options->quality));
    printf("  %sdepth%s      %s\n", c_dim(), c_reset(), options->deep ? "deep" : "quick");
    printf("  %shash evals%s %u/reference\n\n",
           c_dim(), c_reset(), score_hash_evals_per_candidate(options->quality, options->deep));

    printf("%s%-24s  %14s  %8s  %8s  %s%s\n",
           c_dim(), "reference", "score", "flags", "evals", "decoded flags", c_reset());

    uint32_t result_count = 0;
    for (uint32_t i = 0; i < baseline_case_count; i++) {
        BaselineResult *result = &results[result_count++];
        result->name = baseline_cases[i].name;
        result->note = baseline_cases[i].note;
        result->score = score_baseline_case(&baseline_cases[i], scratch, options->seed, options->deep, options->quality);

        printf("%-24s  %s%14lld%s  0x%06x  %8u  ",
               result->name,
               result->score.fail_flags ? c_yellow() : c_green(),
               (long long)result->score.score,
               c_reset(),
               result->score.fail_flags,
               result->score.eval_count);
        print_fail_flags(stdout, result->score.fail_flags);
        printf("\n");
    }

    int wrote = write_baselines_report(options, results, result_count);
    printf("\n%s%sBaselines complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? "out/baselines.md" : "export failed");
    printf("  %sread%s    compare evolved winners by flags first, then deep score\n", c_dim(), c_reset());

    free(scratch);
    return wrote ? 0 : 1;
}

static int write_champions_report(const ChampionRecord *records, uint32_t count, uint32_t rescored, uint64_t fingerprint) {
    ensure_out_dir();
    FILE *md = fopen("out/champions.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/champions.md\n");
        return 0;
    }
    fprintf(md, "# hash-forge champions\n\n");
    fprintf(md, "- Champion records: `%u`\n", count);
    fprintf(md, "- Rescored records: `%u`\n", rescored);
    fprintf(md, "- Current scoring fingerprint: `0x%llx`\n\n", (unsigned long long)fingerprint);
    fprintf(md, "| rank | id | deep | quick | flags | audit worst | audit avg | audit flags | source | report |\n");
    fprintf(md, "|---:|---|---:|---:|---|---:|---:|---|---|---|\n");
    for (uint32_t i = 0; i < count; i++) {
        fprintf(md, "| %u | `%llx` | %lld | %lld | `0x%x` | %lld | %lld | `0x%x` | %s | `%s` |\n",
                i + 1,
                (unsigned long long)records[i].candidate.id,
                (long long)records[i].candidate.deep_score,
                (long long)records[i].candidate.quick_score,
                records[i].candidate.fail_flags,
                (long long)records[i].audit_worst,
                (long long)records[i].audit_average,
                records[i].audit_flags,
                candidate_source_name(records[i].candidate.source),
                records[i].source_report);
    }
    fclose(md);
    return 1;
}

int command_champions(void) {
    ChampionRecord records[MAX_CHAMPIONS];
    uint32_t rescored = 0;
    uint64_t fingerprint = current_scoring_fingerprint();
    uint32_t count = load_champion_records(records, MAX_CHAMPIONS, fingerprint, &rescored);
    int wrote = write_champions_report(records, count, rescored, fingerprint);

    printf("\n%s%sHash Forge champions%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %srecords%s      %u\n", c_dim(), c_reset(), count);
    printf("  %srescored%s     %u\n", c_dim(), c_reset(), rescored);
    printf("  %sfingerprint%s  0x%016llx\n\n", c_dim(), c_reset(), (unsigned long long)fingerprint);
    printf("%s%4s  %-16s  %12s  %12s  %8s  %12s  %s%s\n",
           c_dim(), "rank", "id", "deep", "quick", "flags", "audit", "report", c_reset());
    for (uint32_t i = 0; i < count && i < 20; i++) {
        printf("%s%4u%s  %s%016llx%s  %12lld  %12lld  0x%06x  %12lld  %s\n",
               c_bold(), i + 1, c_reset(),
               c_cyan(), (unsigned long long)records[i].candidate.id, c_reset(),
               (long long)records[i].candidate.deep_score,
               (long long)records[i].candidate.quick_score,
               records[i].candidate.fail_flags,
               (long long)records[i].audit_worst,
               records[i].source_report);
    }
    printf("\n%s%sChampions complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? "out/champions.md" : "export failed");
    return wrote ? 0 : 1;
}

static int parse_history_row(const char *line, HistoryRow *row) {
    unsigned threads = 0;
    unsigned candidate_generation = 0;
    unsigned instruction_count = 0;
    unsigned flags = 0;
    unsigned long long seed = 0;
    unsigned long long run_generation = 0;
    unsigned long long best_id = 0;
    unsigned long long total_candidates = 0;
    long long quick_score = 0;
    long long deep_score = 0;
    unsigned starter_candidates = STARTER_COUNT;
    unsigned refresh_enabled = 1;
    unsigned crossover_children = CROSSOVER_COUNT;

    int parsed = sscanf(line,
                        "%lld,%llu,%llu,%lf,%63[^,],%15[^,],%u,%llx,%u,%u,%lld,%lld,0x%x,%llu,%u,%u,%u",
                        &row->unix_time,
                        &seed,
                        &run_generation,
                        &row->elapsed_seconds,
                        row->stop_reason,
                        row->quality,
                        &threads,
                        &best_id,
                        &candidate_generation,
                        &instruction_count,
                        &quick_score,
                        &deep_score,
                        &flags,
                        &total_candidates,
                        &starter_candidates,
                        &refresh_enabled,
                        &crossover_children);
    if (parsed != 14 && parsed != 16 && parsed != 17) return 0;

    row->seed = (uint64_t)seed;
    row->run_generation = (uint64_t)run_generation;
    row->threads = (uint32_t)threads;
    row->best_id = (uint64_t)best_id;
    row->candidate_generation = (uint32_t)candidate_generation;
    row->instruction_count = (uint32_t)instruction_count;
    row->quick_score = (int64_t)quick_score;
    row->deep_score = (int64_t)deep_score;
    row->flags = (uint32_t)flags;
    row->total_candidates = (uint64_t)total_candidates;
    row->starter_candidates = (uint32_t)starter_candidates;
    row->refresh_enabled = refresh_enabled ? 1u : 0u;
    row->crossover_children = (uint32_t)crossover_children;
    return 1;
}

static int compare_history_rows(const void *a_ptr, const void *b_ptr) {
    const HistoryRow *a = (const HistoryRow *)a_ptr;
    const HistoryRow *b = (const HistoryRow *)b_ptr;
    uint32_t a_severity = fail_severity(a->flags);
    uint32_t b_severity = fail_severity(b->flags);
    if (a_severity != b_severity) return a_severity < b_severity ? -1 : 1;
    if (a->deep_score != b->deep_score) return a->deep_score > b->deep_score ? -1 : 1;
    if (a->quick_score != b->quick_score) return a->quick_score > b->quick_score ? -1 : 1;
    if (a->total_candidates != b->total_candidates) return a->total_candidates > b->total_candidates ? -1 : 1;
    if (a->unix_time != b->unix_time) return a->unix_time > b->unix_time ? -1 : 1;
    return 0;
}

static void print_history_row(FILE *out, uint32_t rank, const HistoryRow *row, int markdown) {
    if (markdown) {
        fprintf(out, "| %u | %s | %u | %u | %u | %u | %llu | %llu | %lld | %lld | `0x%x` | ",
                rank,
                row->quality,
                row->threads,
                row->starter_candidates,
                row->refresh_enabled,
                row->crossover_children,
                (unsigned long long)row->run_generation,
                (unsigned long long)row->total_candidates,
                (long long)row->deep_score,
                (long long)row->quick_score,
                row->flags);
        print_fail_flags(out, row->flags);
        fprintf(out, " | `%llx` | %llu | %.3f |\n",
                (unsigned long long)row->best_id,
                (unsigned long long)row->seed,
                row->elapsed_seconds);
    } else {
        printf("%s%4u%s  %-6s  %3u  %5u  %3u  %5u  %7llu  %12lld  %12lld  0x%02x  %s%016llx%s  %8llu  %7.3fs\n",
               c_bold(), rank, c_reset(),
               row->quality,
               row->threads,
               row->starter_candidates,
               row->refresh_enabled,
               row->crossover_children,
               (unsigned long long)row->run_generation,
               (long long)row->deep_score,
               (long long)row->quick_score,
               row->flags,
               c_cyan(), (unsigned long long)row->best_id, c_reset(),
               (unsigned long long)row->seed,
               row->elapsed_seconds);
    }
}

static int write_history_report(const HistoryRow *rows, uint32_t row_count, uint32_t top) {
    ensure_out_dir();
    FILE *md = fopen("out/history.md", "wb");
    if (!md) {
        fprintf(stderr, "failed to open out/history.md\n");
        return 0;
    }

    uint32_t limit = row_count < top ? row_count : top;
    fprintf(md, "# hash-forge history\n\n");
    fprintf(md, "- Rows read: `%u`\n", row_count);
    fprintf(md, "- Top rows shown: `%u`\n\n", limit);
    fprintf(md, "| rank | quality | threads | starters | refresh | crossover | generations | total candidates | deep | quick | flags | flag names | best id | seed | elapsed |\n");
    fprintf(md, "|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---:|---:|\n");
    for (uint32_t i = 0; i < limit; i++) {
        print_history_row(md, i + 1, &rows[i], 1);
    }
    fclose(md);
    return 1;
}

int command_history(const HistoryOptions *options) {
    FILE *file = fopen("out/history.csv", "rb");
    if (!file) {
        fprintf(stderr, "no history found at out/history.csv; run evolution first\n");
        return 2;
    }

    HistoryRow *rows = (HistoryRow *)calloc(MAX_HISTORY_ROWS, sizeof(*rows));
    if (!rows) {
        fclose(file);
        fprintf(stderr, "failed to allocate history rows\n");
        return 1;
    }

    char line[1024];
    uint32_t row_count = 0;
    int first_line = 1;
    while (fgets(line, sizeof(line), file)) {
        if (first_line) {
            first_line = 0;
            continue;
        }
        if (row_count >= MAX_HISTORY_ROWS) break;
        if (parse_history_row(line, &rows[row_count])) {
            row_count++;
        }
    }
    fclose(file);

    if (row_count == 0) {
        free(rows);
        fprintf(stderr, "history has no run rows\n");
        return 1;
    }

    qsort(rows, row_count, sizeof(rows[0]), compare_history_rows);
    uint32_t limit = row_count < options->top ? row_count : options->top;

    printf("\n%s%sHash Forge history top runs%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %srows%s       %u\n", c_dim(), c_reset(), row_count);
    printf("  %sshowing%s    %u\n", c_dim(), c_reset(), limit);
    printf("\n%s%4s  %-6s  %3s  %5s  %3s  %5s  %7s  %12s  %12s  %5s  %16s  %8s  %8s%s\n",
           c_dim(), "rank", "qual", "thr", "start", "ref", "cross", "gens", "deep", "quick", "flags", "best id", "seed", "elapsed", c_reset());
    for (uint32_t i = 0; i < limit; i++) {
        print_history_row(stdout, i + 1, &rows[i], 0);
    }

    int wrote = write_history_report(rows, row_count, options->top);
    printf("\n%s%sHistory complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? "out/history.md" : "export failed");

    free(rows);
    return wrote ? 0 : 1;
}

