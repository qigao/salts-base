#include <deadline_queue.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define SALTS_DEADLINE_INDEX_NONE UINT32_MAX

typedef struct cmeta_deadline_entry {
  cmeta_deadline_event event;
  uint64_t order;
  uint32_t heap_index;
  uint32_t generation;
  bool active;
} cmeta_deadline_entry;

typedef struct cmeta_deadline_queue_impl {
  cmeta_deadline_entry *entries;
  uint32_t *heap;
  uint32_t *free_slots;
  size_t size;
  size_t free_count;
  size_t capacity;
  uint64_t next_order;
} cmeta_deadline_queue_impl;

static cmeta_deadline_queue_impl *cmeta_deadline_get(cmeta_deadline_queue *queue) {
  return queue != NULL ? (cmeta_deadline_queue_impl *)queue->impl : NULL;
}

static const cmeta_deadline_queue_impl *
cmeta_deadline_const_get(const cmeta_deadline_queue *queue) {
  return queue != NULL ? (const cmeta_deadline_queue_impl *)queue->impl : NULL;
}

static cmeta_deadline_id cmeta_deadline_make_id(uint32_t slot, uint32_t generation) {
  return ((uint64_t)generation << 32u) | ((uint64_t)slot + 1u);
}

static bool cmeta_deadline_decode_id(cmeta_deadline_id id, uint32_t *out_slot,
                                     uint32_t *out_generation) {
  const uint32_t encoded_slot = (uint32_t)id;
  const uint32_t generation = (uint32_t)(id >> 32u);
  if (encoded_slot == 0u || generation == 0u) return false;
  *out_slot = encoded_slot - 1u;
  *out_generation = generation;
  return true;
}

static bool cmeta_deadline_before(const cmeta_deadline_queue_impl *impl, uint32_t left_slot,
                                  uint32_t right_slot) {
  const cmeta_deadline_entry *left = &impl->entries[left_slot];
  const cmeta_deadline_entry *right = &impl->entries[right_slot];
  return left->event.deadline_ms < right->event.deadline_ms ||
         (left->event.deadline_ms == right->event.deadline_ms && left->order < right->order);
}

static void cmeta_deadline_swap(cmeta_deadline_queue_impl *impl, size_t left, size_t right) {
  const uint32_t temporary = impl->heap[left];
  impl->heap[left] = impl->heap[right];
  impl->heap[right] = temporary;
  impl->entries[impl->heap[left]].heap_index = (uint32_t)left;
  impl->entries[impl->heap[right]].heap_index = (uint32_t)right;
}

static void cmeta_deadline_sift_up(cmeta_deadline_queue_impl *impl, size_t index) {
  while (index != 0u) {
    const size_t parent = (index - 1u) / 2u;
    if (!cmeta_deadline_before(impl, impl->heap[index], impl->heap[parent])) break;
    cmeta_deadline_swap(impl, index, parent);
    index = parent;
  }
}

static void cmeta_deadline_sift_down(cmeta_deadline_queue_impl *impl, size_t index) {
  for (;;) {
    const size_t left = index * 2u + 1u;
    const size_t right = left + 1u;
    size_t earliest = index;
    if (left < impl->size && cmeta_deadline_before(impl, impl->heap[left], impl->heap[earliest]))
      earliest = left;
    if (right < impl->size && cmeta_deadline_before(impl, impl->heap[right], impl->heap[earliest]))
      earliest = right;
    if (earliest == index) break;
    cmeta_deadline_swap(impl, index, earliest);
    index = earliest;
  }
}

static void cmeta_deadline_release_slot(cmeta_deadline_queue_impl *impl, uint32_t slot) {
  cmeta_deadline_entry *entry = &impl->entries[slot];
  memset(&entry->event, 0, sizeof(entry->event));
  entry->order = 0u;
  entry->heap_index = SALTS_DEADLINE_INDEX_NONE;
  entry->active = false;
  if (entry->generation == UINT32_MAX) {
    entry->generation = 0u;
    return;
  }
  ++entry->generation;
  impl->free_slots[impl->free_count++] = slot;
}

static void cmeta_deadline_remove(cmeta_deadline_queue_impl *impl, size_t heap_index,
                                  cmeta_deadline_event *out_event) {
  const uint32_t removed_slot = impl->heap[heap_index];
  *out_event = impl->entries[removed_slot].event;
  --impl->size;
  if (heap_index != impl->size) {
    impl->heap[heap_index] = impl->heap[impl->size];
    impl->entries[impl->heap[heap_index]].heap_index = (uint32_t)heap_index;
    if (heap_index != 0u &&
        cmeta_deadline_before(impl, impl->heap[heap_index], impl->heap[(heap_index - 1u) / 2u]))
      cmeta_deadline_sift_up(impl, heap_index);
    else cmeta_deadline_sift_down(impl, heap_index);
  }
  impl->heap[impl->size] = SALTS_DEADLINE_INDEX_NONE;
  cmeta_deadline_release_slot(impl, removed_slot);
}

