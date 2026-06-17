#ifndef HASH_FORGE_DYNAMIC_ARRAY_H
#define HASH_FORGE_DYNAMIC_ARRAY_H

#include <stddef.h>

typedef struct hf_array {
    void *data;
    size_t count;
    size_t capacity;
    size_t element_size;
} hf_array;

int hf_array_init(hf_array *array, size_t element_size, size_t initial_capacity);
void hf_array_destroy(hf_array *array);
void hf_array_clear(hf_array *array);

int hf_array_reserve(hf_array *array, size_t capacity);
int hf_array_resize(hf_array *array, size_t count);

int hf_array_push(hf_array *array, const void *element);
void *hf_array_push_uninit(hf_array *array);
int hf_array_pop(hf_array *array, void *out_element);

void *hf_array_get(hf_array *array, size_t index);
const void *hf_array_get_const(const hf_array *array, size_t index);
void *hf_array_data(hf_array *array);
const void *hf_array_data_const(const hf_array *array);

size_t hf_array_count(const hf_array *array);
size_t hf_array_capacity(const hf_array *array);
size_t hf_array_element_size(const hf_array *array);

#endif
