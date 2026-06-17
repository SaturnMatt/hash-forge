#include "hf_core.h"

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

int ensure_out_dir(void) {
    if (HF_MKDIR("out") != 0) {
        /* Existing directory is fine; file creation below will catch real errors. */
    }
    return 1;
}

static int ensure_out_runs_dir(void) {
    ensure_out_dir();
    if (HF_MKDIR("out/runs") != 0) {
        /* Existing directory is fine; file creation below will catch real errors. */
    }
    return 1;
}

static int ensure_champions_dir(void) {
    ensure_out_dir();
    if (HF_MKDIR("out/champions") != 0) {
        /* Existing directory is fine; file creation below will catch real errors. */
    }
    return 1;
}

static int copy_file_bytes(const char *from_path, const char *to_path) {
    FILE *from = fopen(from_path, "rb");
    if (!from) return 0;
    FILE *to = fopen(to_path, "wb");
    if (!to) {
        fclose(from);
        return 0;
    }
    char buffer[8192];
    size_t got = 0;
    int ok = 1;
    while ((got = fread(buffer, 1, sizeof(buffer), from)) > 0) {
        if (fwrite(buffer, 1, got, to) != got) {
            ok = 0;
            break;
        }
    }
    if (ferror(from)) ok = 0;
    fclose(from);
    fclose(to);
    return ok;
}

static int parse_champion_instruction(const char *text, Instruction *ins) {
    unsigned op = 0, dst = 0, kind = 0, reg = 0, shift = 0;
    unsigned long long constant = 0;
    if (sscanf(text, "%u,%u,%u,%u,%u,%llx", &op, &dst, &kind, &reg, &shift, &constant) != 6) {
        return 0;
    }
    if (op >= OP_COUNT || dst >= REG_COUNT || kind > OPERAND_CONST || reg >= REG_COUNT || shift >= 64) {
        return 0;
    }
    ins->op = (uint8_t)op;
    ins->dst = (uint8_t)dst;
    ins->operand_kind = (uint8_t)kind;
    ins->operand_reg = (uint8_t)reg;
    ins->shift = (uint8_t)shift;
    ins->constant = (uint64_t)constant;
    return 1;
}

static int read_champion_record(const char *path, ChampionRecord *record) {
    FILE *file = fopen(path, "rb");
    char line[512];
    uint32_t seen_instructions = 0;
    memset(record, 0, sizeof(*record));
    record->candidate.deep_score = INT64_MIN;
    record->candidate.source = SOURCE_CHAMPION;
    strncpy(record->path, path, sizeof(record->path) - 1);
    if (!file) return 0;

    while (fgets(line, sizeof(line), file)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (strncmp(line, "id=", 3) == 0) {
            record->candidate.id = (uint64_t)strtoull(line + 3, NULL, 0);
        } else if (strncmp(line, "parent_id=", 10) == 0) {
            record->candidate.parent_id = (uint64_t)strtoull(line + 10, NULL, 0);
        } else if (strncmp(line, "generation=", 11) == 0) {
            record->candidate.generation = (uint32_t)strtoul(line + 11, NULL, 0);
        } else if (strncmp(line, "instruction_count=", 18) == 0) {
            record->candidate.instruction_count = (uint32_t)strtoul(line + 18, NULL, 0);
        } else if (strncmp(line, "quick_score=", 12) == 0) {
            record->candidate.quick_score = (int64_t)_strtoi64(line + 12, NULL, 0);
        } else if (strncmp(line, "deep_score=", 11) == 0) {
            record->candidate.deep_score = (int64_t)_strtoi64(line + 11, NULL, 0);
        } else if (strncmp(line, "fail_flags=", 11) == 0) {
            record->candidate.fail_flags = (uint32_t)strtoul(line + 11, NULL, 0);
        } else if (strncmp(line, "seed=", 5) == 0) {
            record->seed = (uint64_t)strtoull(line + 5, NULL, 0);
        } else if (strncmp(line, "timestamp=", 10) == 0) {
            record->unix_time = _strtoi64(line + 10, NULL, 0);
        } else if (strncmp(line, "quality=", 8) == 0) {
            strncpy(record->quality, line + 8, sizeof(record->quality) - 1);
        } else if (strncmp(line, "source_report=", 14) == 0) {
            strncpy(record->source_report, line + 14, sizeof(record->source_report) - 1);
        } else if (strncmp(line, "fingerprint=", 12) == 0) {
            record->fingerprint = (uint64_t)strtoull(line + 12, NULL, 0);
        } else if (strncmp(line, "audit_worst=", 12) == 0) {
            record->audit_worst = (int64_t)_strtoi64(line + 12, NULL, 0);
        } else if (strncmp(line, "audit_average=", 14) == 0) {
            record->audit_average = (int64_t)_strtoi64(line + 14, NULL, 0);
        } else if (strncmp(line, "audit_flags=", 12) == 0) {
            record->audit_flags = (uint32_t)strtoul(line + 12, NULL, 0);
        } else if (strncmp(line, "ins=", 4) == 0) {
            if (seen_instructions >= MAX_INSTRUCTIONS ||
                !parse_champion_instruction(line + 4, &record->candidate.instructions[seen_instructions])) {
                record->malformed = 1;
            } else {
                seen_instructions++;
            }
        }
    }
    fclose(file);

    if (record->candidate.instruction_count == 0) {
        record->candidate.instruction_count = seen_instructions;
    }
    if (record->candidate.instruction_count != seen_instructions ||
        record->candidate.instruction_count > MAX_INSTRUCTIONS ||
        record->candidate.id != candidate_id(&record->candidate)) {
        record->malformed = 1;
    }
    return !record->malformed;
}

