#include "ring_buffer.h"

#include <assert.h>

#ifndef MIN
  #define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

static size_t calc_free(size_t w, size_t r, size_t size);

void ring_init(ring_data_type *inst, uint8_t *data_array, const size_t size) {
  assert(inst != NULL);
  assert(data_array != NULL);
  assert(size != 0U);

  inst->data = data_array;
  inst->size = size;
  inst->r = 0U;
  inst->w = 0U;
  inst->i = 0U;
  inst->write_wrapped = false;
  inst->read_wrapped = false;
}

uint8_t *ring_write_acquire(ring_data_type *inst, const size_t free_required) {
  assert(inst != NULL);
  assert(inst->data != NULL);

  /* Load variables (no atomic operations needed in single-threaded) */
  const size_t w = inst->w;
  const size_t r = inst->r;
  const size_t size = inst->size;

  const size_t free = calc_free(w, r, size);
  const size_t linear_space = size - w;
  const size_t linear_free = MIN(free, linear_space);

  /* Try to find enough linear space until the end of the buffer */
  if (free_required <= linear_free) {
    return &inst->data[w];
  }

  /* If that doesn't work try from the beginning of the buffer */
  if (free_required <= free - linear_free) {
    inst->write_wrapped = true;
    return &inst->data[0];
  }

  /* Could not find free linear space with required size */
  return NULL;
}

void ring_write_release(ring_data_type *inst, const size_t written) {
  assert(inst != NULL);
  assert(inst->data != NULL);

  size_t w = inst->w;

  /* If the write wrapped set the invalidate index and reset write index*/
  size_t i;
  if (inst->write_wrapped) {
    inst->write_wrapped = false;
    i = w;
    w = 0U;
  } else {
    i = inst->i;
  }

  /* Increment the write index */
  assert(w + written <= inst->size);
  w += written;

  /* If we wrote over invalidated parts of the buffer move the invalidate
   * index
   */
  if (w > i) {
    i = w;
  }

  /* Wrap the write index if we reached the end of the buffer */
  if (w == inst->size) {
    w = 0U;
  }

  /* Store the indexes (no atomic operations needed) */
  inst->i = i;
  inst->w = w;
}

uint8_t *ring_read_acquire(ring_data_type *inst, size_t *available) {
  assert(inst != NULL);
  assert(inst->data != NULL);
  assert(available != NULL);

  /* Load variables (no atomic operations needed) */
  const size_t r = inst->r;
  const size_t w = inst->w;

  /* When read and write indexes are equal, the buffer is empty */
  if (r == w) {
    *available = 0;
    return NULL;
  }

  /* Simplest case, read index is behind the write index */
  if (r < w) {
    *available = w - r;
    return &inst->data[r];
  }

  /* Read index reached the invalidate index, make the read wrap */
  const size_t i = inst->i;
  if (r == i) {
    inst->read_wrapped = true;
    *available = w;
    return &inst->data[0];
  }

  /* There is some data until the invalidate index */
  *available = i - r;
  return &inst->data[r];
}

void ring_read_release(ring_data_type *inst, const size_t read) {
  assert(inst != NULL);
  assert(inst->data != NULL);

  /* If the read wrapped, overflow the read index */
  size_t r;
  if (inst->read_wrapped) {
    inst->read_wrapped = false;
    r = 0U;
  } else {
    r = inst->r;
  }

  /* Increment the read index and wrap to 0 if needed */
  r += read;
  if (r == inst->size) {
    r = 0U;
  }

  /* Store the index (no atomic operations needed) */
  inst->r = r;
}

/********************* PRIVATE FUNCTIONS **********************/

static size_t calc_free(const size_t w, const size_t r, const size_t size) {
  if (r > w) {
    return (r - w) - 1U;
  } else {
    return (size - (w - r)) - 1U;
  }
}
