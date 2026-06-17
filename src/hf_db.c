#include "hf_core.h"

#define DB_PATH "out/hash-forge.db"
#define DB_TOP_PATH "out/db-top.md"
#define MAX_DB_RECORDS 4096

typedef struct DbRecord {
    Candidate candidate;
    uint64_t seed;
    long long unix_time;
    char quality[16];
    char source_report[260];
    char export_path[260];
    uint64_t fingerprint;
    int64_t audit_worst;
    int64_t audit_average;
    uint32_t audit_flags;
} DbRecord;

typedef struct DbStore {
    DbRecord records[MAX_DB_RECORDS];
    uint32_t count;
} DbStore;

static QualityMode db_quality_from_text(const char *text) {
    if (strcmp(text, "quick") == 0) return QUALITY_QUICK;
    if (strcmp(text, "deep") == 0) return QUALITY_DEEP;
    return QUALITY_NORMAL;
}

static void db_quality_to_text(QualityMode quality, char *out, size_t size) {
    strncpy(out, quality_name(quality), size - 1);
    out[size - 1] = '\0';
}

static void db_copy_text(char *dst, size_t size, const char *src) {
    if (!size) return;
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static int db_parse_instruction(const char *text, Instruction *ins) {
    unsigned op = 0, dst = 0, kind = 0, reg = 0, shift = 0;
    unsigned long long constant = 0;
    if (sscanf(text, "%u,%u,%u,%u,%u,%llx", &op, &dst, &kind, &reg, &shift, &constant) != 6) return 0;
    if (op >= OP_COUNT || dst >= REG_COUNT || kind > OPERAND_CONST || reg >= REG_COUNT || shift >= 64) return 0;
    ins->op = (uint8_t)op;
    ins->dst = (uint8_t)dst;
    ins->operand_kind = (uint8_t)kind;
    ins->operand_reg = (uint8_t)reg;
    ins->shift = (uint8_t)shift;
    ins->constant = (uint64_t)constant;
    return 1;
}

static int db_load(DbStore *db) {
    memset(db, 0, sizeof(*db));
    FILE *file = fopen(DB_PATH, "rb");
    if (!file) return 0;
    char line[512];
    DbRecord *record = NULL;
    uint32_t seen_instructions = 0;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "record") == 0) {
            if (db->count >= MAX_DB_RECORDS) break;
            record = &db->records[db->count];
            memset(record, 0, sizeof(*record));
            record->candidate.deep_score = INT64_MIN;
            record->candidate.source = SOURCE_CHAMPION;
            seen_instructions = 0;
        } else if (strcmp(line, "end") == 0) {
            if (record && seen_instructions == record->candidate.instruction_count &&
                candidate_is_valid(&record->candidate)) {
                db->count++;
            }
            record = NULL;
        } else if (record) {
            if (strncmp(line, "candidate_id=", 13) == 0) {
                record->candidate.id = (uint64_t)strtoull(line + 13, NULL, 0);
            } else if (strncmp(line, "parent_id=", 10) == 0) {
                record->candidate.parent_id = (uint64_t)strtoull(line + 10, NULL, 0);
            } else if (strncmp(line, "generation=", 11) == 0) {
                record->candidate.generation = (uint32_t)strtoul(line + 11, NULL, 0);
            } else if (strncmp(line, "instruction_count=", 18) == 0) {
                record->candidate.instruction_count = (uint32_t)strtoul(line + 18, NULL, 0);
            } else if (strncmp(line, "source=", 7) == 0) {
                const char *source = line + 7;
                record->candidate.source = strcmp(source, "starter") == 0 ? SOURCE_STARTER :
                    strcmp(source, "novelty") == 0 ? SOURCE_NOVELTY :
                    strcmp(source, "random") == 0 ? SOURCE_RANDOM : SOURCE_CHAMPION;
            } else if (strncmp(line, "seed=", 5) == 0) {
                record->seed = (uint64_t)strtoull(line + 5, NULL, 0);
            } else if (strncmp(line, "timestamp=", 10) == 0) {
                record->unix_time = _strtoi64(line + 10, NULL, 0);
            } else if (strncmp(line, "quality=", 8) == 0) {
                db_copy_text(record->quality, sizeof(record->quality), line + 8);
            } else if (strncmp(line, "quick_score=", 12) == 0) {
                record->candidate.quick_score = _strtoi64(line + 12, NULL, 0);
            } else if (strncmp(line, "deep_score=", 11) == 0) {
                record->candidate.deep_score = _strtoi64(line + 11, NULL, 0);
            } else if (strncmp(line, "fail_flags=", 11) == 0) {
                record->candidate.fail_flags = (uint32_t)strtoul(line + 11, NULL, 0);
            } else if (strncmp(line, "fingerprint=", 12) == 0) {
                record->fingerprint = (uint64_t)strtoull(line + 12, NULL, 0);
            } else if (strncmp(line, "audit_worst=", 12) == 0) {
                record->audit_worst = _strtoi64(line + 12, NULL, 0);
            } else if (strncmp(line, "audit_average=", 14) == 0) {
                record->audit_average = _strtoi64(line + 14, NULL, 0);
            } else if (strncmp(line, "audit_flags=", 12) == 0) {
                record->audit_flags = (uint32_t)strtoul(line + 12, NULL, 0);
            } else if (strncmp(line, "report=", 7) == 0) {
                db_copy_text(record->source_report, sizeof(record->source_report), line + 7);
            } else if (strncmp(line, "export=", 7) == 0) {
                db_copy_text(record->export_path, sizeof(record->export_path), line + 7);
            } else if (strncmp(line, "instruction=", 12) == 0 &&
                       seen_instructions < MAX_INSTRUCTIONS &&
                       db_parse_instruction(line + 12, &record->candidate.instructions[seen_instructions])) {
                seen_instructions++;
            }
        }
    }
    fclose(file);
    return 1;
}

