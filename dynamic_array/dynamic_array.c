#include "dynamic_array.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int hf_array_bytes_fit(size_t count, size_t element_size, size_t *bytes) {
    if (element_size == 0 || count > ((size_t)-1) / element_size) {
        return 0;
    }
    *bytes = count * element_size;
    return 1;
}

static size_t hf_array_next_capacity(size_t needed) {
    size_t capacity = 8;
    while (capacity < needed) {
        if (capacity > ((size_t)-1) / 2) {
            return 0;
        }
        capacity *= 2;
    }
    return capacity;
}

int hf_array_init(hf_array *array, size_t element_size, size_t initial_capacity) {
    if (!array || element_size == 0) {
        return 0;
    }

    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->element_size = element_size;

    return initial_capacity == 0 || hf_array_reserve(array, initial_capacity);
}

void hf_array_destroy(hf_array *array) {
    if (!array) {
        return;
    }

    free(array->data);
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->element_size = 0;
}

void hf_array_clear(hf_array *array) {
    if (array) {
        array->count = 0;
    }
}

int hf_array_reserve(hf_array *array, size_t capacity) {
    size_t bytes = 0;

    if (!array || array->element_size == 0) {
        return 0;
    }
    if (capacity <= array->capacity) {
        return 1;
    }
    if (!hf_array_bytes_fit(capacity, array->element_size, &bytes)) {
        return 0;
    }

    void *data = realloc(array->data, bytes);
    if (!data) {
        return 0;
    }

    array->data = data;
    array->capacity = capacity;
    return 1;
}

int hf_array_resize(hf_array *array, size_t count) {
    if (!array || array->element_size == 0) {
        return 0;
    }

    if (count > array->capacity) {
        size_t capacity = hf_array_next_capacity(count);
        if (capacity == 0 || !hf_array_reserve(array, capacity)) {
            return 0;
        }
    }

    if (count > array->count) {
        uint8_t *base = (uint8_t *)array->data;
        size_t old_bytes = array->count * array->element_size;
        size_t new_bytes = count * array->element_size;
        memset(base + old_bytes, 0, new_bytes - old_bytes);
    }

    array->count = count;
    return 1;
}

int hf_array_push(hf_array *array, const void *element) {
    void *slot = hf_array_push_uninit(array);
    if (!slot) {
        return 0;
    }

    if (element) {
        memcpy(slot, element, array->element_size);
    } else {
        memset(slot, 0, array->element_size);
    }
    return 1;
}

void *hf_array_push_uninit(hf_array *array) {
    size_t capacity = 0;

    if (!array || array->element_size == 0) {
        return NULL;
    }
    if (array->count == (size_t)-1) {
        return NULL;
    }
    if (array->count == array->capacity) {
        capacity = hf_array_next_capacity(array->count + 1);
        if (capacity == 0 || !hf_array_reserve(array, capacity)) {
            return NULL;
        }
    }

    uint8_t *base = (uint8_t *)array->data;
    void *slot = base + array->count * array->element_size;
    array->count++;
    return slot;
}

int hf_array_pop(hf_array *array, void *out_element) {
    if (!array || array->count == 0 || array->element_size == 0) {
        return 0;
    }

    array->count--;
    if (out_element) {
        uint8_t *base = (uint8_t *)array->data;
        memcpy(out_element, base + array->count * array->element_size, array->element_size);
    }
    return 1;
}

void *hf_array_get(hf_array *array, size_t index) {
    if (!array || index >= array->count || array->element_size == 0) {
        return NULL;
    }

    return (uint8_t *)array->data + index * array->element_size;
}

const void *hf_array_get_const(const hf_array *array, size_t index) {
    if (!array || index >= array->count || array->element_size == 0) {
        return NULL;
    }

    return (const uint8_t *)array->data + index * array->element_size;
}

void *hf_array_data(hf_array *array) {
    return array ? array->data : NULL;
}

const void *hf_array_data_const(const hf_array *array) {
    return array ? array->data : NULL;
}

size_t hf_array_count(const hf_array *array) {
    return array ? array->count : 0;
}

size_t hf_array_capacity(const hf_array *array) {
    return array ? array->capacity : 0;
}

size_t hf_array_element_size(const hf_array *array) {
    return array ? array->element_size : 0;
}
