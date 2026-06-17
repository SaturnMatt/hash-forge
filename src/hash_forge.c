#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program) {
    printf("usage:\n");
    printf("  %s self-test\n", program);
    printf("  %s run --seed <u64> [--generations <n>]\n", program);
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

static int command_self_test(void) {
    printf("hash-forge self-test scaffold: pass\n");
    return 0;
}

static int command_run(int argc, char **argv) {
    uint64_t seed = 0;
    uint64_t generations = 0;
    int have_seed = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &seed)) {
                fprintf(stderr, "invalid --seed value\n");
                return 2;
            }
            have_seed = 1;
        } else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &generations)) {
                fprintf(stderr, "invalid --generations value\n");
                return 2;
            }
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!have_seed) {
        fprintf(stderr, "run requires --seed <u64>\n");
        return 2;
    }

    printf("hash-forge run scaffold\n");
    printf("seed=%llu generations=%llu\n",
           (unsigned long long)seed,
           (unsigned long long)generations);
    printf("engine implementation is next; see SPEC.md\n");
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
        return command_run(argc, argv);
    }

    print_usage(argv[0]);
    return 2;
}
