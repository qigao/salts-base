#include "bucket_priority_queue.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#define BUCKET_ENTRY_SIZE sizeof(bucket_priority_value_t)
#define BUCKET_MIN_CAPACITY 16U

static bool bucket_priority_valid(bucket_priority_t priority) {
  return priority >= BUCKET_PRIORITY_LOW && priority < BUCKET_PRIORITY_COUNT;
}

static uint8_t bucket_priority_bit(bucket_priority_t priority) {
  return (uint8_t)(1u << (uint8_t)priority);
}

static int bucket_highest_non_empty_priority(uint8_t mask) {
  if (mask == 0) return -1;
#if defined(_MSC_VER)
  unsigned long idx;
  _BitScanReverse(&idx, (unsigned long)mask);
  return (int)idx;
#elif defined(__GNUC__) || defined(__clang__)
  return 31 - __builtin_clz((unsigned int)mask);
#else
  /* Portable fallback: binary search for highest set bit */
  int bit = 0;
  uint8_t m = mask;
  if (m & 0xF0) {
    bit += 4;
    m >>= 4;
  }
  if (m & 0x0C) {
    bit += 2;
    m >>= 2;
  }
  if (m & 0x02) {
    bit += 1;
  }
  return bit;
#endif
}

static bool bucket_capacity_overflow(size_t entries) {
  if (entries == 0) {
    return false;
  }
  return entries > ((SIZE_MAX - 1U) / BUCKET_ENTRY_SIZE);
}

static bool bucket_copy_fifo_bytes(const bucket_priority_bucket_t *bucket, uint8_t *dst,
                                   size_t dst_bytes) {
  const size_t used_bytes = bucket->count * BUCKET_ENTRY_SIZE;
  if (used_bytes > dst_bytes) {
    return false;
  }
  if (used_bytes == 0) {
    return true;
  }

  ring_data_type cursor = bucket->ring;
  size_t copied = 0;

  while (copied < used_bytes) {
    size_t available = 0;
    uint8_t *read_ptr = ring_read_acquire(&cursor, &available);
    if (read_ptr == NULL || available == 0) {
      return false;
    }

    size_t take = available;
    if (take > (used_bytes - copied)) {
      take = used_bytes - copied;
    }

    memcpy(dst + copied, read_ptr, take);
    ring_read_release(&cursor, take);
    copied += take;
  }

  return copied == used_bytes;
}

static bool bucket_reserve_entries(bucket_priority_bucket_t *bucket, size_t min_entries) {
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

  const size_t new_bytes = (new_capacity * BUCKET_ENTRY_SIZE) + 1U;
  uint8_t *new_storage = (uint8_t *)malloc(new_bytes);
  if (new_storage == NULL) {
    return false;
  }

  ring_data_type new_ring;
  ring_init(&new_ring, new_storage, new_bytes);

  const size_t used_bytes = bucket->count * BUCKET_ENTRY_SIZE;
  if (!bucket_copy_fifo_bytes(bucket, new_storage, new_bytes)) {
    free(new_storage);
    return false;
  }

  if (used_bytes > 0) {
    ring_write_release(&new_ring, used_bytes);
  }

  free(bucket->storage);
  bucket->storage = new_storage;
  bucket->storage_bytes = new_bytes;
  bucket->capacity_entries = new_capacity;
  bucket->ring = new_ring;
  return true;
}

bool bucket_priority_queue_init(bucket_priority_queue_t *queue, size_t capacity_per_bucket) {
  size_t p = 0;

  if (queue == NULL) {
    return false;
  }

  memset(queue, 0, sizeof(*queue));
  if (capacity_per_bucket == 0) {
    return true;
  }

  for (p = 0; p < BUCKET_PRIORITY_COUNT; ++p) {
    if (!bucket_reserve_entries(&queue->buckets[p], capacity_per_bucket)) {
      bucket_priority_queue_destroy(queue);
      return false;
    }
  }
  return true;
}

