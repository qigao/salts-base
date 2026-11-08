#include "bucket_priority_queue_spsc.h"

#include <stdlib.h>
#include <string.h>

#define BUCKET_ENTRY_SIZE sizeof(bucket_priority_spsc_value_t)
#define BUCKET_MIN_CAPACITY 16U

static bool bucket_priority_spsc_valid(bucket_priority_spsc_t priority) {
  return priority >= BUCKET_PRIORITY_SPSC_LOW && priority < BUCKET_PRIORITY_SPSC_COUNT;
}

static bool bucket_capacity_overflow(size_t entries) {
  if (entries == 0) {
    return false;
  }
  return entries > ((SIZE_MAX - 1U) / BUCKET_ENTRY_SIZE);
}

static bool is_power_of_two(size_t n) {
  return n > 0 && (n & (n - 1)) == 0;
}

static bool bucket_reserve_entries_spsc(bucket_priority_bucket_spsc_t *bucket, size_t min_entries) {
  if (min_entries <= bucket->capacity_entries) {
    return true;
  }

  size_t new_capacity = bucket->capacity_entries;
  if (new_capacity == 0) {
    new_capacity = BUCKET_MIN_CAPACITY;
  }

  while (new_capacity < min_entries) {
    if (new_capacity > (SIZE_MAX / 2U)) {
      return false;
    }
    new_capacity *= 2U;
  }

  if (bucket_capacity_overflow(new_capacity)) {
    return false;
  }

  const size_t new_bytes = new_capacity * BUCKET_ENTRY_SIZE;

  if (!is_power_of_two(new_bytes)) {
    return false;
  }

  uint8_t *new_storage = (uint8_t *)malloc(new_bytes);
  if (new_storage == NULL) {
    return false;
  }

  ring_spsc_t new_ring;
  if (!ring_spsc_init(&new_ring, new_storage, new_bytes)) {
    free(new_storage);
    return false;
  }

  // Copy existing data if any
  if (bucket->storage != NULL) {
    size_t available = 0;
    uint8_t *read_ptr = ring_spsc_read_acquire(&bucket->ring, &available);

    while (read_ptr != NULL && available >= BUCKET_ENTRY_SIZE) {
      size_t entries_available = available / BUCKET_ENTRY_SIZE;

      for (size_t i = 0; i < entries_available; ++i) {
        uint8_t *write_ptr = ring_spsc_write_acquire(&new_ring, BUCKET_ENTRY_SIZE);
        if (write_ptr == NULL) {
          free(new_storage);
          return false;
        }
        memcpy(write_ptr, read_ptr + i * BUCKET_ENTRY_SIZE, BUCKET_ENTRY_SIZE);
        ring_spsc_write_release(&new_ring, BUCKET_ENTRY_SIZE);
      }

      ring_spsc_read_release(&bucket->ring, entries_available * BUCKET_ENTRY_SIZE);
      read_ptr = ring_spsc_read_acquire(&bucket->ring, &available);
    }

    free(bucket->storage);
  }

  bucket->storage = new_storage;
  bucket->storage_bytes = new_bytes;
  bucket->capacity_entries = new_capacity;
  bucket->ring = new_ring;
  return true;
}

bool bucket_priority_queue_spsc_init(bucket_priority_queue_spsc_t *queue,
                                     size_t capacity_per_bucket) {
  if (queue == NULL) {
    return false;
  }

  memset(queue, 0, sizeof(*queue));

  if (capacity_per_bucket == 0) {
    capacity_per_bucket = BUCKET_MIN_CAPACITY;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    if (!bucket_reserve_entries_spsc(&queue->buckets[p], capacity_per_bucket)) {
      bucket_priority_queue_spsc_destroy(queue);
      return false;
    }
  }
  return true;
}

void bucket_priority_queue_spsc_destroy(bucket_priority_queue_spsc_t *queue) {
  if (queue == NULL) {
    return;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    free(queue->buckets[p].storage);
    queue->buckets[p].storage = NULL;
    queue->buckets[p].storage_bytes = 0;
    queue->buckets[p].capacity_entries = 0;
    memset(&queue->buckets[p].ring, 0, sizeof(queue->buckets[p].ring));
  }
}

void bucket_priority_queue_spsc_clear(bucket_priority_queue_spsc_t *queue) {
  if (queue == NULL) {
    return;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage != NULL && bucket->storage_bytes > 0) {
      ring_spsc_init(&bucket->ring, bucket->storage, bucket->storage_bytes);
    } else {
      memset(&bucket->ring, 0, sizeof(bucket->ring));
    }
  }
}

bool bucket_priority_queue_spsc_reserve(bucket_priority_queue_spsc_t *queue,
                                        size_t capacity_per_bucket) {
  if (queue == NULL) {
    return false;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    if (!bucket_reserve_entries_spsc(&queue->buckets[p], capacity_per_bucket)) {
      return false;
    }
  }
  return true;
}

bool bucket_priority_queue_spsc_push(bucket_priority_queue_spsc_t *queue,
                                     bucket_priority_spsc_t priority,
                                     bucket_priority_spsc_value_t value) {
  if (queue == NULL || !bucket_priority_spsc_valid(priority)) {
    return false;
  }

  bucket_priority_bucket_spsc_t *bucket = &queue->buckets[(size_t)priority];

  if (bucket->storage == NULL || bucket->capacity_entries == 0) {
    return false;
  }

  uint8_t *write_ptr = ring_spsc_write_acquire(&bucket->ring, BUCKET_ENTRY_SIZE);
  if (write_ptr == NULL) {
    // Queue full or needs wrapping - cannot expand in SPSC mode
    return false;
  }

  memcpy(write_ptr, &value, BUCKET_ENTRY_SIZE);
  ring_spsc_write_release(&bucket->ring, BUCKET_ENTRY_SIZE);
  return true;
}

