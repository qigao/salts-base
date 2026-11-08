#include "bucket_priority_queue_mpmc.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define BUCKET_ENTRY_SIZE sizeof(bucket_priority_mpmc_value_t)

typedef struct {
  disruptor_t *disruptor;
  disruptor_consumer_t shared_consumer;
  uint64_t next_read_sequence;
} bucket_priority_bucket_mpmc_t;

struct bucket_priority_queue_mpmc_impl_s {
  bucket_priority_bucket_mpmc_t buckets[BUCKET_PRIORITY_MPMC_COUNT];
  uint32_t max_consumers;
  cmeta_mutex_t notify_mutex;
  cmeta_cond_t notify_cond;
};

static bool bucket_priority_mpmc_valid(bucket_priority_mpmc_t priority) {
  return priority >= BUCKET_PRIORITY_MPMC_LOW && priority < BUCKET_PRIORITY_MPMC_COUNT;
}

static bool bucket_priority_queue_mpmc_pop_locked(bucket_priority_queue_mpmc_impl_t *impl,
                                                  bucket_priority_mpmc_value_t *out_value) {
  for (int i = BUCKET_PRIORITY_MPMC_CRITICAL; i >= BUCKET_PRIORITY_MPMC_LOW; --i) {
    bucket_priority_bucket_mpmc_t *bucket = &impl->buckets[i];
    disruptor_t *disruptor = bucket->disruptor;
    uint64_t seq = bucket->next_read_sequence;
    disruptor_cursor_t cursor = {.sequence = seq};

    if (disruptor == NULL) {
      continue;
    }

    if (disruptor_consumer_wait_for_nonblocking(disruptor, &cursor) != 1) {
      continue;
    }

    {
      disruptor_cursor_t read_cursor = {.sequence = seq};
      const bucket_priority_mpmc_value_t *entry =
          (const bucket_priority_mpmc_value_t *)disruptor_show_entry(disruptor, &read_cursor);

      if (entry == NULL) {
        continue;
      }

      *out_value = *entry;
      bucket->next_read_sequence = seq + 1;
      disruptor_consumer_release_entry(disruptor, &bucket->shared_consumer, &read_cursor);
      return true;
    }
  }

  return false;
}

bool bucket_priority_queue_mpmc_init(bucket_priority_queue_mpmc_t *queue,
                                     size_t capacity_per_bucket,
                                     uint32_t max_consumers) {
  bucket_priority_queue_mpmc_impl_t *impl;

  if (queue == NULL || capacity_per_bucket == 0) {
    return false;
  }

  memset(queue, 0, sizeof(*queue));
  impl = (bucket_priority_queue_mpmc_impl_t *)calloc(1, sizeof(*impl));
  if (impl == NULL) {
    return false;
  }

  impl->max_consumers = max_consumers;
  queue->impl = impl;

  disruptor_config_t config = {
    .entry_size = BUCKET_ENTRY_SIZE,
    .capacity = capacity_per_bucket,
    .consumer_capacity = 1 /* WE ONLY NEED 1 LOGICAL CONSUMER! */
  };

  for (size_t i = 0; i < BUCKET_PRIORITY_MPMC_COUNT; ++i) {
    impl->buckets[i].disruptor = disruptor_create(&config);
    if (impl->buckets[i].disruptor == NULL) {
      // Cleanup on failure
      for (size_t j = 0; j < i; ++j) {
        disruptor_destroy(impl->buckets[j].disruptor);
      }
      free(impl);
      queue->impl = NULL;
      return false;
    }

    // Register the shared logical consumer
    uint64_t initial_seq =
        disruptor_consumer_register(impl->buckets[i].disruptor, &impl->buckets[i].shared_consumer);
    impl->buckets[i].next_read_sequence = initial_seq;
  }

  cmeta_mutex_init(&impl->notify_mutex);
  cmeta_cond_init(&impl->notify_cond);

  return true;
}

void bucket_priority_queue_mpmc_destroy(bucket_priority_queue_mpmc_t *queue) {
  bucket_priority_queue_mpmc_impl_t *impl;

  if (queue == NULL) {
    return;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return;
  }

  for (size_t i = 0; i < BUCKET_PRIORITY_MPMC_COUNT; ++i) {
    if (impl->buckets[i].disruptor != NULL) {
      disruptor_destroy(impl->buckets[i].disruptor);
      impl->buckets[i].disruptor = NULL;
    }
  }
  cmeta_cond_destroy(&impl->notify_cond);
  cmeta_mutex_destroy(&impl->notify_mutex);
  free(impl);
  queue->impl = NULL;
}

