/*
 * Copyright (c) 2026 Salts Project
 */

#ifndef SALTS_SERIAL_TEST_H
#define SALTS_SERIAL_TEST_H

#include "serial_internal.h"
#include <threads/threads.h>

typedef int (*serial_test_thread_create_fn)(sync_thread_t *thread,
                                                sync_thread_cb entry, void *arg);

void serial_set_backend_ops_for_testing(const serial_backend_ops_t *ops);
void serial_test_set_handle(serial_t *serial, serial_native_handle_t *handle);
void serial_test_set_thread_create(serial_test_thread_create_fn create);

#if defined(_WIN32) || defined(__CYGWIN__)
#include <wchar.h>
tstr serial_test_wchar_to_utf8(const wchar_t *wstr);
#endif

#endif
