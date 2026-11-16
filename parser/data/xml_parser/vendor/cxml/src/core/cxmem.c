/*
 * Copyright © 2021 Jeremiah Ikosin
 * Distributed under the terms of the MIT license.
 */

#include "core/cxmem.h"

#include <stdint.h>

#define _CXML_FATAL_ERROR "CXMLFatalError... Not enough memory.\n"
#define CXML_FAILURE_MESSAGE_CAPACITY 256u

#if defined(_MSC_VER)
#define CXML_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define CXML_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__)
#define CXML_THREAD_LOCAL __thread
#else
#error "cxml failure handling requires thread-local storage"
#endif

static CXML_THREAD_LOCAL cxml_failure_handler cxml_current_failure_handler;
static CXML_THREAD_LOCAL void *cxml_current_failure_user;
static CXML_THREAD_LOCAL size_t cxml_allocations_before_failure = SIZE_MAX;
static CXML_THREAD_LOCAL size_t cxml_outstanding_allocations;

void cxml_set_failure_handler(cxml_failure_handler handler, void *user) {
    cxml_current_failure_handler = handler;
    cxml_current_failure_user = user;
}

void cxml_test_fail_allocation_after(size_t successful_allocations) {
    cxml_allocations_before_failure = successful_allocations;
}

void cxml_test_clear_allocation_failure(void) {
    cxml_allocations_before_failure = SIZE_MAX;
}

size_t cxml_test_outstanding_allocations(void) {
    return cxml_outstanding_allocations;
}

static bool cxml_should_fail_allocation(void) {
    if (cxml_allocations_before_failure == SIZE_MAX) return false;
    if (cxml_allocations_before_failure == 0u) return true;
    --cxml_allocations_before_failure;
    return false;
}

_CX_ATR_NORETURN static void cxml_raise_failure_v(
    cxml_failure_kind kind, const char *fmt, va_list args) {
    char message[CXML_FAILURE_MESSAGE_CAPACITY];
    (void)vsnprintf(message, sizeof(message), fmt, args);
    if (cxml_current_failure_handler != NULL) {
        cxml_current_failure_handler(kind, message, cxml_current_failure_user);
    }
    fputs(message, stderr);
    exit(EXIT_FAILURE);
}

_CX_ATR_NORETURN void cxml_error(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    cxml_raise_failure_v(CXML_FAILURE_FATAL, fmt, args);
}

void *_cxml_allocate_r(size_t len, char *fmt, ...) {
    void *ptr = cxml_should_fail_allocation() ? NULL : malloc(len);
    if (ptr == NULL) {
        va_list args;
        va_start(args, fmt);
        cxml_raise_failure_v(CXML_FAILURE_ALLOCATION, fmt, args);
    }
    ++cxml_outstanding_allocations;
    return ptr;
}

void *_cxml_callocate_r(size_t nitems, size_t size, char *fmt, ...) {
    void *ptr = cxml_should_fail_allocation() ? NULL : calloc(nitems, size);
    if (ptr == NULL) {
        va_list args;
        va_start(args, fmt);
        cxml_raise_failure_v(CXML_FAILURE_ALLOCATION, fmt, args);
    }
    ++cxml_outstanding_allocations;
    return ptr;
}

void *_cxml_rallocate_r(void *ptr, size_t len, char *fmt, ...) {
    void *new_ptr = cxml_should_fail_allocation() ? NULL : realloc(ptr, len);
    if (new_ptr == NULL) {
        va_list args;
        va_start(args, fmt);
        cxml_raise_failure_v(CXML_FAILURE_ALLOCATION, fmt, args);
    }
    if (ptr == NULL) ++cxml_outstanding_allocations;
    return new_ptr;
}

void _cxml_free(void *ptr) {
    if (ptr == NULL) return;
    if (cxml_outstanding_allocations != 0u) --cxml_outstanding_allocations;
    free(ptr);
}

void *_cxml_allocate(size_t len) {
    return _cxml_allocate_r(len, _CXML_FATAL_ERROR);
}

void *_cxml_callocate(size_t nitems, size_t size) {
    return _cxml_callocate_r(nitems, size, _CXML_FATAL_ERROR);
}

void *_cxml_rallocate(void *ptr, size_t len) {
    return _cxml_rallocate_r(ptr, len, _CXML_FATAL_ERROR);
}
