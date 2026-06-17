#include "hf_core.h"

static const char *panel_blue(void) { return g_color_enabled ? "\x1b[34m" : ""; }

static void panel_sparkline(const struct ImprovementLog *log, char out[49]) {
    static const char levels[] = ".:-=+*#%@";
    uint32_t count = log->count;
    uint32_t samples = count < 48 ? count : 48;
    int64_t min_score = INT64_MAX;
    int64_t max_score = INT64_MIN;
    if (samples == 0) {
        strcpy(out, "collecting");
        return;
    }
    for (uint32_t i = 0; i < samples; i++) {
        uint32_t index = samples == 1 ? 0 : (uint32_t)(((uint64_t)i * (count - 1)) / (samples - 1));
        const struct ImprovementEvent *event = &log->events[index];
        int64_t score = event->quick_score;
        if (score < min_score) min_score = score;
        if (score > max_score) max_score = score;
    }
    for (uint32_t i = 0; i < samples; i++) {
        uint32_t index = samples == 1 ? 0 : (uint32_t)(((uint64_t)i * (count - 1)) / (samples - 1));
        const struct ImprovementEvent *event = &log->events[index];
        int64_t score = event->quick_score;
        uint32_t level = 8;
        if (max_score > min_score) {
            level = (uint32_t)(((score - min_score) * 8) / (max_score - min_score));
            if (level > 8) level = 8;
        }
        out[i] = levels[level];
    }
    out[samples] = '\0';
}

static void panel_bar(char *out, size_t size, uint64_t value, uint64_t max_value, uint32_t width) {
    uint32_t filled = 0;
    if (size == 0) return;
    if (width + 1 > size) width = (uint32_t)size - 1;
    if (max_value) {
        filled = (uint32_t)((value * width + max_value - 1) / max_value);
        if (filled > width) filled = width;
    }
    for (uint32_t i = 0; i < width; i++) out[i] = i < filled ? '#' : '.';
    out[width] = '\0';
}

static const char *flag_health(uint32_t flags, uint32_t mask) {
    return (flags & mask) ? "WARN" : "OK";
}

static const char *flag_color(uint32_t flags, uint32_t mask) {
    return (flags & mask) ? c_red() : c_green();
}

static void print_instruction_dna(const Candidate *candidate) {
    uint32_t op_counts[OP_COUNT];
    memset(op_counts, 0, sizeof(op_counts));
    for (uint32_t i = 0; i < candidate->instruction_count; i++) {
        if (candidate->instructions[i].op < OP_COUNT) op_counts[candidate->instructions[i].op]++;
    }

    printf("  %sop mix%s   ", c_dim(), c_reset());
    for (uint32_t i = 0; i < OP_COUNT; i++) {
        if (!op_counts[i]) continue;
        printf("%s%s%s:%u ", i == OP_MUL || i == OP_ROTL || i == OP_ROTR ? c_magenta() : c_cyan(),
               op_name((uint8_t)i), c_reset(), op_counts[i]);
    }
    printf("\n  %sdna%s      ", c_dim(), c_reset());
    for (uint32_t i = 0; i < candidate->instruction_count && i < 12; i++) {
        printf("%s%s%s", i ? " " : "", op_name(candidate->instructions[i].op), i + 1 == candidate->instruction_count ? "" : "");
    }
    if (candidate->instruction_count > 12) printf(" ...");
    printf("\n");
}

static void print_mix_line(const char *label, uint32_t value, uint32_t total, const char *color) {
    char bar[25];
    panel_bar(bar, sizeof(bar), value, total ? total : 1, 24);
    printf("  %s%-8s%s %s%-24s%s %3u/%u\n", c_dim(), label, c_reset(), color, bar, c_reset(), value, total);
}

