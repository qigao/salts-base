#ifndef SALTS_NATIVE_IO_ACCEPT_ESCROW_H
#define SALTS_NATIVE_IO_ACCEPT_ESCROW_H

#include <native_io.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*native_io_accept_escrow_retire_fn)(
    void *context,
    uintptr_t transport);

typedef struct native_io_accept_escrow_entry {
    native_io_request request;
    uintptr_t transport;
    bool live;
} native_io_accept_escrow_entry;

typedef struct native_io_accept_escrow {
    native_io_accept_escrow_entry *entries;
    size_t capacity;
    size_t live_count;
    native_io_accept_escrow_retire_fn retire;
    void *retire_context;
} native_io_accept_escrow;

int native_io_accept_escrow_init(
    native_io_accept_escrow *escrow,
    size_t capacity,
    native_io_accept_escrow_retire_fn retire,
    void *retire_context);

int native_io_accept_escrow_publish(
    native_io_accept_escrow *escrow,
    native_io_request request,
    uintptr_t transport);

int native_io_accept_escrow_take(
    native_io_accept_escrow *escrow,
    native_io_request request,
    uintptr_t *out_transport);

int native_io_accept_escrow_discard(
    native_io_accept_escrow *escrow,
    native_io_request request);

int native_io_accept_escrow_destroy(
    native_io_accept_escrow *escrow);

#endif /* SALTS_NATIVE_IO_ACCEPT_ESCROW_H */
