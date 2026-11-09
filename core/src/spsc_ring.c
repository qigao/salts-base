#include <spsc_ring.h>

#include <assert.h>

static bool cmeta_spsc_ring_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

bool cmeta_spsc_ring_init(cmeta_spsc_ring *ring, uint8_t *storage, size_t size) {
  assert(ring != NULL);
  assert(storage != NULL);
  assert(size != 0u);
  if (!cmeta_spsc_ring_power_of_two(size)) return false;

  ring->data = storage;
  ring->size = size;
  ring->mask = size - 1u;
  atomic_store_explicit(&ring->write_pos, 0u, memory_order_relaxed);
  atomic_store_explicit(&ring->read_pos, 0u, memory_order_relaxed);
  atomic_store_explicit(&ring->wrap_pos, 0u, memory_order_relaxed);
  atomic_store_explicit(&ring->wrap_len, 0u, memory_order_relaxed);
  ring->pending_wrap_len = 0u;
  return true;
}

uint8_t *cmeta_spsc_ring_write_acquire(cmeta_spsc_ring *ring, size_t required) {
  size_t write;
  size_t read;
  size_t used;
  size_t available;
  size_t offset;
  size_t linear;

  assert(ring != NULL);
  assert(ring->data != NULL);
  assert(required != 0u);
  write = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
  read = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
  used = write - read;
  available = ring->size - used - 1u;
  offset = write & ring->mask;
  linear = ring->size - offset;
  if (required >= ring->size || required > available) return NULL;
  ring->pending_wrap_len = 0u;
  if (required > linear) {
    if (atomic_load_explicit(&ring->wrap_len, memory_order_acquire) != 0u ||
        required + linear > available)
      return NULL;
    ring->pending_wrap_len = linear;
    return ring->data;
  }
  return &ring->data[offset];
}

void cmeta_spsc_ring_write_release(cmeta_spsc_ring *ring, size_t written) {
  size_t write;
  assert(ring != NULL);
  assert(ring->data != NULL);
  write = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
  if (ring->pending_wrap_len != 0u) {
    atomic_store_explicit(&ring->wrap_pos, write, memory_order_release);
    atomic_store_explicit(&ring->wrap_len, ring->pending_wrap_len, memory_order_release);
    atomic_store_explicit(&ring->write_pos, write + ring->pending_wrap_len + written,
                          memory_order_release);
    ring->pending_wrap_len = 0u;
    return;
  }
  atomic_store_explicit(&ring->write_pos, write + written, memory_order_release);
}

uint8_t *cmeta_spsc_ring_read_acquire(cmeta_spsc_ring *ring, size_t *available) {
  size_t read;
  size_t write;
  size_t wrap_len;
  size_t wrap_pos;
  size_t data_available;
  size_t offset;
  size_t linear;

  assert(ring != NULL);
  assert(ring->data != NULL);
  assert(available != NULL);
  read = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  write = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  wrap_len = atomic_load_explicit(&ring->wrap_len, memory_order_acquire);
  wrap_pos = atomic_load_explicit(&ring->wrap_pos, memory_order_acquire);
  if (wrap_len != 0u && read == wrap_pos) {
    const size_t next_read = read + wrap_len;
    if (write < next_read) {
      *available = 0u;
      return NULL;
    }
    read = next_read;
    atomic_store_explicit(&ring->read_pos, read, memory_order_release);
    atomic_store_explicit(&ring->wrap_len, 0u, memory_order_release);
  }
  data_available = wrap_len != 0u && read < wrap_pos ? wrap_pos - read : write - read;
  if (data_available == 0u) {
    *available = 0u;
    return NULL;
  }
  offset = read & ring->mask;
  linear = ring->size - offset;
  *available = data_available < linear ? data_available : linear;
  return &ring->data[offset];
}

void cmeta_spsc_ring_read_release(cmeta_spsc_ring *ring, size_t read) {
  size_t position;
  assert(ring != NULL);
  assert(ring->data != NULL);
  position = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  atomic_store_explicit(&ring->read_pos, position + read, memory_order_release);
}

size_t cmeta_spsc_ring_write_available(const cmeta_spsc_ring *ring) {
  size_t write;
  size_t read;
  assert(ring != NULL);
  write = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
  read = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
  return ring->size - (write - read) - 1u;
}

size_t cmeta_spsc_ring_read_available(const cmeta_spsc_ring *ring) {
  size_t write;
  size_t read;
  size_t wrap_len;
  size_t wrap_pos;
  assert(ring != NULL);
  write = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  read = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  wrap_len = atomic_load_explicit(&ring->wrap_len, memory_order_acquire);
  wrap_pos = atomic_load_explicit(&ring->wrap_pos, memory_order_acquire);
  if (wrap_len != 0u && read == wrap_pos && write >= read + wrap_len)
    return write - read - wrap_len;
  if (wrap_len != 0u && read < wrap_pos && write >= wrap_pos + wrap_len)
    return (wrap_pos - read) + (write - wrap_pos - wrap_len);
  return write - read;
}
