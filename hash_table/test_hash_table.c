#include "hash_table.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static uint64_t test_mix(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static void test_empty_table(void) {
    hf_table table;
    uint64_t value = 123;

    CHECK(hf_table_init(&table, 0));
    CHECK(hf_table_count(&table) == 0);
    CHECK(hf_table_capacity(&table) >= 16);
    CHECK(!hf_table_get(&table, 42, &value));
    CHECK(value == 123);
    CHECK(!hf_table_remove(&table, 42));

    hf_table_destroy(&table);
}

static void test_insert_lookup_and_update(void) {
    hf_table table;
    uint64_t value = 0;

    CHECK(hf_table_init(&table, 2));
    CHECK(hf_table_put(&table, 0, 11));
    CHECK(hf_table_put(&table, UINT64_MAX, 22));
    CHECK(hf_table_put(&table, 12345, 33));
    CHECK(hf_table_count(&table) == 3);

    CHECK(hf_table_get(&table, 0, &value) && value == 11);
    CHECK(hf_table_get(&table, UINT64_MAX, &value) && value == 22);
    CHECK(hf_table_get(&table, 12345, &value) && value == 33);

    CHECK(hf_table_put(&table, UINT64_MAX, 44));
    CHECK(hf_table_count(&table) == 3);
    CHECK(hf_table_get(&table, UINT64_MAX, &value) && value == 44);

    hf_table_destroy(&table);
}

static void test_growth_and_many_keys(void) {
    enum { N = 20000 };
    hf_table table;
    uint64_t value = 0;

    CHECK(hf_table_init(&table, 1));

    for (uint64_t i = 0; i < N; i++) {
        uint64_t key = test_mix(i + 0x123456789abcdef0ULL);
        CHECK(hf_table_put(&table, key, i ^ UINT64_C(0xa5a5a5a5a5a5a5a5)));
    }

    CHECK(hf_table_count(&table) == N);
    CHECK(hf_table_capacity(&table) > 16);

    for (uint64_t i = 0; i < N; i++) {
        uint64_t key = test_mix(i + 0x123456789abcdef0ULL);
        CHECK(hf_table_get(&table, key, &value));
        CHECK(value == (i ^ UINT64_C(0xa5a5a5a5a5a5a5a5)));
    }

    hf_table_destroy(&table);
}

static void test_remove_and_reuse_tombstones(void) {
    enum { N = 4096 };
    hf_table table;
    uint64_t value = 0;

    CHECK(hf_table_init(&table, 16));

    for (uint64_t i = 0; i < N; i++) {
        CHECK(hf_table_put(&table, i * 17, i + 1));
    }

    for (uint64_t i = 0; i < N; i += 2) {
        CHECK(hf_table_remove(&table, i * 17));
    }

    CHECK(hf_table_count(&table) == N / 2);

    for (uint64_t i = 0; i < N; i++) {
        int found = hf_table_get(&table, i * 17, &value);
        if ((i & 1) == 0) {
            CHECK(!found);
        } else {
            CHECK(found && value == i + 1);
        }
    }

    for (uint64_t i = 0; i < N; i += 2) {
        CHECK(hf_table_put(&table, i * 17, i + 100));
    }

    CHECK(hf_table_count(&table) == N);

    for (uint64_t i = 0; i < N; i++) {
        CHECK(hf_table_get(&table, i * 17, &value));
        CHECK(value == ((i & 1) ? i + 1 : i + 100));
    }

    hf_table_destroy(&table);
}

static void test_clear(void) {
    hf_table table;
    uint64_t value = 0;

    CHECK(hf_table_init(&table, 64));
    CHECK(hf_table_put(&table, 1, 10));
    CHECK(hf_table_put(&table, 2, 20));

    hf_table_clear(&table);
    CHECK(hf_table_count(&table) == 0);
    CHECK(!hf_table_get(&table, 1, &value));
    CHECK(!hf_table_get(&table, 2, &value));
    CHECK(hf_table_put(&table, 2, 200));
    CHECK(hf_table_get(&table, 2, &value) && value == 200);

    hf_table_destroy(&table);
}

int main(void) {
    test_empty_table();
    test_insert_lookup_and_update();
    test_growth_and_many_keys();
    test_remove_and_reuse_tombstones();
    test_clear();

    if (failures) {
        printf("hash_table tests: %d failure(s)\n", failures);
        return 1;
    }

    printf("hash_table tests: pass\n");
    return 0;
}