void bucket_priority_queue_destroy(bucket_priority_queue_t *queue) {
  size_t p = 0;
  if (queue == NULL) {
    return;
  }

  for (p = 0; p < BUCKET_PRIORITY_COUNT; ++p) {
    free(queue->buckets[p].storage);
    queue->buckets[p].storage = NULL;
    queue->buckets[p].storage_bytes = 0;
    queue->buckets[p].capacity_entries = 0;
    queue->buckets[p].count = 0;
    memset(&queue->buckets[p].ring, 0, sizeof(queue->buckets[p].ring));
  }
  queue->total_size = 0;
  queue->non_empty_mask = 0;
}

void bucket_priority_queue_clear(bucket_priority_queue_t *queue) {
  size_t p = 0;
  if (queue == NULL) {
    return;
  }

  for (p = 0; p < BUCKET_PRIORITY_COUNT; ++p) {
    bucket_priority_bucket_t *bucket = &queue->buckets[p];
    if (bucket->storage != NULL && bucket->storage_bytes > 0) {
      ring_init(&bucket->ring, bucket->storage, bucket->storage_bytes);
    } else {
      memset(&bucket->ring, 0, sizeof(bucket->ring));
    }
    bucket->count = 0;
  }
  queue->total_size = 0;
  queue->non_empty_mask = 0;
}

bool bucket_priority_queue_reserve(bucket_priority_queue_t *queue, size_t capacity_per_bucket) {
  size_t p = 0;
  if (queue == NULL) {
    return false;
  }

  for (p = 0; p < BUCKET_PRIORITY_COUNT; ++p) {
    if (!bucket_reserve_entries(&queue->buckets[p], capacity_per_bucket)) {
      return false;
    }
  }
  return true;
}

bool bucket_priority_queue_push(bucket_priority_queue_t *queue, bucket_priority_t priority,
                                bucket_priority_value_t value) {
  bucket_priority_bucket_t *bucket = NULL;
  uint8_t *write_ptr = NULL;
  bool was_empty = false;

  if (queue == NULL || !bucket_priority_valid(priority)) {
    return false;
  }

  bucket = &queue->buckets[(size_t)priority];
  was_empty = bucket->count == 0;
  if (!bucket_reserve_entries(bucket, bucket->count + 1U)) {
    return false;
  }

  write_ptr = ring_write_acquire(&bucket->ring, BUCKET_ENTRY_SIZE);
  if (write_ptr == NULL) {
    if (!bucket_reserve_entries(bucket, bucket->capacity_entries + 1U)) {
      return false;
    }
    write_ptr = ring_write_acquire(&bucket->ring, BUCKET_ENTRY_SIZE);
    if (write_ptr == NULL) {
      return false;
    }
  }

  memcpy(write_ptr, &value, BUCKET_ENTRY_SIZE);
  ring_write_release(&bucket->ring, BUCKET_ENTRY_SIZE);
  bucket->count += 1U;
  queue->total_size += 1U;
  if (was_empty) {
    queue->non_empty_mask |= bucket_priority_bit(priority);
  }
  return true;
}

static bool bucket_pop_one(bucket_priority_bucket_t *bucket, bucket_priority_value_t *out_value) {
  size_t available = 0;
  uint8_t *read_ptr = NULL;

  if (bucket == NULL || out_value == NULL || bucket->count == 0) {
    return false;
  }

  read_ptr = ring_read_acquire(&bucket->ring, &available);
  if (read_ptr == NULL || available < BUCKET_ENTRY_SIZE) {
    return false;
  }

  memcpy(out_value, read_ptr, BUCKET_ENTRY_SIZE);
  ring_read_release(&bucket->ring, BUCKET_ENTRY_SIZE);
  bucket->count -= 1U;
  return true;
}

bool bucket_priority_queue_pop(bucket_priority_queue_t *queue, bucket_priority_value_t *out_value) {
  if (queue == NULL || out_value == NULL) {
    return false;
  }

  while (queue->non_empty_mask != 0) {
    int p = bucket_highest_non_empty_priority(queue->non_empty_mask);
    if (p < 0) {
      return false;
    }

    bucket_priority_bucket_t *bucket = &queue->buckets[(size_t)p];
    if (bucket->count == 0) {
      queue->non_empty_mask &= (uint8_t)~bucket_priority_bit((bucket_priority_t)p);
      continue;
    }
    if (!bucket_pop_one(bucket, out_value)) {
      return false;
    }
    queue->total_size -= 1U;
    if (bucket->count == 0) {
      queue->non_empty_mask &= (uint8_t)~bucket_priority_bit((bucket_priority_t)p);
    }
    return true;
  }
  return false;
}