bool bucket_priority_queue_spsc_pop(bucket_priority_queue_spsc_t *queue,
                                    bucket_priority_spsc_value_t *out_value) {
  if (queue == NULL || out_value == NULL) {
    return false;
  }

  // Poll from highest priority first (CRITICAL -> LOW)
  for (int p = BUCKET_PRIORITY_SPSC_CRITICAL; p >= BUCKET_PRIORITY_SPSC_LOW; --p) {
    bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage == NULL || bucket->capacity_entries == 0) {
      continue;
    }

    size_t available = 0;
    uint8_t *read_ptr = ring_spsc_read_acquire(&bucket->ring, &available);

    if (read_ptr != NULL && available >= BUCKET_ENTRY_SIZE) {
      memcpy(out_value, read_ptr, BUCKET_ENTRY_SIZE);
      ring_spsc_read_release(&bucket->ring, BUCKET_ENTRY_SIZE);
      return true;
    }
  }

  return false;
}

bool bucket_priority_queue_spsc_peek(const bucket_priority_queue_spsc_t *queue,
                                     bucket_priority_spsc_value_t *out_value) {
  if (queue == NULL || out_value == NULL) {
    return false;
  }

  // Poll from highest priority first
  for (int p = BUCKET_PRIORITY_SPSC_CRITICAL; p >= BUCKET_PRIORITY_SPSC_LOW; --p) {
    const bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage == NULL || bucket->capacity_entries == 0) {
      continue;
    }

    size_t available = 0;
    uint8_t *read_ptr = ring_spsc_read_acquire((ring_spsc_t *)&bucket->ring, &available);

    if (read_ptr != NULL && available >= BUCKET_ENTRY_SIZE) {
      memcpy(out_value, read_ptr, BUCKET_ENTRY_SIZE);
      return true;
    }
  }

  return false;
}

size_t bucket_priority_queue_spsc_pop_batch(bucket_priority_queue_spsc_t *queue,
                                            size_t max_items,
                                            bucket_priority_spsc_value_t *out_values) {
  size_t popped = 0;

  if (queue == NULL || max_items == 0 || out_values == NULL) {
    return 0;
  }

  // Poll from highest priority first
  for (int p = BUCKET_PRIORITY_SPSC_CRITICAL; p >= BUCKET_PRIORITY_SPSC_LOW && popped < max_items; --p) {
    bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage == NULL || bucket->capacity_entries == 0) {
      continue;
    }

    while (popped < max_items) {
      size_t available = 0;
      uint8_t *read_ptr = ring_spsc_read_acquire(&bucket->ring, &available);

      if (read_ptr == NULL || available < BUCKET_ENTRY_SIZE) {
        break;
      }

      size_t available_entries = available / BUCKET_ENTRY_SIZE;
      size_t remaining = max_items - popped;
      size_t take = available_entries < remaining ? available_entries : remaining;

      memcpy(out_values + popped, read_ptr, take * BUCKET_ENTRY_SIZE);
      ring_spsc_read_release(&bucket->ring, take * BUCKET_ENTRY_SIZE);
      popped += take;
    }
  }

  return popped;
}

bool bucket_priority_queue_spsc_empty(const bucket_priority_queue_spsc_t *queue) {
  if (queue == NULL) {
    return true;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    const bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage == NULL || bucket->capacity_entries == 0) {
      continue;
    }
    if (ring_spsc_read_available(&bucket->ring) >= BUCKET_ENTRY_SIZE) {
      return false;
    }
  }
  return true;
}

size_t bucket_priority_queue_spsc_size(const bucket_priority_queue_spsc_t *queue) {
  size_t total = 0;

  if (queue == NULL) {
    return 0;
  }

  for (size_t p = 0; p < BUCKET_PRIORITY_SPSC_COUNT; ++p) {
    const bucket_priority_bucket_spsc_t *bucket = &queue->buckets[p];
    if (bucket->storage == NULL || bucket->capacity_entries == 0) {
      continue;
    }
    total += ring_spsc_read_available(&bucket->ring) / BUCKET_ENTRY_SIZE;
  }
  return total;
}

size_t bucket_priority_queue_spsc_size_at(const bucket_priority_queue_spsc_t *queue,
                                          bucket_priority_spsc_t priority) {
  const bucket_priority_bucket_spsc_t *bucket;

  if (queue == NULL || !bucket_priority_spsc_valid(priority)) {
    return 0;
  }

  bucket = &queue->buckets[(size_t)priority];
  if (bucket->storage == NULL || bucket->capacity_entries == 0) {
    return 0;
  }
  return ring_spsc_read_available(&bucket->ring) / BUCKET_ENTRY_SIZE;
}

size_t bucket_priority_queue_spsc_capacity_at(const bucket_priority_queue_spsc_t *queue,
                                              bucket_priority_spsc_t priority) {
  if (queue == NULL || !bucket_priority_spsc_valid(priority)) {
    return 0;
  }
  return queue->buckets[(size_t)priority].capacity_entries;
}
