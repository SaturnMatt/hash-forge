#include "hash_table.h"

#include <stdlib.h>
#include <string.h>

enum {
    HF_SLOT_EMPTY = 0,
    HF_SLOT_FULL = 1,
    HF_SLOT_DELETED = 2
};

static uint64_t hf_mix_u64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static size_t hf_next_power_of_two(size_t value) {
    size_t capacity = 16;
    while (capacity < value) {
        if (capacity > ((size_t)-1) / 2) {
            return 0;
        }
        capacity *= 2;
    }
    return capacity;
}

static void hf_insert_rehashed(hf_table_entry *entries, size_t capacity, uint64_t key, uint64_t value) {
    size_t index = (size_t)hf_mix_u64(key) & (capacity - 1);

    for (;;) {
        hf_table_entry *entry = &entries[index];
        if (entry->state != HF_SLOT_FULL) {
            entry->key = key;
            entry->value = value;
            entry->state = HF_SLOT_FULL;
            return;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static int hf_table_rehash(hf_table *table, size_t requested_capacity) {
    size_t capacity = hf_next_power_of_two(requested_capacity);
    if (capacity == 0) {
        return 0;
    }

    hf_table_entry *entries = (hf_table_entry *)calloc(capacity, sizeof(*entries));
    if (!entries) {
        return 0;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        hf_table_entry *entry = &table->entries[i];
        if (entry->state == HF_SLOT_FULL) {
            hf_insert_rehashed(entries, capacity, entry->key, entry->value);
        }
    }

    free(table->entries);
    table->entries = entries;
    table->capacity = capacity;
    table->tombstones = 0;
    return 1;
}

int hf_table_init(hf_table *table, size_t initial_capacity) {
    if (!table) {
        return 0;
    }

    table->entries = NULL;
    table->capacity = 0;
    table->count = 0;
    table->tombstones = 0;

    return hf_table_rehash(table, initial_capacity);
}

void hf_table_destroy(hf_table *table) {
    if (!table) {
        return;
    }

    free(table->entries);
    table->entries = NULL;
    table->capacity = 0;
    table->count = 0;
    table->tombstones = 0;
}

void hf_table_clear(hf_table *table) {
    if (!table || !table->entries) {
        return;
    }

    memset(table->entries, 0, table->capacity * sizeof(*table->entries));
    table->count = 0;
    table->tombstones = 0;
}

static int hf_table_prepare_insert(hf_table *table) {
    if (table->capacity == 0) {
        return hf_table_rehash(table, 16);
    }

    if ((table->count + table->tombstones + 1) * 10 >= table->capacity * 7) {
        size_t requested = table->capacity;
        if ((table->count + 1) * 10 >= table->capacity * 7) {
            requested = table->capacity * 2;
        }
        return hf_table_rehash(table, requested);
    }

    return 1;
}

int hf_table_put(hf_table *table, uint64_t key, uint64_t value) {
    size_t first_deleted = (size_t)-1;

    if (!table || !hf_table_prepare_insert(table)) {
        return 0;
    }

    size_t index = (size_t)hf_mix_u64(key) & (table->capacity - 1);

    for (;;) {
        hf_table_entry *entry = &table->entries[index];
        if (entry->state == HF_SLOT_EMPTY) {
            if (first_deleted != (size_t)-1) {
                entry = &table->entries[first_deleted];
                table->tombstones--;
            }
            entry->key = key;
            entry->value = value;
            entry->state = HF_SLOT_FULL;
            table->count++;
            return 1;
        }

        if (entry->state == HF_SLOT_DELETED) {
            if (first_deleted == (size_t)-1) {
                first_deleted = index;
            }
        } else if (entry->key == key) {
            entry->value = value;
            return 1;
        }

        index = (index + 1) & (table->capacity - 1);
    }
}

int hf_table_get(const hf_table *table, uint64_t key, uint64_t *value) {
    if (!table || !table->entries || table->capacity == 0) {
        return 0;
    }

    size_t index = (size_t)hf_mix_u64(key) & (table->capacity - 1);

    for (;;) {
        const hf_table_entry *entry = &table->entries[index];
        if (entry->state == HF_SLOT_EMPTY) {
            return 0;
        }
        if (entry->state == HF_SLOT_FULL && entry->key == key) {
            if (value) {
                *value = entry->value;
            }
            return 1;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

int hf_table_remove(hf_table *table, uint64_t key) {
    if (!table || !table->entries || table->capacity == 0) {
        return 0;
    }

    size_t index = (size_t)hf_mix_u64(key) & (table->capacity - 1);

    for (;;) {
        hf_table_entry *entry = &table->entries[index];
        if (entry->state == HF_SLOT_EMPTY) {
            return 0;
        }
        if (entry->state == HF_SLOT_FULL && entry->key == key) {
            entry->state = HF_SLOT_DELETED;
            table->count--;
            table->tombstones++;
            return 1;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

size_t hf_table_count(const hf_table *table) {
    return table ? table->count : 0;
}

size_t hf_table_capacity(const hf_table *table) {
    return table ? table->capacity : 0;
}