static bool bucket_peek_one(const bucket_priority_bucket_t *bucket,
                            bucket_priority_value_t *out_value) {
  ring_data_type cursor;
  size_t available = 0;
  uint8_t *read_ptr = NULL;

  if (bucket == NULL || out_value == NULL || bucket->count == 0) {
    return false;
  }

  cursor = bucket->ring;
  read_ptr = ring_read_acquire(&cursor, &available);
  if (read_ptr == NULL || available < BUCKET_ENTRY_SIZE) {
    return false;
  }

  memcpy(out_value, read_ptr, BUCKET_ENTRY_SIZE);
  return true;
}

bool bucket_priority_queue_peek(const bucket_priority_queue_t *queue,
                                bucket_priority_value_t *out_value) {
  uint8_t mask = 0;

  if (queue == NULL || out_value == NULL) {
    return false;
  }

  mask = queue->non_empty_mask;
  while (mask != 0) {
    int p = bucket_highest_non_empty_priority(mask);
    if (p < 0) {
      return false;
    }

    const bucket_priority_bucket_t *bucket = &queue->buckets[(size_t)p];
    if (bucket->count == 0) {
      mask &= (uint8_t)~bucket_priority_bit((bucket_priority_t)p);
      continue;
    }
    return bucket_peek_one(bucket, out_value);
  }

  return false;
}

size_t bucket_priority_queue_pop_batch(bucket_priority_queue_t *queue, size_t max_items,
                                       bucket_priority_value_t *out_values) {
  size_t popped = 0;

  if (queue == NULL || max_items == 0) {
    return 0;
  }
  if (out_values == NULL) {
    return 0;
  }

  while (queue->non_empty_mask != 0 && popped < max_items) {
    int p = bucket_highest_non_empty_priority(queue->non_empty_mask);
    if (p < 0) {
      return popped;
    }

    bucket_priority_bucket_t *bucket = &queue->buckets[(size_t)p];
    if (bucket->count == 0) {
      queue->non_empty_mask &= (uint8_t)~bucket_priority_bit((bucket_priority_t)p);
      continue;
    }

    while (bucket->count > 0 && popped < max_items) {
      size_t available = 0;
      uint8_t *read_ptr = ring_read_acquire(&bucket->ring, &available);
      if (read_ptr == NULL || available < BUCKET_ENTRY_SIZE) {
        return popped;
      }

      size_t available_entries = available / BUCKET_ENTRY_SIZE;
      size_t remaining = max_items - popped;
      size_t take = available_entries;

      if (take > remaining) {
        take = remaining;
      }
      if (take > bucket->count) {
        take = bucket->count;
      }
      if (take == 0) {
        return popped;
      }

      memcpy(out_values + popped, read_ptr, take * BUCKET_ENTRY_SIZE);
      ring_read_release(&bucket->ring, take * BUCKET_ENTRY_SIZE);
      bucket->count -= take;
      queue->total_size -= take;
      popped += take;
    }

    if (bucket->count == 0) {
      queue->non_empty_mask &= (uint8_t)~bucket_priority_bit((bucket_priority_t)p);
    }
  }

  return popped;
}

bool bucket_priority_queue_empty(const bucket_priority_queue_t *queue) {
  if (queue == NULL) {
    return true;
  }
  return queue->total_size == 0;
}

size_t bucket_priority_queue_size(const bucket_priority_queue_t *queue) {
  if (queue == NULL) {
    return 0;
  }
  return queue->total_size;
}

size_t bucket_priority_queue_size_at(const bucket_priority_queue_t *queue,
                                     bucket_priority_t priority) {
  if (queue == NULL || !bucket_priority_valid(priority)) {
    return 0;
  }
  return queue->buckets[(size_t)priority].count;
}

size_t bucket_priority_queue_capacity_at(const bucket_priority_queue_t *queue,
                                         bucket_priority_t priority) {
  if (queue == NULL || !bucket_priority_valid(priority)) {
    return 0;
  }
  return queue->buckets[(size_t)priority].capacity_entries;
}