static int db_save(const DbStore *db) {
    ensure_out_dir();
    FILE *file = fopen(DB_PATH, "wb");
    if (!file) {
        fprintf(stderr, "failed to open %s\n", DB_PATH);
        return 0;
    }
    fprintf(file, "HASH_FORGE_DB_V1\n");
    fprintf(file, "current_fingerprint=%llu\n", (unsigned long long)current_scoring_fingerprint());
    for (uint32_t i = 0; i < db->count; i++) {
        const DbRecord *record = &db->records[i];
        fprintf(file, "record\n");
        fprintf(file, "candidate_id=%llu\n", (unsigned long long)record->candidate.id);
        fprintf(file, "parent_id=%llu\n", (unsigned long long)record->candidate.parent_id);
        fprintf(file, "generation=%u\n", record->candidate.generation);
        fprintf(file, "instruction_count=%u\n", record->candidate.instruction_count);
        fprintf(file, "source=%s\n", candidate_source_name(record->candidate.source));
        fprintf(file, "seed=%llu\n", (unsigned long long)record->seed);
        fprintf(file, "timestamp=%lld\n", record->unix_time);
        fprintf(file, "quality=%s\n", record->quality[0] ? record->quality : "normal");
        fprintf(file, "quick_score=%lld\n", (long long)record->candidate.quick_score);
        fprintf(file, "deep_score=%lld\n", (long long)record->candidate.deep_score);
        fprintf(file, "fail_flags=0x%x\n", record->candidate.fail_flags);
        fprintf(file, "fingerprint=%llu\n", (unsigned long long)record->fingerprint);
        fprintf(file, "audit_worst=%lld\n", (long long)record->audit_worst);
        fprintf(file, "audit_average=%lld\n", (long long)record->audit_average);
        fprintf(file, "audit_flags=0x%x\n", record->audit_flags);
        fprintf(file, "report=%s\n", record->source_report);
        fprintf(file, "export=%s\n", record->export_path);
        for (uint32_t j = 0; j < record->candidate.instruction_count; j++) {
            const Instruction *ins = &record->candidate.instructions[j];
            fprintf(file, "instruction=%u,%u,%u,%u,%u,%llx\n",
                    ins->op, ins->dst, ins->operand_kind, ins->operand_reg,
                    ins->shift, (unsigned long long)ins->constant);
        }
        fprintf(file, "end\n");
    }
    fclose(file);
    return 1;
}

