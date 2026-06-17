#ifndef HASH_FORGE_HASH_TABLE_H
#define HASH_FORGE_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct hf_table_entry {
    uint64_t key;
    uint64_t value;
    uint8_t state;
} hf_table_entry;

typedef struct hf_table {
    hf_table_entry *entries;
    size_t capacity;
    size_t count;
    size_t tombstones;
} hf_table;

int hf_table_init(hf_table *table, size_t initial_capacity);
void hf_table_destroy(hf_table *table);
void hf_table_clear(hf_table *table);

int hf_table_put(hf_table *table, uint64_t key, uint64_t value);
int hf_table_get(const hf_table *table, uint64_t key, uint64_t *value);
int hf_table_remove(hf_table *table, uint64_t key);

size_t hf_table_count(const hf_table *table);
size_t hf_table_capacity(const hf_table *table);

#endif