static void print_test_strip(uint32_t flags) {
    printf("  %stests%s    ", c_dim(), c_reset());
    printf("%sZERO:%s%s ", flag_color(flags, FAIL_ZERO), flag_health(flags, FAIL_ZERO), c_reset());
    printf("%sCOLL:%s%s ", flag_color(flags, FAIL_COLLISION), flag_health(flags, FAIL_COLLISION), c_reset());
    printf("%sBUCK:%s%s ", flag_color(flags, FAIL_BUCKET), flag_health(flags, FAIL_BUCKET), c_reset());
    printf("%sAVAL:%s%s ", flag_color(flags, FAIL_AVALANCHE), flag_health(flags, FAIL_AVALANCHE), c_reset());
    printf("%sDIFF:%s%s ", flag_color(flags, FAIL_DIFFERENTIAL), flag_health(flags, FAIL_DIFFERENTIAL), c_reset());
    printf("%sSENS:%s%s\n", flag_color(flags, FAIL_SENSITIVITY), flag_health(flags, FAIL_SENSITIVITY), c_reset());
}

static void print_panel_body(const RunOptions *options, const Candidate *candidate, const PanelSnapshot *snapshot, int final) {
    uint64_t total = snapshot->quick_candidates_evaluated + snapshot->deep_candidates_evaluated;
    double rate = snapshot->elapsed_seconds > 0.0 ? (double)total / snapshot->elapsed_seconds : 0.0;
    char gen_bar[25];
    char sec_bar[25];
    char unique_bar[25];
    const char *state_color = candidate->fail_flags ? c_yellow() : c_green();

    panel_bar(gen_bar, sizeof(gen_bar), snapshot->generation, options->generations, 24);
    panel_bar(sec_bar, sizeof(sec_bar), (uint64_t)snapshot->elapsed_seconds, options->have_seconds ? options->seconds : 0, 24);
    panel_bar(unique_bar, sizeof(unique_bar), snapshot->last_unique_candidates, POPULATION_SIZE, 24);

    if (g_color_enabled) printf("\x1b[H\x1b[2J");
    printf("%s%sHASH-FORGE LIVE PANEL%s %s[%s]%s seed=%llu  quality=%s  threads=%u\n",
           c_bold(), c_cyan(), c_reset(), panel_blue(), final ? "complete" : "running", c_reset(),
           (unsigned long long)options->seed, quality_name(options->quality), snapshot->threads);
    printf("%s--------------------------------------------------------------------------------%s\n", c_dim(), c_reset());
    printf("  %selapsed%s  %7.2fs   %sgeneration%s %-8llu  %sevaluated%s %-12llu  %srate%s %.0f/s\n",
           c_dim(), c_reset(), snapshot->elapsed_seconds,
           c_dim(), c_reset(), (unsigned long long)snapshot->generation,
           c_dim(), c_reset(), (unsigned long long)total,
           c_dim(), c_reset(), rate);
    printf("  %slimits%s   gen [%s] %s   sec [%s] %s   stop=%s\n",
           c_dim(), c_reset(),
           gen_bar, options->generations ? "set" : "open",
           sec_bar, options->have_seconds ? "set" : "open",
           snapshot->stop_reason ? snapshot->stop_reason : "running");
    printf("  %sbest%s     %s%016llx%s  q=%lld  d=",
           c_dim(), c_reset(), c_cyan(), (unsigned long long)candidate->id, c_reset(),
           (long long)candidate->quick_score);
    if (candidate->deep_score == INT64_MIN) printf("pending");
    else printf("%lld", (long long)candidate->deep_score);
    printf("  %sflags=0x%x%s  len=%u  source=%s\n",
           state_color, candidate->fail_flags, c_reset(), candidate->instruction_count,
           candidate_source_name(candidate->source));

    print_test_strip(candidate->fail_flags);
    printf("  %sscore%s    %s%s%s\n", c_dim(), c_reset(), c_green(), snapshot->score_sparkline[0] ? snapshot->score_sparkline : "collecting", c_reset());
    printf("  %sunique%s   %s%-24s%s %u/%u   novelty=%llu best=%llu avg=%llu\n",
           c_dim(), c_reset(), c_green(), unique_bar, c_reset(), snapshot->last_unique_candidates, POPULATION_SIZE,
           (unsigned long long)snapshot->novelty_candidates_admitted,
           (unsigned long long)snapshot->last_best_novelty_score,
           (unsigned long long)snapshot->last_avg_novelty_score);

    printf("\n%s  population lanes%s\n", c_bold(), c_reset());
    print_mix_line("random", snapshot->source_counts[SOURCE_RANDOM], POPULATION_SIZE, c_cyan());
    print_mix_line("starter", snapshot->source_counts[SOURCE_STARTER], POPULATION_SIZE, c_green());
    print_mix_line("champion", snapshot->source_counts[SOURCE_CHAMPION], POPULATION_SIZE, c_yellow());
    print_mix_line("novelty", snapshot->source_counts[SOURCE_NOVELTY], POPULATION_SIZE, c_magenta());

    printf("\n%s  improvement signal%s\n", c_bold(), c_reset());
    if (snapshot->has_last_improvement) {
        printf("  %slast%s     gen=%llu t=%.2fs id=%s%016llx%s q=%lld d=",
               c_dim(), c_reset(),
               (unsigned long long)snapshot->last_improvement_generation,
               snapshot->last_improvement_elapsed,
               c_cyan(), (unsigned long long)snapshot->last_improvement_candidate_id, c_reset(),
               (long long)snapshot->last_improvement_quick_score);
        if (snapshot->last_improvement_deep_score == INT64_MIN) printf("pending");
        else printf("%lld", (long long)snapshot->last_improvement_deep_score);
        printf(" flags=0x%x source=%s reason=%s\n",
               snapshot->last_improvement_flags,
               candidate_source_name(snapshot->last_improvement_source),
               improvement_reason_name(snapshot->last_improvement_reason));
    } else {
        printf("  %slast%s     collecting first signal\n", c_dim(), c_reset());
    }
    print_instruction_dna(candidate);
    fflush(stdout);
}