static void audit_candidate_for_record(const Candidate *candidate, uint64_t seed, QualityMode quality,
                                       int64_t *worst, int64_t *average, uint32_t *flags) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    const uint32_t audit_count = 5;
    int64_t total = 0;
    *worst = INT64_MAX;
    *average = 0;
    *flags = 0;
    if (!scratch) {
        *worst = 0;
        return;
    }
    for (uint32_t i = 0; i < audit_count; i++) {
        uint64_t audit_seed = mix_seed(seed, candidate->id, 1000u + i);
        ScoreResult audit = score_candidate(candidate, scratch, audit_seed, 1, quality);
        if (audit.score < *worst) *worst = audit.score;
        total += audit.score;
        *flags |= audit.fail_flags;
    }
    *average = total / (int64_t)audit_count;
    free(scratch);
}

static int write_champion_record(const ChampionRecord *record) {
    FILE *file = fopen(record->path, "wb");
    if (!file) return 0;
    fprintf(file, "version=1\n");
    fprintf(file, "id=%llu\n", (unsigned long long)record->candidate.id);
    fprintf(file, "parent_id=%llu\n", (unsigned long long)record->candidate.parent_id);
    fprintf(file, "generation=%u\n", record->candidate.generation);
    fprintf(file, "instruction_count=%u\n", record->candidate.instruction_count);
    fprintf(file, "quick_score=%lld\n", (long long)record->candidate.quick_score);
    fprintf(file, "deep_score=%lld\n", (long long)record->candidate.deep_score);
    fprintf(file, "fail_flags=0x%x\n", record->candidate.fail_flags);
    fprintf(file, "seed=%llu\n", (unsigned long long)record->seed);
    fprintf(file, "timestamp=%lld\n", record->unix_time);
    fprintf(file, "quality=%s\n", record->quality[0] ? record->quality : "deep");
    fprintf(file, "source_report=%s\n", record->source_report);
    fprintf(file, "fingerprint=0x%llx\n", (unsigned long long)record->fingerprint);
    fprintf(file, "audit_worst=%lld\n", (long long)record->audit_worst);
    fprintf(file, "audit_average=%lld\n", (long long)record->audit_average);
    fprintf(file, "audit_flags=0x%x\n", record->audit_flags);
    fprintf(file, "source=%s\n", candidate_source_name(record->candidate.source));
    for (uint32_t i = 0; i < record->candidate.instruction_count; i++) {
        const Instruction *ins = &record->candidate.instructions[i];
        fprintf(file, "ins=%u,%u,%u,%u,%u,%llx\n",
                ins->op, ins->dst, ins->operand_kind, ins->operand_reg,
                ins->shift, (unsigned long long)ins->constant);
    }
    fclose(file);
    return 1;
}

static int compare_champion_records(const void *a_ptr, const void *b_ptr) {
    const ChampionRecord *a = (const ChampionRecord *)a_ptr;
    const ChampionRecord *b = (const ChampionRecord *)b_ptr;
    return compare_candidates(&a->candidate, &b->candidate);
}

static int rescore_champion_record(ChampionRecord *record, uint64_t fingerprint) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) return 0;
    ScoreResult quick = score_candidate(&record->candidate, scratch, record->seed, 0, QUALITY_DEEP);
    ScoreResult deep = score_candidate(&record->candidate, scratch, record->seed, 1, QUALITY_DEEP);
    record->candidate.quick_score = quick.score;
    record->candidate.deep_score = deep.score;
    record->candidate.fail_flags = deep.fail_flags;
    record->fingerprint = fingerprint;
    audit_candidate_for_record(&record->candidate, record->seed, QUALITY_DEEP,
                               &record->audit_worst, &record->audit_average, &record->audit_flags);
    free(scratch);
    return write_champion_record(record);
}