bool bucket_priority_queue_mpmc_try_push(bucket_priority_queue_mpmc_t *queue,
                                         bucket_priority_mpmc_t priority,
                                         bucket_priority_mpmc_value_t value) {
  bucket_priority_queue_mpmc_impl_t *impl;
  if (queue == NULL || !bucket_priority_mpmc_valid(priority)) {
    return false;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return false;
  }

  disruptor_t *disruptor = impl->buckets[(size_t)priority].disruptor;
  disruptor_cursor_t cursor;

  if (disruptor_publisher_try_claim(disruptor, &cursor) != 1) {
    return false;  // Queue full
  }

  bucket_priority_mpmc_value_t *entry =
      (bucket_priority_mpmc_value_t *)disruptor_acquire_entry(disruptor, &cursor);
  *entry = value;

  disruptor_publisher_publish(disruptor, &cursor);

  /* Wake any blocked poppers */
  cmeta_mutex_lock(&impl->notify_mutex);
  cmeta_cond_signal(&impl->notify_cond);
  cmeta_mutex_unlock(&impl->notify_mutex);
  return true;
}

void bucket_priority_queue_mpmc_push_blocking(bucket_priority_queue_mpmc_t *queue,
                                              bucket_priority_mpmc_t priority,
                                              bucket_priority_mpmc_value_t value) {
  bucket_priority_queue_mpmc_impl_t *impl;
  if (queue == NULL || !bucket_priority_mpmc_valid(priority)) {
    return;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return;
  }

  disruptor_t *disruptor = impl->buckets[(size_t)priority].disruptor;
  disruptor_cursor_t cursor;

  bucket_priority_mpmc_value_t *entry =
      (bucket_priority_mpmc_value_t *)disruptor_publisher_next_entry_and_acquire_blocking(
          disruptor, &cursor);
  *entry = value;

  disruptor_publisher_commit_entry_blocking(disruptor, &cursor);

  /* Wake any blocked poppers */
  cmeta_mutex_lock(&impl->notify_mutex);
  cmeta_cond_signal(&impl->notify_cond);
  cmeta_mutex_unlock(&impl->notify_mutex);
}

bool bucket_priority_queue_mpmc_try_pop(
    bucket_priority_queue_mpmc_t *queue,
    bucket_priority_mpmc_value_t *out_value) {
  bucket_priority_queue_mpmc_impl_t *impl;
  if (queue == NULL || out_value == NULL) {
    return false;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return false;
  }

  cmeta_mutex_lock(&impl->notify_mutex);
  {
    bool popped = bucket_priority_queue_mpmc_pop_locked(impl, out_value);
    cmeta_mutex_unlock(&impl->notify_mutex);
    return popped;
  }
}

bool bucket_priority_queue_mpmc_pop_blocking(
    bucket_priority_queue_mpmc_t *queue,
    bucket_priority_mpmc_value_t *out_value,
    uint32_t timeout_ms) {
  bucket_priority_queue_mpmc_impl_t *impl;
  if (queue == NULL || out_value == NULL) {
    return false;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return false;
  }

  uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
  cmeta_mutex_lock(&impl->notify_mutex);
  while (!bucket_priority_queue_mpmc_pop_locked(impl, out_value)) {
    int rc = cmeta_cond_timedwait(&impl->notify_cond, &impl->notify_mutex, timeout_ns);
    if (rc != 0) {
      bool popped = bucket_priority_queue_mpmc_pop_locked(impl, out_value);
      cmeta_mutex_unlock(&impl->notify_mutex);
      return popped;
    }
  }
  cmeta_mutex_unlock(&impl->notify_mutex);
  return true;
}

bool bucket_priority_queue_mpmc_empty(const bucket_priority_queue_mpmc_t *queue) {
  bucket_priority_queue_mpmc_impl_t *impl;
  if (queue == NULL) {
    return true;
  }

  impl = queue->impl;
  if (impl == NULL) {
    return true;
  }

  /* The query is approximate with respect to concurrent publication, but
     next_read_sequence is shared with pop_locked and must use its mutex. */
  cmeta_mutex_lock(&impl->notify_mutex);
  // Check all buckets from highest to lowest priority
  for (int i = BUCKET_PRIORITY_MPMC_CRITICAL; i >= BUCKET_PRIORITY_MPMC_LOW; --i) {
    const bucket_priority_bucket_mpmc_t *bucket = &impl->buckets[i];
    disruptor_t *disruptor = bucket->disruptor;

    if (disruptor == NULL) {
      continue;
    }

    // Check if there are unconsumed entries
    // Compare next_read_sequence with the published cursor
    uint64_t next_seq = bucket->next_read_sequence;
    disruptor_cursor_t cursor = {.sequence = next_seq};

    if (disruptor_consumer_wait_for_nonblocking(disruptor, &cursor) == 1) {
      cmeta_mutex_unlock(&impl->notify_mutex);
      return false;  // Found data in this bucket
    }
  }

  cmeta_mutex_unlock(&impl->notify_mutex);
  return true;  // All buckets are empty
}
