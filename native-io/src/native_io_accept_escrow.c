#include "native_io_accept_escrow.h"

#include <salts/error_codes.h>

#include <stdlib.h>
#include <string.h>

static bool request_equal(
    native_io_request left,
    native_io_request right) {
    return left.slot == right.slot &&
           left.generation == right.generation;
}

static native_io_accept_escrow_entry *find_entry(
    native_io_accept_escrow *escrow,
    native_io_request request) {
    size_t index;

    if (escrow == NULL || escrow->entries == NULL)
        return NULL;

    for (index = 0u; index < escrow->capacity; ++index) {
        native_io_accept_escrow_entry *entry =
            &escrow->entries[index];
        if (entry->live &&
            request_equal(entry->request, request))
            return entry;
    }
    return NULL;
}

static native_io_accept_escrow_entry *find_free(
    native_io_accept_escrow *escrow) {
    size_t index;

    if (escrow == NULL || escrow->entries == NULL)
        return NULL;

    for (index = 0u; index < escrow->capacity; ++index) {
        if (!escrow->entries[index].live)
            return &escrow->entries[index];
    }
    return NULL;
}

static void clear_entry(
    native_io_accept_escrow *escrow,
    native_io_accept_escrow_entry *entry) {
    if (escrow == NULL || entry == NULL || !entry->live)
        return;

    memset(entry, 0, sizeof(*entry));
    if (escrow->live_count != 0u)
        --escrow->live_count;
}

int native_io_accept_escrow_init(
    native_io_accept_escrow *escrow,
    size_t capacity,
    native_io_accept_escrow_retire_fn retire,
    void *retire_context) {
    if (escrow == NULL || capacity == 0u ||
        retire == NULL || escrow->entries != NULL)
        return SALTS_EINVAL;
    if (capacity > SIZE_MAX / sizeof(*escrow->entries))
        return SALTS_ERANGE;

    escrow->entries = (native_io_accept_escrow_entry *)
        calloc(capacity, sizeof(*escrow->entries));
    if (escrow->entries == NULL)
        return SALTS_ENOMEM;

    escrow->capacity = capacity;
    escrow->live_count = 0u;
    escrow->retire = retire;
    escrow->retire_context = retire_context;
    return SALTS_OK;
}

int native_io_accept_escrow_publish(
    native_io_accept_escrow *escrow,
    native_io_request request,
    uintptr_t transport) {
    native_io_accept_escrow_entry *entry;

    if (escrow == NULL || escrow->entries == NULL ||
        escrow->retire == NULL ||
        !native_io_request_valid(request))
        return SALTS_EINVAL;
    if (find_entry(escrow, request) != NULL)
        return SALTS_EALREADY;
    if (escrow->live_count >= escrow->capacity)
        return SALTS_ENOBUFS;

    entry = find_free(escrow);
    if (entry == NULL)
        return SALTS_EPROTO;

    entry->request = request;
    entry->transport = transport;
    entry->live = true;
    ++escrow->live_count;
    return SALTS_OK;
}

int native_io_accept_escrow_take(
    native_io_accept_escrow *escrow,
    native_io_request request,
    uintptr_t *out_transport) {
    native_io_accept_escrow_entry *entry;

    if (escrow == NULL || out_transport == NULL ||
        !native_io_request_valid(request))
        return SALTS_EINVAL;

    entry = find_entry(escrow, request);
    if (entry == NULL)
        return SALTS_ENOENT;

    *out_transport = entry->transport;
    clear_entry(escrow, entry);
    return SALTS_OK;
}

int native_io_accept_escrow_discard(
    native_io_accept_escrow *escrow,
    native_io_request request) {
    native_io_accept_escrow_entry *entry;
    uintptr_t transport;
    int status;

    if (escrow == NULL || escrow->retire == NULL ||
        !native_io_request_valid(request))
        return SALTS_EINVAL;

    entry = find_entry(escrow, request);
    if (entry == NULL)
        return SALTS_ENOENT;

    transport = entry->transport;
    clear_entry(escrow, entry);
    status = escrow->retire(
        escrow->retire_context, transport);
    return status;
}

int native_io_accept_escrow_destroy(
    native_io_accept_escrow *escrow) {
    int first_status = SALTS_OK;
    size_t index;

    if (escrow == NULL)
        return SALTS_EINVAL;
    if (escrow->entries == NULL) {
        memset(escrow, 0, sizeof(*escrow));
        return SALTS_OK;
    }
    if (escrow->retire == NULL)
        return SALTS_EINVAL;

    for (index = 0u; index < escrow->capacity; ++index) {
        native_io_accept_escrow_entry *entry =
            &escrow->entries[index];
        int status;

        if (!entry->live)
            continue;

        status = escrow->retire(
            escrow->retire_context, entry->transport);
        if (first_status == SALTS_OK && status != SALTS_OK)
            first_status = status;
        memset(entry, 0, sizeof(*entry));
    }

    free(escrow->entries);
    memset(escrow, 0, sizeof(*escrow));
    return first_status;
}
