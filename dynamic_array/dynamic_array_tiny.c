#include "dynamic_array.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int bytes(size_t n, size_t z, size_t *b) { return z && n <= (size_t)-1 / z ? (*b = n * z, 1) : 0; }
static size_t grow(size_t n) { size_t c = 8; while (c < n) { if (c > (size_t)-1 / 2) return 0; c *= 2; } return c; }

int hf_array_init(hf_array *a, size_t z, size_t c) {
    if (!a || !z) return 0;
    a->data = NULL; a->count = a->capacity = 0; a->element_size = z;
    return !c || hf_array_reserve(a, c);
}

void hf_array_destroy(hf_array *a) {
    if (a) { free(a->data); a->data = NULL; a->count = a->capacity = a->element_size = 0; }
}

void hf_array_clear(hf_array *a) { if (a) a->count = 0; }

int hf_array_reserve(hf_array *a, size_t c) {
    size_t b; void *p;
    if (!a || !a->element_size || c <= a->capacity) return a && a->element_size;
    if (!bytes(c, a->element_size, &b) || !(p = realloc(a->data, b))) return 0;
    a->data = p; a->capacity = c; return 1;
}

int hf_array_resize(hf_array *a, size_t n) {
    size_t old;
    if (!a || !a->element_size) return 0;
    if (n > a->capacity && !hf_array_reserve(a, grow(n))) return 0;
    old = a->count * a->element_size;
    if (n > a->count) memset((uint8_t *)a->data + old, 0, n * a->element_size - old);
    a->count = n; return 1;
}

void *hf_array_push_uninit(hf_array *a) {
    void *p;
    if (!a || !a->element_size || a->count == (size_t)-1) return NULL;
    if (a->count == a->capacity && !hf_array_reserve(a, grow(a->count + 1))) return NULL;
    p = (uint8_t *)a->data + a->count * a->element_size; a->count++; return p;
}

int hf_array_push(hf_array *a, const void *e) {
    void *p = hf_array_push_uninit(a);
    if (!p) return 0;
    e ? memcpy(p, e, a->element_size) : memset(p, 0, a->element_size);
    return 1;
}

int hf_array_pop(hf_array *a, void *out) {
    if (!a || !a->count || !a->element_size) return 0;
    a->count--;
    if (out) memcpy(out, (uint8_t *)a->data + a->count * a->element_size, a->element_size);
    return 1;
}

void *hf_array_get(hf_array *a, size_t i) { return a && i < a->count && a->element_size ? (uint8_t *)a->data + i * a->element_size : NULL; }
const void *hf_array_get_const(const hf_array *a, size_t i) { return a && i < a->count && a->element_size ? (const uint8_t *)a->data + i * a->element_size : NULL; }
void *hf_array_data(hf_array *a) { return a ? a->data : NULL; }
const void *hf_array_data_const(const hf_array *a) { return a ? a->data : NULL; }
size_t hf_array_count(const hf_array *a) { return a ? a->count : 0; }
size_t hf_array_capacity(const hf_array *a) { return a ? a->capacity : 0; }
size_t hf_array_element_size(const hf_array *a) { return a ? a->element_size : 0; }
