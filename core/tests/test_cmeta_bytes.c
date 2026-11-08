#include "cmeta_bytes.h"

#include "tinytest.h"
#include <error_codes.h>

#include <stdint.h>
#include <string.h>

spec("bounded byte buffer") {
  it("initializes lazily and exposes an empty view") {
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t view = {(const uint8_t *)1, 1u};

    check_equal(cmeta_bytes_init(&buffer, 1024u), SALTS_OK);
    check_equal(cmeta_bytes_size(&buffer), 0u);
    check_equal(cmeta_bytes_available(&buffer), 1024u);
    check_equal(cmeta_bytes_capacity(&buffer), 0u);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_null(view.data);
    check_equal(view.size, 0u);
    cmeta_bytes_destroy(&buffer);
  }

  it("reassembles binary chunks in one contiguous view") {
    static const uint8_t first[] = {'a', 0u, 'b'};
    static const uint8_t second[] = {'c', 'd'};
    static const uint8_t expected[] = {'a', 0u, 'b', 'c', 'd'};
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t view;

    check_equal(cmeta_bytes_init(&buffer, sizeof(expected)), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, first, sizeof(first)), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, second, sizeof(second)), SALTS_OK);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_equal(view.size, sizeof(expected));
    check_equal(view.data, expected, sizeof(expected));
    check_less_equal(cmeta_bytes_capacity(&buffer), sizeof(expected));
    cmeta_bytes_destroy(&buffer);
  }

  it("preserves unread bytes when quota or consume validation fails") {
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t before;
    cmeta_bytes_view_t after;

    check_equal(cmeta_bytes_init(&buffer, 5u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, "1234", 4u), SALTS_OK);
    check_equal(cmeta_bytes_view(&buffer, &before), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, "56", 2u), SALTS_ENOSPC);
    check_equal(cmeta_bytes_consume(&buffer, 5u), SALTS_ERANGE);
    check_equal(cmeta_bytes_view(&buffer, &after), SALTS_OK);
    check_equal(after.size, before.size);
    check_equal(after.data, before.data, before.size);
    check_less_equal(cmeta_bytes_capacity(&buffer), 5u);
    cmeta_bytes_destroy(&buffer);
  }

  it("defers compaction until append needs contiguous tail space") {
    uint8_t source[256];
    uint8_t suffix[128];
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t view;
    uint8_t *allocation;
    size_t i;

    for (i = 0u; i < sizeof(source); ++i)
      source[i] = (uint8_t)i;
    memset(suffix, 0xa5, sizeof(suffix));
    check_equal(cmeta_bytes_init(&buffer, 512u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, source, sizeof(source)), SALTS_OK);
    allocation = buffer.data;
    check_equal(cmeta_bytes_consume(&buffer, 192u), SALTS_OK);
    check_equal((const void *)(buffer.data), (const void *)(allocation));
    check_equal(buffer.read_pos, 192u);

    check_equal(cmeta_bytes_append(&buffer, suffix, sizeof(suffix)), SALTS_OK);
    check_equal((const void *)(buffer.data), (const void *)(allocation));
    check_equal(buffer.read_pos, 0u);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_equal(view.size, 192u);
    check_equal(view.data, source + 192u, 64u);
    check_equal(view.data + 64u, suffix, sizeof(suffix));
    cmeta_bytes_destroy(&buffer);
  }

  it("supports appending from its unread view across growth") {
    uint8_t source[200];
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t view;
    size_t i;

    for (i = 0u; i < sizeof(source); ++i)
      source[i] = (uint8_t)(i ^ 0x5au);
    check_equal(cmeta_bytes_init(&buffer, 1024u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, source, sizeof(source)), SALTS_OK);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, view.data + 50u, 150u), SALTS_OK);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_equal(view.size, 350u);
    check_equal(view.data, source, sizeof(source));
    check_equal(view.data + sizeof(source), source + 50u, 150u);
    cmeta_bytes_destroy(&buffer);
  }

  it("rejects overlap outside the unread range without mutation") {
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t view;
    const uint8_t *consumed;

    check_equal(cmeta_bytes_init(&buffer, 64u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, "abcdef", 6u), SALTS_OK);
    consumed = buffer.data;
    check_equal(cmeta_bytes_consume(&buffer, 3u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, consumed, 1u), SALTS_EINVAL);
    check_equal(cmeta_bytes_view(&buffer, &view), SALTS_OK);
    check_equal(view.size, 3u);
    check_equal(view.data, "def", 3u);
    cmeta_bytes_destroy(&buffer);
  }

  it("reset reuses capacity and destroy clears the object") {
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    size_t capacity;

    check_equal(cmeta_bytes_init(&buffer, 1024u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, "payload", 7u), SALTS_OK);
    capacity = cmeta_bytes_capacity(&buffer);
    cmeta_bytes_reset(&buffer);
    check_equal(cmeta_bytes_size(&buffer), 0u);
    check_equal(cmeta_bytes_capacity(&buffer), capacity);
    cmeta_bytes_destroy(&buffer);
    check_null(buffer.data);
    check_equal(buffer.max_bytes, 0u);
    check_equal(cmeta_bytes_capacity(&buffer), 0u);
  }

  it("reports invalid arguments without changing output views") {
    cmeta_bytes_t buffer = cmeta_bytes_INIT;
    cmeta_bytes_view_t out = {(const uint8_t *)1, 7u};

    check_equal(cmeta_bytes_init(NULL, 1u), SALTS_EINVAL);
    check_equal(cmeta_bytes_init(&buffer, 0u), SALTS_EINVAL);
    check_equal(cmeta_bytes_append(&buffer, "x", 1u), SALTS_EINVAL);
    check_equal(cmeta_bytes_view(&buffer, &out), SALTS_EINVAL);
    check_equal((const void *)(out.data), (const void *)((const uint8_t *)1));
    check_equal(out.size, 7u);

    check_equal(cmeta_bytes_init(&buffer, 8u), SALTS_OK);
    check_equal(cmeta_bytes_append(&buffer, NULL, 1u), SALTS_EINVAL);
    check_equal(cmeta_bytes_append(&buffer, NULL, 0u), SALTS_OK);
    check_equal(cmeta_bytes_view(&buffer, NULL), SALTS_EINVAL);
    cmeta_bytes_destroy(&buffer);
  }
}
