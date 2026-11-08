#include <deadline_queue.h>

#include "tinytest.h"

#include <stdint.h>

spec("Concurrency deadline queue") {
  it("orders absolute deadlines and preserves FIFO ties") {
    cmeta_deadline_queue queue = {0};
    cmeta_deadline_event event = {0};
    cmeta_deadline_id first = 0u;
    cmeta_deadline_id second = 0u;
    cmeta_deadline_id third = 0u;

    check_equal(cmeta_deadline_queue_init(&queue, 3u), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 20u, 200u, &first), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 10u, 100u, &second), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 10u, 101u, &third), SALTS_OK);
    check_true(first != 0u && second != 0u && third != 0u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 9u, &event), SALTS_ETIMEDOUT);
    check_equal(event.id, 0u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 10u, &event), SALTS_OK);
    check_equal(event.id, second);
    check_equal(event.token, 100u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 10u, &event), SALTS_OK);
    check_equal(event.id, third);
    check_equal(event.token, 101u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 20u, &event), SALTS_OK);
    check_equal(event.id, first);
    check_equal(event.token, 200u);
    check_equal(cmeta_deadline_queue_size(&queue), 0u);
    check_equal(cmeta_deadline_queue_destroy(&queue), SALTS_OK);
  }

  it("rejects a stale generation after slot reuse and restores heap order") {
    cmeta_deadline_queue queue = {0};
    cmeta_deadline_event event = {0};
    cmeta_deadline_id first = 0u;
    cmeta_deadline_id second = 0u;
    cmeta_deadline_id third = 0u;
    cmeta_deadline_id replacement = 0u;

    check_equal(cmeta_deadline_queue_init(&queue, 3u), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 30u, 3u, &third), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 10u, 1u, &first), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 20u, 2u, &second), SALTS_OK);
    check_equal(cmeta_deadline_queue_cancel(&queue, first, &event), SALTS_OK);
    check_equal(event.token, 1u);
    check_equal(cmeta_deadline_queue_schedule(&queue, 5u, 4u, &replacement), SALTS_OK);
    check_not_equal(replacement, first);
    check_equal(cmeta_deadline_queue_cancel(&queue, second, &event), SALTS_OK);
    check_equal(event.token, 2u);
    check_equal(cmeta_deadline_queue_cancel(&queue, first, &event), SALTS_ENOENT);
    check_equal(event.id, 0u);
    check_equal(cmeta_deadline_queue_peek(&queue, &event), SALTS_OK);
    check_equal(event.token, 4u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 30u, &event), SALTS_OK);
    check_equal(event.token, 4u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 30u, &event), SALTS_OK);
    check_equal(event.id, third);
    check_equal(cmeta_deadline_queue_destroy(&queue), SALTS_OK);
  }

  it("maintains indexed heap invariants across cancellation and slot reuse") {
    enum { CAPACITY = 32, ROUNDS = 128 };
    cmeta_deadline_queue queue = {0};
    cmeta_deadline_id ids[CAPACITY] = {0};
    size_t round;

    check_equal(cmeta_deadline_queue_init(&queue, CAPACITY), SALTS_OK);
    for (round = 0u; round < ROUNDS; ++round) {
      cmeta_deadline_event event = {0};
      uint64_t previous_deadline = 0u;
      size_t index;
      size_t taken = 0u;

      for (index = 0u; index < CAPACITY; ++index)
        check_equal(cmeta_deadline_queue_schedule(&queue, (uint64_t)((index * 17u) % CAPACITY + 1u),
                                                  (uint64_t)index, &ids[index]),
                    SALTS_OK);
      for (index = 0u; index < CAPACITY; index += 2u) {
        check_equal(cmeta_deadline_queue_cancel(&queue, ids[index], &event), SALTS_OK);
        check_equal(event.token, (uint64_t)index);
      }
      while (cmeta_deadline_queue_take_ready(&queue, UINT64_MAX, &event) == SALTS_OK) {
        check_true(event.deadline_ms >= previous_deadline);
        check_equal(event.token % 2u, 1u);
        previous_deadline = event.deadline_ms;
        ++taken;
      }
      check_equal(taken, (size_t)(CAPACITY / 2u));
      check_equal(cmeta_deadline_queue_size(&queue), 0u);
    }
    check_equal(cmeta_deadline_queue_destroy(&queue), SALTS_OK);
  }

  it("fails fast at fixed capacity and invalid boundaries") {
    cmeta_deadline_queue queue = {0};
    cmeta_deadline_event event = {1u, 2u, 3u};
    cmeta_deadline_id id = 99u;

    check_equal(cmeta_deadline_queue_init(NULL, 1u), SALTS_EINVAL);
    check_equal(cmeta_deadline_queue_init(&queue, 0u), SALTS_EINVAL);
    check_equal(cmeta_deadline_queue_init(&queue, SIZE_MAX), SALTS_ERANGE);
    check_equal(cmeta_deadline_queue_init(&queue, 1u), SALTS_OK);
    check_equal(cmeta_deadline_queue_init(&queue, 1u), SALTS_EALREADY);
    check_equal(cmeta_deadline_queue_schedule(&queue, 1u, 7u, &id), SALTS_OK);
    check_equal(cmeta_deadline_queue_schedule(&queue, 2u, 8u, &id), SALTS_ENOBUFS);
    check_equal(id, 0u);
    check_equal(cmeta_deadline_queue_cancel(&queue, 0u, &event), SALTS_EINVAL);
    check_equal(event.id, 0u);
    check_equal(cmeta_deadline_queue_take_ready(&queue, 1u, &event), SALTS_OK);
    check_equal(cmeta_deadline_queue_peek(&queue, &event), SALTS_ETIMEDOUT);
    check_equal(event.id, 0u);
    check_equal(cmeta_deadline_queue_destroy(&queue), SALTS_OK);
    check_equal(cmeta_deadline_queue_destroy(&queue), SALTS_OK);
  }
}