static int db_find_record(const DbStore *db, uint64_t candidate_id) {
    for (uint32_t i = 0; i < db->count; i++) {
        if (db->records[i].candidate.id == candidate_id) return (int)i;
    }
    return -1;
}

static int db_upsert(DbStore *db, const DbRecord *record) {
    int index = db_find_record(db, record->candidate.id);
    if (index >= 0) {
        db->records[index] = *record;
        return 1;
    }
    if (db->count >= MAX_DB_RECORDS) return 0;
    db->records[db->count++] = *record;
    return 1;
}

static void db_audit_candidate(DbRecord *record, QualityMode quality) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) return;
    int64_t total = 0;
    record->audit_worst = INT64_MAX;
    record->audit_flags = 0;
    const uint32_t audit_count = 5;
    for (uint32_t i = 0; i < audit_count; i++) {
        uint64_t audit_seed = mix_seed(record->seed, record->candidate.id, 1000u + i);
        ScoreResult score = score_candidate(&record->candidate, scratch, audit_seed, 1, quality);
        if (score.score < record->audit_worst) record->audit_worst = score.score;
        total += score.score;
        record->audit_flags |= score.fail_flags;
    }
    record->audit_average = total / (int64_t)audit_count;
    free(scratch);
}

static int db_rescore_record(DbRecord *record, uint64_t fingerprint) {
    ScoreScratch *scratch = (ScoreScratch *)calloc(1, sizeof(*scratch));
    if (!scratch) return 0;
    QualityMode quality = db_quality_from_text(record->quality);
    ScoreResult quick = score_candidate(&record->candidate, scratch, record->seed, 0, quality);
    ScoreResult deep = score_candidate(&record->candidate, scratch, record->seed, 1, quality);
    record->candidate.quick_score = quick.score;
    record->candidate.deep_score = deep.score;
    record->candidate.fail_flags = deep.fail_flags;
    record->fingerprint = fingerprint;
    free(scratch);
    db_audit_candidate(record, quality);
    return 1;
}

static int db_rescore_stale(DbStore *db, uint64_t fingerprint) {
    int rescored = 0;
    for (uint32_t i = 0; i < db->count; i++) {
        if (db->records[i].fingerprint != fingerprint) {
            if (db_rescore_record(&db->records[i], fingerprint)) rescored++;
        }
    }
    return rescored;
}

static int db_record_better(const DbRecord *a, const DbRecord *b) {
    uint32_t a_severity = fail_severity(a->candidate.fail_flags);
    uint32_t b_severity = fail_severity(b->candidate.fail_flags);
    if (a_severity != b_severity) return a_severity < b_severity;
    if (a->candidate.fail_flags != b->candidate.fail_flags) return a->candidate.fail_flags < b->candidate.fail_flags;
    if (a->audit_flags != b->audit_flags) return a->audit_flags < b->audit_flags;
    if (a->candidate.deep_score != b->candidate.deep_score) return a->candidate.deep_score > b->candidate.deep_score;
    if (a->audit_worst != b->audit_worst) return a->audit_worst > b->audit_worst;
    if (a->candidate.quick_score != b->candidate.quick_score) return a->candidate.quick_score > b->candidate.quick_score;
    if (a->candidate.instruction_count != b->candidate.instruction_count) return a->candidate.instruction_count < b->candidate.instruction_count;
    return a->unix_time > b->unix_time;
}

static int compare_db_records(const void *a_ptr, const void *b_ptr) {
    const DbRecord *a = (const DbRecord *)a_ptr;
    const DbRecord *b = (const DbRecord *)b_ptr;
    if (db_record_better(a, b)) return -1;
    if (db_record_better(b, a)) return 1;
    return 0;
}