void panel_render(const RunOptions *options, const Candidate *candidate, const PanelSnapshot *snapshot) {
    print_panel_body(options, candidate, snapshot, 0);
}

void panel_render_final(const RunOptions *options, const Candidate *candidate, const RunReport *report) {
    PanelSnapshot snapshot;
    const struct ImprovementEvent *last = last_improvement_event(&report->improvements);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.generation = report->run_generation;
    snapshot.quick_candidates_evaluated = report->quick_candidates_evaluated;
    snapshot.deep_candidates_evaluated = report->deep_candidates_evaluated;
    snapshot.elapsed_seconds = report->elapsed_seconds;
    snapshot.stop_reason = report->stop_reason;
    snapshot.threads = report->threads;
    snapshot.last_unique_candidates = report->last_unique_candidates;
    memcpy(snapshot.source_counts, report->final_source_counts, sizeof(snapshot.source_counts));
    snapshot.novelty_candidates_admitted = report->novelty_candidates_admitted;
    snapshot.last_best_novelty_score = report->last_best_novelty_score;
    snapshot.last_avg_novelty_score = report->last_avg_novelty_score;
    panel_sparkline(&report->improvements, snapshot.score_sparkline);
    if (last) {
        snapshot.has_last_improvement = 1;
        snapshot.last_improvement_generation = last->run_generation;
        snapshot.last_improvement_elapsed = last->elapsed_seconds;
        snapshot.last_improvement_candidate_id = last->candidate_id;
        snapshot.last_improvement_quick_score = last->quick_score;
        snapshot.last_improvement_deep_score = last->deep_score;
        snapshot.last_improvement_flags = last->fail_flags;
        snapshot.last_improvement_source = last->source;
        snapshot.last_improvement_reason = last->reason;
    }
    panel_render(options, candidate, &snapshot);
    printf("\n");
}
