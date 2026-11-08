/*
 * Copyright (c) 2026 Salts Project
 */

#ifndef SALTS_SERIAL_TEST_H
#define SALTS_SERIAL_TEST_H

#include "serial_internal.h"
#include "thread.h"

typedef int (*salts_serial_test_thread_create_fn)(cmeta_thread_t *thread,
                                                cmeta_thread_cb entry, void *arg);

void salts_serial_set_backend_ops_for_testing(const salts_serial_backend_ops_t *ops);
void salts_serial_test_set_handle(salts_serial_t *serial, salts_port_handle_t *handle);
void salts_serial_test_set_thread_create(salts_serial_test_thread_create_fn create);

#if defined(_WIN32) || defined(__CYGWIN__)
#include <wchar.h>
tstr salts_serial_test_wchar_to_utf8(const wchar_t *wstr);
#endif

#endif