static void db_from_champion(DbRecord *out, const ChampionRecord *champion, uint64_t fingerprint) {
    memset(out, 0, sizeof(*out));
    out->candidate = champion->candidate;
    out->seed = champion->seed ? champion->seed : CHAMPION_FINGERPRINT_SEED;
    out->unix_time = champion->unix_time;
    db_copy_text(out->quality, sizeof(out->quality), champion->quality[0] ? champion->quality : "deep");
    db_copy_text(out->source_report, sizeof(out->source_report), champion->source_report);
    db_copy_text(out->export_path, sizeof(out->export_path), champion->source_report);
    char *dot = strrchr(out->export_path, '.');
    if (dot) strcpy(dot, ".c");
    out->fingerprint = champion->fingerprint ? champion->fingerprint : fingerprint;
    out->audit_worst = champion->audit_worst;
    out->audit_average = champion->audit_average;
    out->audit_flags = champion->audit_flags;
}

static int db_import_champions(DbStore *db, uint64_t fingerprint) {
    ChampionRecord champions[MAX_CHAMPIONS];
    uint32_t rescored = 0;
    uint32_t count = load_champion_records(champions, MAX_CHAMPIONS, fingerprint, &rescored);
    for (uint32_t i = 0; i < count; i++) {
        DbRecord record;
        db_from_champion(&record, &champions[i], fingerprint);
        db_upsert(db, &record);
    }
    return (int)count;
}

static int parse_best_txt(DbRecord *record) {
    FILE *file = fopen("out/best.txt", "rb");
    if (!file) return 0;
    memset(record, 0, sizeof(*record));
    record->candidate.deep_score = INT64_MIN;
    record->seed = CHAMPION_FINGERPRINT_SEED;
    db_copy_text(record->quality, sizeof(record->quality), "normal");
    db_copy_text(record->source_report, sizeof(record->source_report), "out/report.md");
    db_copy_text(record->export_path, sizeof(record->export_path), "out/best.c");
    record->unix_time = (long long)time(NULL);
    char line[512];
    int in_instructions = 0;
    uint32_t seen = 0;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "instructions_csv:") == 0) {
            in_instructions = 1;
            continue;
        }
        if (in_instructions) {
            if (line[0] && seen < MAX_INSTRUCTIONS &&
                db_parse_instruction(line, &record->candidate.instructions[seen])) {
                seen++;
            }
            continue;
        }
        if (strncmp(line, "id: ", 4) == 0) record->candidate.id = (uint64_t)strtoull(line + 4, NULL, 0);
        else if (strncmp(line, "parent_id: ", 11) == 0) record->candidate.parent_id = (uint64_t)strtoull(line + 11, NULL, 0);
        else if (strncmp(line, "generation: ", 12) == 0) record->candidate.generation = (uint32_t)strtoul(line + 12, NULL, 0);
        else if (strncmp(line, "seed: ", 6) == 0) record->seed = (uint64_t)strtoull(line + 6, NULL, 0);
        else if (strncmp(line, "quality: ", 9) == 0) db_copy_text(record->quality, sizeof(record->quality), line + 9);
        else if (strncmp(line, "quick_score: ", 13) == 0) record->candidate.quick_score = _strtoi64(line + 13, NULL, 0);
        else if (strncmp(line, "deep_score: ", 12) == 0) record->candidate.deep_score = _strtoi64(line + 12, NULL, 0);
        else if (strncmp(line, "fail_flags: ", 12) == 0) record->candidate.fail_flags = (uint32_t)strtoul(line + 12, NULL, 0);
        else if (strncmp(line, "source: ", 8) == 0) {
            const char *source = line + 8;
            record->candidate.source = strcmp(source, "starter") == 0 ? SOURCE_STARTER :
                strcmp(source, "novelty") == 0 ? SOURCE_NOVELTY :
                strcmp(source, "champion") == 0 ? SOURCE_CHAMPION : SOURCE_RANDOM;
        }
    }
    fclose(file);
    record->candidate.instruction_count = seen;
    if (!record->candidate.id && seen) record->candidate.id = candidate_id(&record->candidate);
    return seen > 0 && candidate_is_valid(&record->candidate);
}