int cmeta_deadline_queue_init(cmeta_deadline_queue *queue, size_t capacity) {
  cmeta_deadline_queue_impl *impl;
  size_t index;
  if (queue == NULL || capacity == 0u) return SALTS_EINVAL;
  if (queue->impl != NULL) return SALTS_EALREADY;
  if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(cmeta_deadline_entry) ||
      capacity > SIZE_MAX / sizeof(uint32_t))
    return SALTS_ERANGE;
  impl = (cmeta_deadline_queue_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->entries = (cmeta_deadline_entry *)calloc(capacity, sizeof(*impl->entries));
  impl->heap = (uint32_t *)malloc(capacity * sizeof(*impl->heap));
  impl->free_slots = (uint32_t *)malloc(capacity * sizeof(*impl->free_slots));
  if (impl->entries == NULL || impl->heap == NULL || impl->free_slots == NULL) {
    free(impl->free_slots);
    free(impl->heap);
    free(impl->entries);
    free(impl);
    return SALTS_ENOMEM;
  }
  for (index = 0u; index < capacity; ++index) {
    impl->entries[index].generation = 1u;
    impl->entries[index].heap_index = SALTS_DEADLINE_INDEX_NONE;
    impl->heap[index] = SALTS_DEADLINE_INDEX_NONE;
    impl->free_slots[index] = (uint32_t)(capacity - index - 1u);
  }
  impl->capacity = capacity;
  impl->free_count = capacity;
  queue->impl = impl;
  return SALTS_OK;
}

int cmeta_deadline_queue_schedule(cmeta_deadline_queue *queue, uint64_t deadline_ms, uint64_t token,
                                  cmeta_deadline_id *out_id) {
  cmeta_deadline_queue_impl *impl = cmeta_deadline_get(queue);
  cmeta_deadline_entry *entry;
  uint32_t slot;
  size_t heap_index;
  if (out_id == NULL) return SALTS_EINVAL;
  *out_id = 0u;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->free_count == 0u) return SALTS_ENOBUFS;
  if (impl->next_order == UINT64_MAX) return SALTS_ERANGE;
  slot = impl->free_slots[--impl->free_count];
  entry = &impl->entries[slot];
  if (entry->active || entry->generation == 0u) return SALTS_EPROTO;
  heap_index = impl->size++;
  entry->event =
      (cmeta_deadline_event){cmeta_deadline_make_id(slot, entry->generation), deadline_ms, token};
  entry->order = impl->next_order++;
  entry->heap_index = (uint32_t)heap_index;
  entry->active = true;
  impl->heap[heap_index] = slot;
  *out_id = entry->event.id;
  cmeta_deadline_sift_up(impl, heap_index);
  return SALTS_OK;
}

int cmeta_deadline_queue_cancel(cmeta_deadline_queue *queue, cmeta_deadline_id id,
                                cmeta_deadline_event *out_event) {
  cmeta_deadline_queue_impl *impl = cmeta_deadline_get(queue);
  cmeta_deadline_entry *entry;
  uint32_t slot;
  uint32_t generation;
  if (out_event == NULL) return SALTS_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL || !cmeta_deadline_decode_id(id, &slot, &generation)) return SALTS_EINVAL;
  if ((size_t)slot >= impl->capacity) return SALTS_ENOENT;
  entry = &impl->entries[slot];
  if (!entry->active || entry->generation != generation) return SALTS_ENOENT;
  cmeta_deadline_remove(impl, entry->heap_index, out_event);
  return SALTS_OK;
}

int cmeta_deadline_queue_peek(const cmeta_deadline_queue *queue, cmeta_deadline_event *out_event) {
  const cmeta_deadline_queue_impl *impl = cmeta_deadline_const_get(queue);
  if (out_event == NULL) return SALTS_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->size == 0u) return SALTS_ETIMEDOUT;
  *out_event = impl->entries[impl->heap[0]].event;
  return SALTS_OK;
}

int cmeta_deadline_queue_take_ready(cmeta_deadline_queue *queue, uint64_t now_ms,
                                    cmeta_deadline_event *out_event) {
  cmeta_deadline_queue_impl *impl = cmeta_deadline_get(queue);
  if (out_event == NULL) return SALTS_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->size == 0u || impl->entries[impl->heap[0]].event.deadline_ms > now_ms)
    return SALTS_ETIMEDOUT;
  cmeta_deadline_remove(impl, 0u, out_event);
  return SALTS_OK;
}

size_t cmeta_deadline_queue_size(const cmeta_deadline_queue *queue) {
  const cmeta_deadline_queue_impl *impl = cmeta_deadline_const_get(queue);
  return impl != NULL ? impl->size : 0u;
}

int cmeta_deadline_queue_destroy(cmeta_deadline_queue *queue) {
  cmeta_deadline_queue_impl *impl = cmeta_deadline_get(queue);
  if (queue == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  free(impl->free_slots);
  free(impl->heap);
  free(impl->entries);
  free(impl);
  queue->impl = NULL;
  return SALTS_OK;
}