uint32_t load_champion_records(ChampionRecord *records, uint32_t capacity, uint64_t fingerprint, uint32_t *rescored) {
    uint32_t count = 0;
    if (rescored) *rescored = 0;
    ensure_champions_dir();
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA("out/champions/*.hfch", &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        char path[260];
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        snprintf(path, sizeof(path), "out/champions/%s", data.cFileName);
        ChampionRecord record;
        if (!read_champion_record(path, &record)) continue;
        if (record.fingerprint != fingerprint) {
            if (rescore_champion_record(&record, fingerprint) && rescored) {
                (*rescored)++;
            }
        }
        if (count < capacity) {
            records[count++] = record;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    (void)records;
    (void)capacity;
    (void)fingerprint;
#endif
    qsort(records, count, sizeof(records[0]), compare_champion_records);
    return count;
}

uint32_t load_champion_starters(Candidate *population, Rng *rng, uint32_t start_index) {
    ChampionRecord records[MAX_CHAMPIONS];
    uint64_t fingerprint = current_scoring_fingerprint();
    uint32_t rescored = 0;
    uint32_t count = load_champion_records(records, MAX_CHAMPIONS, fingerprint, &rescored);
    uint32_t loaded = 0;
    (void)rescored;
    for (uint32_t i = 0; i < count && loaded < MAX_CHAMPION_STARTERS; i++) {
        if (records[i].candidate.fail_flags != 0 || records[i].candidate.deep_score == INT64_MIN) continue;
        uint32_t at = start_index + loaded;
        if (at >= POPULATION_SIZE) break;
        population[at] = records[i].candidate;
        population[at].source = SOURCE_CHAMPION;
        population[at].deep_score = INT64_MIN;
        ensure_unique_candidate(&population[at], population, at, rng);
        loaded++;
    }
    return loaded;
}

static int save_champion_candidate(const Candidate *candidate, const RunOptions *options, const RunReport *report, const char *source_report) {
    ChampionRecord record;
    if (candidate->fail_flags != 0 || candidate->deep_score == INT64_MIN || candidate->deep_score <= 0) {
        return 0;
    }
    ensure_champions_dir();
    memset(&record, 0, sizeof(record));
    record.candidate = *candidate;
    record.seed = options->seed;
    record.unix_time = (long long)time(NULL);
    strncpy(record.quality, quality_name(options->quality), sizeof(record.quality) - 1);
    if (source_report) strncpy(record.source_report, source_report, sizeof(record.source_report) - 1);
    record.fingerprint = current_scoring_fingerprint();
    audit_candidate_for_record(candidate, options->seed, options->quality,
                               &record.audit_worst, &record.audit_average, &record.audit_flags);
    snprintf(record.path, sizeof(record.path), "out/champions/%016llx.hfch", (unsigned long long)candidate->id);
    (void)report;
    return write_champion_record(&record);
}

void print_fail_flags(FILE *out, uint32_t flags) {
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
    if (flags & FAIL_DIFFERENTIAL) { fprintf(out, "%sDIFFERENTIAL", wrote ? ", " : ""); wrote = 1; }
    if (flags & FAIL_SENSITIVITY) { fprintf(out, "%sSENSITIVITY", wrote ? ", " : ""); wrote = 1; }
}


static void count_ops(const Candidate *candidate, uint32_t counts[OP_COUNT]) {
    memset(counts, 0, OP_COUNT * sizeof(counts[0]));
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (candidate->instructions[i].op < OP_COUNT) {
            counts[candidate->instructions[i].op]++;
        }
    }
}

static int write_improvements_csv(const RunReport *report, const char *path) {
    FILE *csv = fopen(path, "wb");
    if (!csv) {
        fprintf(stderr, "failed to open %s\n", path);
        return 0;
    }

    fprintf(csv, "index,run_generation,elapsed_seconds,candidate_id,parent_id,candidate_generation,source,instruction_count,quick_score,deep_score,deep_known,fail_flags,flag_names,total_candidates,reason\n");
    for (uint32_t i = 0; i < report->improvements.count; i++) {
        const struct ImprovementEvent *event = &report->improvements.events[i];
        fprintf(csv, "%u,%llu,%.3f,%llx,%llx,%u,%s,%u,%lld,",
                i + 1u,
                (unsigned long long)event->run_generation,
                event->elapsed_seconds,
                (unsigned long long)event->candidate_id,
                (unsigned long long)event->parent_id,
                event->candidate_generation,
                candidate_source_name(event->source),
                event->instruction_count,
                (long long)event->quick_score);
        if (event->deep_known) fprintf(csv, "%lld", (long long)event->deep_score);
        else fprintf(csv, "pending");
        fprintf(csv, ",%u,0x%x,\"", event->deep_known, event->fail_flags);
        print_fail_flags(csv, event->fail_flags);
        fprintf(csv, "\",%llu,%s\n",
                (unsigned long long)event->total_candidates,
                improvement_reason_name(event->reason));
    }
    fclose(csv);
    return 1;
}

static int write_markdown_report_to_path(const Candidate *candidate, const RunOptions *options, const RunReport *report, const char *path) {
    FILE *md = fopen(path, "wb");
    if (!md) {
        fprintf(stderr, "failed to open %s\n", path);
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
    fprintf(md, "- Scoring threads: `%u`\n", report->threads);
    fprintf(md, "- Crossover enabled: `%s`\n", report->crossover_children_per_generation ? "yes" : "no");
    fprintf(md, "- Champion starters loaded: `%u`\n\n", report->champion_starters_loaded);

    fprintf(md, "## Evaluation totals\n\n");
    fprintf(md, "- Quick candidates evaluated: `%llu`\n", (unsigned long long)report->quick_candidates_evaluated);
    fprintf(md, "- Deep candidates evaluated: `%llu`\n", (unsigned long long)report->deep_candidates_evaluated);
    fprintf(md, "- Total hash functions evaluated: `%llu`\n\n",
            (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));

    fprintf(md, "## Improvement timeline\n\n");
    const struct ImprovementEvent *last_improvement = last_improvement_event(&report->improvements);
    fprintf(md, "- Improvement events: `%llu`\n", (unsigned long long)report->improvements.total_count);
    fprintf(md, "- Improvement events kept in report: `%u`\n", report->improvements.count);
    fprintf(md, "- Omitted middle improvement events: `%llu`\n", (unsigned long long)report->improvements.omitted_count);
    if (last_improvement) {
        fprintf(md, "- Last improvement generation: `%llu`\n", (unsigned long long)last_improvement->run_generation);
        fprintf(md, "- Last improvement elapsed seconds: `%.3f`\n\n", last_improvement->elapsed_seconds);
    } else {
        fprintf(md, "- Last improvement generation: `0`\n");
        fprintf(md, "- Last improvement elapsed seconds: `0.000`\n\n");
    }
    fprintf(md, "| # | gen | elapsed | reason | id | source | quick | deep | flags | total candidates |\n");
    fprintf(md, "|---:|---:|---:|---|---|---|---:|---:|---|---:|\n");
    for (uint32_t i = 0; i < report->improvements.count; i++) {
        const struct ImprovementEvent *event = &report->improvements.events[i];
        fprintf(md, "| %u | %llu | %.3f | %s | `%llx` | %s | %lld | ",
                i + 1u,
                (unsigned long long)event->run_generation,
                event->elapsed_seconds,
                improvement_reason_name(event->reason),
                (unsigned long long)event->candidate_id,
                candidate_source_name(event->source),
                (long long)event->quick_score);
        if (event->deep_known) fprintf(md, "%lld", (long long)event->deep_score);
        else fprintf(md, "pending");
        fprintf(md, " | `0x%x` (", event->fail_flags);
        print_fail_flags(md, event->fail_flags);
        fprintf(md, ") | %llu |\n", (unsigned long long)event->total_candidates);
    }
    fprintf(md, "\n");
    fprintf(md, "Early final improvement relative to total run generations is a plateau signal; ");
    fprintf(md, "late improvement means the extra run budget was still finding better candidates.\n\n");

    fprintf(md, "## Diversity telemetry\n\n");
    fprintf(md, "- Unique candidates in last scored generation: `%u` of `%u`\n",
            report->last_unique_candidates, POPULATION_SIZE);
    fprintf(md, "- Duplicate candidate repairs: `%llu`\n",
            (unsigned long long)report->duplicate_repairs);
    fprintf(md, "- Fresh random duplicate replacements: `%llu`\n",
            (unsigned long long)report->duplicate_random_replacements);
    fprintf(md, "- Stagnation refreshes: `%llu`\n",
            (unsigned long long)report->stagnation_refreshes);
    fprintf(md, "- Extra adaptive random immigrants: `%llu`\n\n",
            (unsigned long long)report->adaptive_random_immigrants);

    fprintf(md, "## Novelty telemetry\n\n");
    fprintf(md, "- Novelty children per generation: `%u`\n", report->novelty_lane);
    fprintf(md, "- Novelty candidates admitted: `%llu`\n", (unsigned long long)report->novelty_candidates_admitted);
    fprintf(md, "- Last-generation best novelty score: `%llu`\n", (unsigned long long)report->last_best_novelty_score);
    fprintf(md, "- Last-generation average novelty score: `%llu`\n\n", (unsigned long long)report->last_avg_novelty_score);

    fprintf(md, "## Starter cap telemetry\n\n");
    fprintf(md, "- Starter cap enabled: `%s`\n", report->starter_cap_enabled ? "yes" : "no");
    fprintf(md, "- Starter survivor cap: `%u`\n", report->starter_cap);
    fprintf(md, "- Starter cap after generation: `%llu`\n", (unsigned long long)report->starter_cap_after);
    fprintf(md, "- Starter cap displacements: `%llu`\n", (unsigned long long)report->starter_cap_displacements);
    fprintf(md, "- Last starter survivors before cap: `%u`\n", report->last_starter_survivors_before_cap);
    fprintf(md, "- Last starter survivors after cap: `%u`\n", report->last_starter_survivors_after_cap);
    fprintf(md, "- Final winner starter-lineage: `%s`\n\n", report->final_winner_starter ? "yes" : "no");

    fprintf(md, "## Population settings\n\n");
    fprintf(md, "- Population size: `%u`\n", POPULATION_SIZE);
    fprintf(md, "- Survivor count: `%u`\n", SURVIVOR_COUNT);
    fprintf(md, "- Novelty children per generation: `%u`\n", report->novelty_lane);
    fprintf(md, "- Crossover children per generation: `%u`\n", report->crossover_children_per_generation);
    fprintf(md, "- Random immigrants per generation: `%u`\n", IMMIGRANT_COUNT);
    fprintf(md, "- Compact starter candidates: `%u`\n", options->no_starter ? 0u : STARTER_COUNT);
    fprintf(md, "- Champion starter candidates: `%u`\n", report->champion_starters_loaded);
    fprintf(md, "- Stagnation refresh window: `%llu` generations\n", (unsigned long long)report->refresh_window);
    fprintf(md, "- Stagnation refresh immigrant count: `%u`\n", report->refresh_window ? report->refresh_immigrants : 0u);
    fprintf(md, "- Scoring threads: `%u`\n", report->threads);
    fprintf(md, "- Deep score cadence: every `%u` generations\n", deep_every_for_quality(options->quality));
    fprintf(md, "- Deep score top N: `%u`\n", deep_top_n_for_quality(options->quality));
    fprintf(md, "- Instruction count range: `%u..%u`\n\n", MIN_PROGRAM_LEN, MAX_PROGRAM_LEN);

    fprintf(md, "## Best trackers\n\n");
    fprintf(md, "- Export selection: `%s`\n", export_selection_name(report->export_selection));
    fprintf(md, "- Quick tracker ID: `%llx`\n", (unsigned long long)report->best_quick_seen.id);
    fprintf(md, "- Quick tracker source: `%s`\n", candidate_source_name(report->best_quick_seen.source));
    fprintf(md, "- Quick tracker score: quick `%lld`, deep `%lld`, flags `0x%x` (",
            (long long)report->best_quick_seen.quick_score,
            (long long)report->best_quick_seen.deep_score,
            report->best_quick_seen.fail_flags);
    print_fail_flags(md, report->best_quick_seen.fail_flags);
    fprintf(md, ")\n");
    if (report->has_best_deep_seen) {
        fprintf(md, "- Deep-seen tracker ID: `%llx`\n", (unsigned long long)report->best_deep_seen.id);
        fprintf(md, "- Deep-seen tracker source: `%s`\n", candidate_source_name(report->best_deep_seen.source));
        fprintf(md, "- Deep-seen tracker score: quick `%lld`, deep `%lld`, flags `0x%x` (",
                (long long)report->best_deep_seen.quick_score,
                (long long)report->best_deep_seen.deep_score,
                report->best_deep_seen.fail_flags);
        print_fail_flags(md, report->best_deep_seen.fail_flags);
        fprintf(md, ")\n\n");
    } else {
        fprintf(md, "- Deep-seen tracker: `none`\n\n");
    }

    fprintf(md, "## Best candidate\n\n");
    fprintf(md, "- ID: `%llx`\n", (unsigned long long)candidate->id);
    fprintf(md, "- Parent ID: `%llx`\n", (unsigned long long)candidate->parent_id);
    fprintf(md, "- Candidate generation: `%u`\n", candidate->generation);
    fprintf(md, "- Instruction count: `%u`\n", candidate->instruction_count);
    fprintf(md, "- Source ancestry: `%s`\n", candidate_source_name(candidate->source));
    fprintf(md, "- Quick score: `%lld`\n", (long long)candidate->quick_score);
    fprintf(md, "- Final deep score: `%lld`\n", (long long)candidate->deep_score);
    fprintf(md, "- Fail flags: `0x%x` (", candidate->fail_flags);
    print_fail_flags(md, candidate->fail_flags);
    fprintf(md, ")\n\n");

    fprintf(md, "## Score breakdown\n\n");
    ScoreScratch *breakdown_scratch = (ScoreScratch *)calloc(1, sizeof(*breakdown_scratch));
    if (breakdown_scratch) {
        ScoreResult quick = score_candidate(candidate, breakdown_scratch, options->seed, 0, options->quality);
        ScoreResult deep = score_candidate(candidate, breakdown_scratch, options->seed, 1, options->quality);
        fprintf(md, "| component | quick | deep |\n");
        fprintf(md, "|---|---:|---:|\n");
        fprintf(md, "| zero/trivial | %lld | %lld |\n", (long long)quick.zero_score, (long long)deep.zero_score);
        fprintf(md, "| collisions | %lld | %lld |\n", (long long)quick.collision_score, (long long)deep.collision_score);
        fprintf(md, "| buckets | %lld | %lld |\n", (long long)quick.bucket_score, (long long)deep.bucket_score);
        fprintf(md, "| avalanche | %lld | %lld |\n", (long long)quick.avalanche_score, (long long)deep.avalanche_score);
        fprintf(md, "| differentials | %lld | %lld |\n", (long long)quick.differential_score, (long long)deep.differential_score);
        fprintf(md, "| sensitivity | %lld | %lld |\n", (long long)quick.sensitivity_score, (long long)deep.sensitivity_score);
        fprintf(md, "| size penalty | %lld | %lld |\n", (long long)quick.size_penalty, (long long)deep.size_penalty);
        fprintf(md, "| total | %lld | %lld |\n\n", (long long)quick.score, (long long)deep.score);
        free(breakdown_scratch);
    } else {
        fprintf(md, "Skipped: failed to allocate score scratch space.\n\n");
    }

    fprintf(md, "## Final multi-seed audit\n\n");
    ScoreScratch *audit_scratch = (ScoreScratch *)calloc(1, sizeof(*audit_scratch));
    if (audit_scratch) {
        const uint32_t audit_count = 5;
        int64_t min_score = INT64_MAX;
        int64_t max_score = INT64_MIN;
        int64_t total_score = 0;
        uint32_t combined_flags = 0;
        fprintf(md, "| audit seed | deep score | flags |\n");
        fprintf(md, "|---|---:|---|\n");
        for (uint32_t i = 0; i < audit_count; i++) {
            uint64_t audit_seed = mix_seed(options->seed, candidate->id, 1000u + i);
            ScoreResult audit = score_candidate(candidate, audit_scratch, audit_seed, 1, options->quality);
            if (audit.score < min_score) min_score = audit.score;
            if (audit.score > max_score) max_score = audit.score;
            total_score += audit.score;
            combined_flags |= audit.fail_flags;
            fprintf(md, "| `%llx` | %lld | `0x%x` (",
                    (unsigned long long)audit_seed,
                    (long long)audit.score,
                    audit.fail_flags);
            print_fail_flags(md, audit.fail_flags);
            fprintf(md, ") |\n");
        }
        fprintf(md, "\n");
        fprintf(md, "- Worst audit deep score: `%lld`\n", (long long)min_score);
        fprintf(md, "- Best audit deep score: `%lld`\n", (long long)max_score);
        fprintf(md, "- Average audit deep score: `%lld`\n", (long long)(total_score / (int64_t)audit_count));
        fprintf(md, "- Combined audit flags: `0x%x` (", combined_flags);
        print_fail_flags(md, combined_flags);
        fprintf(md, ")\n\n");
        free(audit_scratch);
    } else {
        fprintf(md, "Skipped: failed to allocate audit scratch space.\n\n");
    }

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
        fprintf(md, "\n");
        fprintf(md, "## Established hash baselines\n\n");
        fprintf(md, "| reference | deep score | flags | note |\n");
        fprintf(md, "|---|---:|---|---|\n");
        for (uint32_t i = 0; i < baseline_case_count; i++) {
            ScoreResult score = score_baseline_case(&baseline_cases[i], comparison_scratch, options->seed, 1, options->quality);
            fprintf(md, "| %s | %lld | `0x%x` (",
                    baseline_cases[i].name,
                    (long long)score.score,
                    score.fail_flags);
            print_fail_flags(md, score.fail_flags);
            fprintf(md, ") | %s |\n", baseline_cases[i].note);
        }
        fprintf(md, "\n");
        fprintf(md, "Run winners should be judged against these references by fail flags first, then deep score. ");
        fprintf(md, "Matching or beating a reference here is a lab signal, not proof of universal hash quality.\n");
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
    fprintf(md, "- `out/report.md`: latest full human-readable run report\n");
    fprintf(md, "- `out/improvements.csv`: latest improvement timeline as CSV\n");
    fprintf(md, "- `out/runs/*.md`: archived per-run reports\n");
    fprintf(md, "- `out/runs/*.c`: archived per-run standalone C exports\n");
    fprintf(md, "- `out/latest_report_path.txt`: path to the latest archived report\n");
    fprintf(md, "- `out/latest_export_path.txt`: path to the latest archived C export\n");
    fprintf(md, "- `out/history.csv`: compact append-only run history\n");
    fprintf(md, "- `out/compare.md`: latest deterministic policy comparison report\n");
    fprintf(md, "- `out/policy.md`: latest full policy comparison report\n");
    fprintf(md, "- `out/policy.csv`: latest full policy comparison CSV\n");
    fprintf(md, "- `out/baselines.md`: latest established-hash baseline report\n");
    fprintf(md, "- `out/champions/*.hfch`: saved custom champion records\n");
    fprintf(md, "- `out/champions.md`: latest champion leaderboard\n");
    fprintf(md, "- `out/summary.txt`: terse run summary\n\n");

    fprintf(md, "## Interpretation note\n\n");
    fprintf(md, "Scores and flags are exploratory quality signals for non-cryptographic hash search. ");
    fprintf(md, "They are not cryptographic proof and should be treated as candidates for further testing.\n");
    fclose(md);
    return 1;
}

int export_best(const Candidate *candidate, const RunOptions *options, const RunReport *report) {
    ensure_out_dir();
    Candidate exported_candidate;
    prune_candidate_for_export(candidate, &exported_candidate);
    const struct ImprovementEvent *last_improvement = last_improvement_event(&report->improvements);

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
    for (uint32_t i = 0; i < exported_candidate.instruction_count; i++) {
        print_instruction(c, &exported_candidate.instructions[i], 1);
    }
    fprintf(c, "\n    return hash;\n}\n");
    fprintf(c, "\n#ifdef HASH_FORGE_BEST_TEST_MAIN\n");
    fprintf(c, "#include <stdio.h>\n\n");
    fprintf(c, "typedef struct hf_test_vector {\n");
    fprintf(c, "    uint64_t key;\n");
    fprintf(c, "    uint64_t seed;\n");
    fprintf(c, "    uint64_t expected;\n");
    fprintf(c, "} hf_test_vector;\n\n");
    fprintf(c, "int main(void) {\n");
    fprintf(c, "    static const hf_test_vector vectors[] = {\n");
    uint64_t vector_keys[] = {
        0ull,
        1ull,
        0x0123456789abcdefull,
        0xffffffffffffffffull,
        0x9e3779b97f4a7c15ull,
        0x0000000100000000ull,
        0x8000000000000000ull,
        0x55aa55aa55aa55aaull
    };
    uint64_t vector_seeds[] = {
        0ull,
        1ull,
        0xfedcba9876543210ull,
        0x0123456789abcdefull,
        0xd1b54a32d192ed03ull,
        0x00000000ffffffffull,
        0x7fffffffffffffffull,
        0xaa55aa55aa55aa55ull
    };
    for (uint32_t i = 0; i < sizeof(vector_keys) / sizeof(vector_keys[0]); i++) {
        uint64_t expected = eval_candidate(candidate, vector_keys[i], vector_seeds[i]);
        fprintf(c, "        { UINT64_C(0x%llx), UINT64_C(0x%llx), UINT64_C(0x%llx) }%s\n",
                (unsigned long long)vector_keys[i],
                (unsigned long long)vector_seeds[i],
                (unsigned long long)expected,
                i + 1 == sizeof(vector_keys) / sizeof(vector_keys[0]) ? "" : ",");
    }
    fprintf(c, "    };\n");
    fprintf(c, "    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {\n");
    fprintf(c, "        uint64_t got = hash_forge_best(vectors[i].key, vectors[i].seed);\n");
    fprintf(c, "        if (got != vectors[i].expected) {\n");
    fprintf(c, "            printf(\"vector %%u failed: got 0x%%llx expected 0x%%llx\\n\", i, (unsigned long long)got, (unsigned long long)vectors[i].expected);\n");
    fprintf(c, "            return 1;\n");
    fprintf(c, "        }\n");
    fprintf(c, "    }\n");
    fprintf(c, "    puts(\"hash_forge_best vectors: pass\");\n");
    fprintf(c, "    return 0;\n");
    fprintf(c, "}\n");
    fprintf(c, "#endif\n");
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
    fprintf(txt, "best_quick_seen_id: %llu\n", (unsigned long long)report->best_quick_seen.id);
    fprintf(txt, "best_quick_seen_source: %s\n", candidate_source_name(report->best_quick_seen.source));
    fprintf(txt, "best_quick_seen_quick_score: %lld\n", (long long)report->best_quick_seen.quick_score);
    fprintf(txt, "best_quick_seen_deep_score: %lld\n", (long long)report->best_quick_seen.deep_score);
    fprintf(txt, "best_quick_seen_flags: 0x%x\n", report->best_quick_seen.fail_flags);
    fprintf(txt, "best_deep_seen_id: %llu\n",
            (unsigned long long)(report->has_best_deep_seen ? report->best_deep_seen.id : 0u));
    fprintf(txt, "best_deep_seen_source: %s\n",
            report->has_best_deep_seen ? candidate_source_name(report->best_deep_seen.source) : "none");
    fprintf(txt, "best_deep_seen_quick_score: %lld\n",
            (long long)(report->has_best_deep_seen ? report->best_deep_seen.quick_score : INT64_MIN));
    fprintf(txt, "best_deep_seen_deep_score: %lld\n",
            (long long)(report->has_best_deep_seen ? report->best_deep_seen.deep_score : INT64_MIN));
    fprintf(txt, "best_deep_seen_flags: 0x%x\n",
            report->has_best_deep_seen ? report->best_deep_seen.fail_flags : 0u);
    fprintf(txt, "export_selection: %s\n", export_selection_name(report->export_selection));
    fprintf(txt, "quick_candidates_evaluated: %llu\n", (unsigned long long)report->quick_candidates_evaluated);
    fprintf(txt, "deep_candidates_evaluated: %llu\n", (unsigned long long)report->deep_candidates_evaluated);
    fprintf(txt, "total_candidates_evaluated: %llu\n",
            (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated));
    fprintf(txt, "last_unique_candidates: %u\n", report->last_unique_candidates);
    fprintf(txt, "duplicate_repairs: %llu\n", (unsigned long long)report->duplicate_repairs);
    fprintf(txt, "duplicate_random_replacements: %llu\n", (unsigned long long)report->duplicate_random_replacements);
    fprintf(txt, "stagnation_refreshes: %llu\n", (unsigned long long)report->stagnation_refreshes);
    fprintf(txt, "adaptive_random_immigrants: %llu\n", (unsigned long long)report->adaptive_random_immigrants);
    fprintf(txt, "novelty_lane: %u\n", report->novelty_lane);
    fprintf(txt, "novelty_candidates_admitted: %llu\n", (unsigned long long)report->novelty_candidates_admitted);
    fprintf(txt, "last_best_novelty_score: %llu\n", (unsigned long long)report->last_best_novelty_score);
    fprintf(txt, "last_avg_novelty_score: %llu\n", (unsigned long long)report->last_avg_novelty_score);
    fprintf(txt, "starter_cap_enabled: %s\n", report->starter_cap_enabled ? "yes" : "no");
    fprintf(txt, "starter_cap: %u\n", report->starter_cap);
    fprintf(txt, "starter_cap_after: %llu\n", (unsigned long long)report->starter_cap_after);
    fprintf(txt, "starter_cap_displacements: %llu\n", (unsigned long long)report->starter_cap_displacements);
    fprintf(txt, "last_starter_survivors_before_cap: %u\n", report->last_starter_survivors_before_cap);
    fprintf(txt, "last_starter_survivors_after_cap: %u\n", report->last_starter_survivors_after_cap);
    fprintf(txt, "final_winner_starter: %s\n", report->final_winner_starter ? "yes" : "no");
    fprintf(txt, "improvement_count: %llu\n", (unsigned long long)report->improvements.total_count);
    fprintf(txt, "improvement_events_kept: %u\n", report->improvements.count);
    fprintf(txt, "improvement_events_omitted: %llu\n", (unsigned long long)report->improvements.omitted_count);
    fprintf(txt, "last_improvement_generation: %llu\n",
            (unsigned long long)(last_improvement ? last_improvement->run_generation : 0u));
    fprintf(txt, "last_improvement_elapsed: %.3f\n", last_improvement ? last_improvement->elapsed_seconds : 0.0);
    fprintf(txt, "starter_candidates: %u\n", options->no_starter ? 0u : STARTER_COUNT);
    fprintf(txt, "stagnation_refresh_enabled: %s\n", options->no_refresh ? "no" : "yes");
    fprintf(txt, "stagnation_refresh_window: %llu\n", (unsigned long long)report->refresh_window);
    fprintf(txt, "stagnation_refresh_immigrants: %u\n", report->refresh_window ? report->refresh_immigrants : 0u);
    fprintf(txt, "champion_starters: %u\n", report->champion_starters_loaded);
    fprintf(txt, "crossover_children: %u\n", report->crossover_children_per_generation);
    fprintf(txt, "source: %s\n", candidate_source_name(candidate->source));
    fprintf(txt, "threads: %u\n", report->threads);
    fprintf(txt, "exported_instruction_count: %u\n", exported_candidate.instruction_count);
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
        fprintf(summary, "id=%llu generation=%u run_generation=%llu quick=%lld deep=%lld flags=0x%x elapsed_seconds=%.3f stop_reason=%s quality=%s threads=%u quick_candidates=%llu deep_candidates=%llu total_candidates=%llu last_unique=%u duplicate_repairs=%llu duplicate_random_replacements=%llu stagnation_refreshes=%llu adaptive_random_immigrants=%llu novelty_lane=%u novelty_candidates=%llu last_best_novelty=%llu last_avg_novelty=%llu starter_cap_enabled=%s starter_cap=%u starter_cap_after=%llu starter_cap_displacements=%llu last_starter_before_cap=%u last_starter_after_cap=%u final_winner_starter=%s starter_candidates=%u refresh_enabled=%s refresh_window=%llu refresh_immigrants=%u champion_starters=%u source=%s crossover_children=%u improvement_count=%llu last_improvement_generation=%llu last_improvement_elapsed=%.3f best_quick_id=%llu best_deep_seen_id=%llu export_selection=%s\n",
                (unsigned long long)candidate->id, candidate->generation,
                (unsigned long long)report->run_generation,
                (long long)candidate->quick_score, (long long)candidate->deep_score,
                candidate->fail_flags, report->elapsed_seconds, report->stop_reason, quality_name(options->quality), report->threads,
                (unsigned long long)report->quick_candidates_evaluated,
                (unsigned long long)report->deep_candidates_evaluated,
                (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated),
                report->last_unique_candidates,
                (unsigned long long)report->duplicate_repairs,
                (unsigned long long)report->duplicate_random_replacements,
                (unsigned long long)report->stagnation_refreshes,
                (unsigned long long)report->adaptive_random_immigrants,
                report->novelty_lane,
                (unsigned long long)report->novelty_candidates_admitted,
                (unsigned long long)report->last_best_novelty_score,
                (unsigned long long)report->last_avg_novelty_score,
                report->starter_cap_enabled ? "yes" : "no",
                report->starter_cap,
                (unsigned long long)report->starter_cap_after,
                (unsigned long long)report->starter_cap_displacements,
                report->last_starter_survivors_before_cap,
                report->last_starter_survivors_after_cap,
                report->final_winner_starter ? "yes" : "no",
                options->no_starter ? 0u : STARTER_COUNT,
                options->no_refresh ? "no" : "yes",
                (unsigned long long)report->refresh_window,
                report->refresh_window ? report->refresh_immigrants : 0u,
                report->champion_starters_loaded,
                candidate_source_name(candidate->source),
                report->crossover_children_per_generation,
                (unsigned long long)report->improvements.total_count,
                (unsigned long long)(last_improvement ? last_improvement->run_generation : 0u),
                last_improvement ? last_improvement->elapsed_seconds : 0.0,
                (unsigned long long)report->best_quick_seen.id,
                (unsigned long long)(report->has_best_deep_seen ? report->best_deep_seen.id : 0u),
                export_selection_name(report->export_selection));
        fclose(summary);
    }

    int wrote_improvements = write_improvements_csv(report, "out/improvements.csv");

    int history_exists = 0;
    FILE *history_read = fopen("out/history.csv", "rb");
    if (history_read) {
        history_exists = 1;
        fclose(history_read);
    }
    FILE *history = fopen("out/history.csv", "ab");
    if (history) {
        if (!history_exists) {
            fprintf(history, "unix_time,seed,run_generation,elapsed_seconds,stop_reason,quality,threads,best_id,candidate_generation,instruction_count,quick_score,deep_score,flags,total_candidates,starter_candidates,refresh_enabled,crossover_children\n");
        }
        fprintf(history, "%lld,%llu,%llu,%.3f,%s,%s,%u,%llx,%u,%u,%lld,%lld,0x%x,%llu,%u,%u,%u\n",
                (long long)time(NULL),
                (unsigned long long)options->seed,
                (unsigned long long)report->run_generation,
                report->elapsed_seconds,
                report->stop_reason,
                quality_name(options->quality),
                report->threads,
                (unsigned long long)candidate->id,
                candidate->generation,
                candidate->instruction_count,
                (long long)candidate->quick_score,
                (long long)candidate->deep_score,
                candidate->fail_flags,
                (unsigned long long)(report->quick_candidates_evaluated + report->deep_candidates_evaluated),
                options->no_starter ? 0u : STARTER_COUNT,
                options->no_refresh ? 0u : 1u,
                report->crossover_children_per_generation);
        fclose(history);
    }

    ensure_out_runs_dir();
    char archive_stem[256];
    char archive_report_path[320];
    char archive_c_path[320];
    unsigned long long elapsed_ms = (unsigned long long)(report->elapsed_seconds * 1000.0 + 0.5);
    snprintf(archive_stem, sizeof(archive_stem),
             "out/runs/seed_%llu_gen_%llu_ms_%llu_threads_%u_id_%llx",
             (unsigned long long)options->seed,
             (unsigned long long)report->run_generation,
             elapsed_ms,
             report->threads,
             (unsigned long long)candidate->id);
    snprintf(archive_report_path, sizeof(archive_report_path), "%s.md", archive_stem);
    snprintf(archive_c_path, sizeof(archive_c_path), "%s.c", archive_stem);

    int wrote_latest = write_markdown_report_to_path(candidate, options, report, "out/report.md");
    int wrote_archive = write_markdown_report_to_path(candidate, options, report, archive_report_path);
    int copied_c_archive = copy_file_bytes("out/best.c", archive_c_path);
    if (wrote_latest && wrote_archive && copied_c_archive && wrote_improvements) {
        FILE *archive_note = fopen("out/latest_report_path.txt", "wb");
        if (archive_note) {
            fprintf(archive_note, "%s\n", archive_report_path);
            fclose(archive_note);
        }
        archive_note = fopen("out/latest_export_path.txt", "wb");
        if (archive_note) {
            fprintf(archive_note, "%s\n", archive_c_path);
            fclose(archive_note);
        }
        save_champion_candidate(candidate, options, report, archive_report_path);
    }
    return wrote_latest && wrote_archive && copied_c_archive && wrote_improvements;
}

void make_reasonable_baseline(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->source = SOURCE_STARTER;
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

void make_compact_starter(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->source = SOURCE_STARTER;
    candidate->instruction_count = 16;
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
        { OP_XOR, 2, OPERAND_CONST, 0, 1, 0xd1b54a32d192ed03ull }
    };
    memcpy(candidate->instructions, p, sizeof(p));
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void seed_starter_population(Candidate *population, Rng *rng) {
    Candidate starter;
    make_compact_starter(&starter);
    population[0] = starter;
    for (uint32_t i = 1; i < STARTER_COUNT && i < POPULATION_SIZE; i++) {
        mutate_candidate(&population[i], &starter, rng);
        ensure_unique_candidate(&population[i], population, i, rng);
    }
}

void make_constant_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_CONST, 0, 1, 1 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void make_key_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 0, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void make_seed_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 1;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 1, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void make_xor_only_bad(Candidate *candidate) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = 2;
    candidate->instructions[0] = (Instruction){ OP_MOV, 2, OPERAND_REG, 0, 1, 0 };
    candidate->instructions[1] = (Instruction){ OP_XOR, 2, OPERAND_REG, 1, 1, 0 };
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

void make_program(Candidate *candidate, const Instruction *instructions, uint32_t instruction_count) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->instruction_count = instruction_count;
    memcpy(candidate->instructions, instructions, instruction_count * sizeof(instructions[0]));
    candidate->id = candidate_id(candidate);
    candidate->deep_score = INT64_MIN;
}

int candidate_is_valid(const Candidate *candidate) {
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

