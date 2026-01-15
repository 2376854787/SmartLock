#include <stddef.h>
#include "assert_cus.h"

void *__wrap_malloc(size_t n) {
    (void)n;
    ASSERT_FATAL(!"malloc is forbidden");
    return NULL;
}

void __wrap_free(void *p) {
    (void)p;
    ASSERT_FATAL(!"free is forbidden");
}

void *__wrap_realloc(void *p, size_t n) {
    (void)p; (void)n;
    ASSERT_FATAL(!"realloc is forbidden");
    return NULL;
}

void *__wrap_calloc(size_t a, size_t b) {
    (void)a; (void)b;
    ASSERT_FATAL(!"calloc is forbidden");
    return NULL;
}

/* alloca 有时是 builtin，不一定会走符号；wrap 只覆盖符号版本 */
void *__wrap_alloca(size_t n) {
    (void)n;
    ASSERT_FATAL(!"alloca is forbidden");
    return NULL;
}