static int db_add_latest(DbStore *db, uint64_t fingerprint) {
    DbRecord record;
    if (!parse_best_txt(&record)) return 0;
    db_rescore_record(&record, fingerprint);
    return db_upsert(db, &record);
}

static int db_write_top_report(const DbStore *db, uint32_t limit, uint64_t fingerprint) {
    FILE *md = fopen(DB_TOP_PATH, "wb");
    if (!md) return 0;
    uint32_t shown = db->count < limit ? db->count : limit;
    fprintf(md, "# hash-forge best hash database\n\n");
    fprintf(md, "- Database: `%s`\n", DB_PATH);
    fprintf(md, "- Current scoring fingerprint: `0x%016llx`\n", (unsigned long long)fingerprint);
    fprintf(md, "- Candidates: `%u`\n", db->count);
    fprintf(md, "- Top rows shown: `%u`\n\n", shown);
    fprintf(md, "| rank | status | id | source | quality | quick | deep | flags | flag names | audit worst | audit avg | report | export |\n");
    fprintf(md, "|---:|---|---|---|---|---:|---:|---|---|---:|---:|---|---|\n");
    for (uint32_t i = 0; i < shown; i++) {
        const DbRecord *record = &db->records[i];
        fprintf(md, "| %u | %s | `%llx` | %s | %s | %lld | %lld | `0x%x` | ",
                i + 1,
                record->fingerprint == fingerprint ? "current" : "stale",
                (unsigned long long)record->candidate.id,
                candidate_source_name(record->candidate.source),
                record->quality,
                (long long)record->candidate.quick_score,
                (long long)record->candidate.deep_score,
                record->candidate.fail_flags);
        print_fail_flags(md, record->candidate.fail_flags);
        fprintf(md, " | %lld | %lld | `%s` | `%s` |\n",
                (long long)record->audit_worst,
                (long long)record->audit_average,
                record->source_report,
                record->export_path);
    }
    fclose(md);
    return 1;
}

static int db_print_top(DbStore *db, uint32_t limit, uint64_t fingerprint) {
    int rescored = db_rescore_stale(db, fingerprint);
    if (rescored) db_save(db);
    qsort(db->records, db->count, sizeof(db->records[0]), compare_db_records);
    int wrote = db_write_top_report(db, limit, fingerprint);
    uint32_t shown = db->count < limit ? db->count : limit;
    printf("\n%s%sHash Forge best hash database%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %srecords%s      %u\n", c_dim(), c_reset(), db->count);
    printf("  %srescored%s     %d\n", c_dim(), c_reset(), rescored);
    printf("  %sfingerprint%s  0x%016llx\n\n", c_dim(), c_reset(), (unsigned long long)fingerprint);
    printf("%s%4s  %16s  %-8s  %-6s  %12s  %12s  %7s  %12s  %s%s\n",
           c_dim(), "rank", "id", "source", "qual", "deep", "quick", "flags", "audit", "report", c_reset());
    for (uint32_t i = 0; i < shown; i++) {
        const DbRecord *record = &db->records[i];
        printf("%4u  %s%016llx%s  %-8s  %-6s  %12lld  %12lld  0x%05x  %12lld  %s\n",
               i + 1,
               c_cyan(), (unsigned long long)record->candidate.id, c_reset(),
               candidate_source_name(record->candidate.source),
               record->quality,
               (long long)record->candidate.deep_score,
               (long long)record->candidate.quick_score,
               record->candidate.fail_flags,
               (long long)record->audit_worst,
               record->source_report);
    }
    printf("\n%s%sDB top complete%s\n", c_bold(), c_green(), c_reset());
    printf("  %soutput%s  %s\n", c_dim(), c_reset(), wrote ? DB_TOP_PATH : "export failed");
    return wrote ? 0 : 1;
}

