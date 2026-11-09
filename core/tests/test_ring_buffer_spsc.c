#include "ring_buffer_spsc.h"
#include "tinytest.h"

#include <string.h>

spec("Ring Buffer SPSC") {
  it("wraps a contiguous write when the empty ring is parked near the end") {
    uint8_t storage[16] = {0};
    ring_spsc_t ring;
    size_t available = 0;
    uint8_t *ptr;

    check(ring_spsc_init(&ring, storage, sizeof(storage)));

    ptr = ring_spsc_write_acquire(&ring, 10);
    check_not_null(ptr);
    memset(ptr, 'a', 10);
    ring_spsc_write_release(&ring, 10);

    ptr = ring_spsc_read_acquire(&ring, &available);
    check_not_null(ptr);
    check_equal(available, 10);
    ring_spsc_read_release(&ring, 10);
    check_equal(ring_spsc_read_available(&ring), 0);

    ptr = ring_spsc_write_acquire(&ring, 8);
    check_not_null(ptr);
    check_equal((const void *)(ptr), (const void *)(storage));
    memset(ptr, 'b', 8);
    ring_spsc_write_release(&ring, 8);

    check_equal(ring_spsc_read_available(&ring), 8);
    ptr = ring_spsc_read_acquire(&ring, &available);
    check_not_null(ptr);
    check_equal((const void *)(ptr), (const void *)(storage));
    check_equal(available, 8);
    check_equal(ptr, "bbbbbbbb", 8);
    ring_spsc_read_release(&ring, 8);
    check_equal(ring_spsc_read_available(&ring), 0);
  }

  it("preserves unread tail data before exposing wrapped data") {
    uint8_t storage[32] = {0};
    ring_spsc_t ring;
    size_t available = 0;
    uint8_t *ptr;

    check(ring_spsc_init(&ring, storage, sizeof(storage)));

    ptr = ring_spsc_write_acquire(&ring, 20);
    check_not_null(ptr);
    memset(ptr, 'a', 20);
    ring_spsc_write_release(&ring, 20);

    ptr = ring_spsc_read_acquire(&ring, &available);
    check_not_null(ptr);
    check_equal(available, 20);
    ring_spsc_read_release(&ring, 18);

    ptr = ring_spsc_write_acquire(&ring, 16);
    check_not_null(ptr);
    check_equal((const void *)(ptr), (const void *)(storage));
    memset(ptr, 'b', 16);
    ring_spsc_write_release(&ring, 16);

    check_equal(ring_spsc_read_available(&ring), 18);
    ptr = ring_spsc_read_acquire(&ring, &available);
    check_not_null(ptr);
    check_equal(available, 2);
    check_equal(ptr, "aa", 2);
    ring_spsc_read_release(&ring, 2);

    check_equal(ring_spsc_read_available(&ring), 16);
    ptr = ring_spsc_read_acquire(&ring, &available);
    check_not_null(ptr);
    check_equal((const void *)(ptr), (const void *)(storage));
    check_equal(available, 16);
    check_equal(ptr, "bbbbbbbbbbbbbbbb", 16);
    ring_spsc_read_release(&ring, 16);
    check_equal(ring_spsc_read_available(&ring), 0);
  }
}
