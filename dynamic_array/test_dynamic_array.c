#include "dynamic_array.h"

#include <stdint.h>
#include <stdio.h>

typedef struct pair_u64 {
    uint64_t a;
    uint64_t b;
} pair_u64;

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_init_and_reserve(void) {
    hf_array array;

    CHECK(!hf_array_init(NULL, sizeof(uint64_t), 0));
    CHECK(!hf_array_init(&array, 0, 0));

    CHECK(hf_array_init(&array, sizeof(uint64_t), 3));
    CHECK(hf_array_count(&array) == 0);
    CHECK(hf_array_capacity(&array) >= 3);
    CHECK(hf_array_element_size(&array) == sizeof(uint64_t));
    CHECK(hf_array_reserve(&array, 2));
    CHECK(hf_array_capacity(&array) >= 3);
    CHECK(hf_array_reserve(&array, 64));
    CHECK(hf_array_capacity(&array) >= 64);

    hf_array_destroy(&array);
    CHECK(hf_array_count(&array) == 0);
    CHECK(hf_array_capacity(&array) == 0);
}

static void test_push_get_and_direct_data(void) {
    enum { N = 10000 };
    hf_array array;

    CHECK(hf_array_init(&array, sizeof(uint64_t), 0));

    for (uint64_t i = 0; i < N; i++) {
        uint64_t value = i * 17 + 3;
        CHECK(hf_array_push(&array, &value));
    }

    CHECK(hf_array_count(&array) == N);
    CHECK(hf_array_capacity(&array) >= N);

    uint64_t *items = (uint64_t *)hf_array_data(&array);
    CHECK(items != NULL);
    for (uint64_t i = 0; i < N; i++) {
        CHECK(items[i] == i * 17 + 3);
        CHECK(*(uint64_t *)hf_array_get(&array, (size_t)i) == i * 17 + 3);
    }
    CHECK(hf_array_get(&array, N) == NULL);

    hf_array_destroy(&array);
}

static void test_push_uninit_structs(void) {
    enum { N = 4096 };
    hf_array array;

    CHECK(hf_array_init(&array, sizeof(pair_u64), N));

    for (uint64_t i = 0; i < N; i++) {
        pair_u64 *slot = (pair_u64 *)hf_array_push_uninit(&array);
        CHECK(slot != NULL);
        slot->a = i;
        slot->b = i ^ UINT64_C(0xabcdef1234567890);
    }

    for (uint64_t i = 0; i < N; i++) {
        const pair_u64 *slot = (const pair_u64 *)hf_array_get_const(&array, (size_t)i);
        CHECK(slot != NULL);
        CHECK(slot->a == i);
        CHECK(slot->b == (i ^ UINT64_C(0xabcdef1234567890)));
    }

    hf_array_destroy(&array);
}

static void test_resize_clear_and_zero_fill(void) {
    hf_array array;

    CHECK(hf_array_init(&array, sizeof(uint32_t), 0));
    CHECK(hf_array_resize(&array, 8));
    CHECK(hf_array_count(&array) == 8);

    uint32_t *items = (uint32_t *)hf_array_data(&array);
    for (size_t i = 0; i < 8; i++) {
        CHECK(items[i] == 0);
        items[i] = (uint32_t)(i + 1);
    }

    CHECK(hf_array_resize(&array, 4));
    CHECK(hf_array_count(&array) == 4);
    CHECK(hf_array_resize(&array, 10));
    items = (uint32_t *)hf_array_data(&array);
    for (size_t i = 0; i < 4; i++) {
        CHECK(items[i] == i + 1);
    }
    for (size_t i = 4; i < 10; i++) {
        CHECK(items[i] == 0);
    }

    hf_array_clear(&array);
    CHECK(hf_array_count(&array) == 0);
    CHECK(hf_array_capacity(&array) >= 10);

    hf_array_destroy(&array);
}

static void test_pop(void) {
    hf_array array;
    uint64_t out = 0;

    CHECK(hf_array_init(&array, sizeof(uint64_t), 0));
    CHECK(!hf_array_pop(&array, &out));

    for (uint64_t i = 0; i < 5; i++) {
        CHECK(hf_array_push(&array, &i));
    }

    for (uint64_t expected = 4; expected != UINT64_MAX; expected--) {
        out = UINT64_MAX;
        CHECK(hf_array_pop(&array, &out));
        CHECK(out == expected);
        if (expected == 0) {
            break;
        }
    }

    CHECK(hf_array_count(&array) == 0);
    CHECK(!hf_array_pop(&array, NULL));

    hf_array_destroy(&array);
}

int main(void) {
    test_init_and_reserve();
    test_push_get_and_direct_data();
    test_push_uninit_structs();
    test_resize_clear_and_zero_fill();
    test_pop();

    if (failures) {
        printf("dynamic_array tests: %d failure(s)\n", failures);
        return 1;
    }

    printf("dynamic_array tests: pass\n");
    return 0;
}