static int db_verify(const DbStore *db, uint64_t fingerprint) {
    uint32_t stale = 0;
    uint32_t invalid = 0;
    uint32_t missing_artifacts = 0;
    for (uint32_t i = 0; i < db->count; i++) {
        const DbRecord *record = &db->records[i];
        if (record->fingerprint != fingerprint) stale++;
        if (!candidate_is_valid(&record->candidate)) invalid++;
        if (record->source_report[0]) {
            FILE *file = fopen(record->source_report, "rb");
            if (file) fclose(file);
            else missing_artifacts++;
        }
    }
    printf("\n%s%sHash Forge DB verify%s\n", c_bold(), c_cyan(), c_reset());
    printf("  %sdatabase%s           %s\n", c_dim(), c_reset(), DB_PATH);
    printf("  %sschema%s             1\n", c_dim(), c_reset());
    printf("  %scandidates%s         %u\n", c_dim(), c_reset(), db->count);
    printf("  %sscore rows%s         %u\n", c_dim(), c_reset(), db->count);
    printf("  %sstale scores%s       %u\n", c_dim(), c_reset(), stale);
    printf("  %smissing artifacts%s  %u\n", c_dim(), c_reset(), missing_artifacts);
    printf("  %sinvalid records%s    %u\n", c_dim(), c_reset(), invalid);
    return invalid == 0 ? 0 : 1;
}

int command_db(const DbOptions *options) {
    uint64_t fingerprint = current_scoring_fingerprint();
    DbStore *db = (DbStore *)calloc(1, sizeof(*db));
    if (!db) {
        fprintf(stderr, "failed to allocate db store\n");
        return 1;
    }
    int loaded = db_load(db);
    if (strcmp(options->action, "init") == 0) {
        if (!loaded) memset(db, 0, sizeof(*db));
        if (!db_save(db)) {
            free(db);
            return 1;
        }
        printf("\n%s%sDB initialized%s\n", c_bold(), c_green(), c_reset());
        printf("  %spath%s         %s\n", c_dim(), c_reset(), DB_PATH);
        printf("  %srecords%s      %u\n", c_dim(), c_reset(), db->count);
        printf("  %sfingerprint%s  0x%016llx\n", c_dim(), c_reset(), (unsigned long long)fingerprint);
        free(db);
        return 0;
    }
    if (!loaded) {
        memset(db, 0, sizeof(*db));
    }
    if (strcmp(options->action, "import-champions") == 0) {
        int imported = db_import_champions(db, fingerprint);
        if (!db_save(db)) {
            free(db);
            return 1;
        }
        printf("\n%s%sDB import complete%s\n", c_bold(), c_green(), c_reset());
        printf("  %simported%s  %d champion records\n", c_dim(), c_reset(), imported);
        printf("  %srecords%s   %u\n", c_dim(), c_reset(), db->count);
        free(db);
        return 0;
    }
    if (strcmp(options->action, "add-latest") == 0) {
        if (!db_add_latest(db, fingerprint)) {
            fprintf(stderr, "failed to add latest best candidate; run evolution with current binary first\n");
            free(db);
            return 1;
        }
        if (!db_save(db)) {
            free(db);
            return 1;
        }
        printf("\n%s%sDB latest added%s\n", c_bold(), c_green(), c_reset());
        printf("  %srecords%s  %u\n", c_dim(), c_reset(), db->count);
        free(db);
        return 0;
    }
    if (strcmp(options->action, "rescore") == 0) {
        int rescored = db_rescore_stale(db, fingerprint);
        if (!db_save(db)) {
            free(db);
            return 1;
        }
        printf("\n%s%sDB rescore complete%s\n", c_bold(), c_green(), c_reset());
        printf("  %srescored%s  %d\n", c_dim(), c_reset(), rescored);
        printf("  %srecords%s   %u\n", c_dim(), c_reset(), db->count);
        free(db);
        return 0;
    }
    if (strcmp(options->action, "top") == 0) {
        int rc = db_print_top(db, options->limit, fingerprint);
        free(db);
        return rc;
    }
    if (strcmp(options->action, "verify") == 0) {
        int rc = db_verify(db, fingerprint);
        free(db);
        return rc;
    }
    fprintf(stderr, "unknown db action: %s\n", options->action);
    free(db);
    return 2;
}
