#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <unicode.h>
#include <tinytest.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/* The unchanged scalar/property API is the oracle for counted queries. */
static void check_queries(vstr input) {
  size_t cursor = 0u, first = input.len, last = input.len;
  int saw_content = 0, identifier = input.len != 0u;
  while (cursor < input.len) {
    unicode_scalar scalar;
    const size_t start = cursor;
    check_equal(unicode_utf8_next(input, &cursor, &scalar), SALTS_UNICODE_OK);
    if ((scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) == 0u) {
      if (!saw_content) first = start;
      last = cursor;
      saw_content = 1;
    }
    const uint32_t property = start == 0u ? SALTS_UNICODE_PROPERTY_XID_START :
                                          SALTS_UNICODE_PROPERTY_XID_CONTINUE;
    if ((scalar.properties & property) == 0u) identifier = 0;
  }
  vstr trimmed = {0};
  int actual_identifier = -1;
  check_equal(unicode_trim_whitespace(input, &trimmed), SALTS_UNICODE_OK);
  check(trimmed.data == (input.data != NULL ? input.data + first : NULL));
  check_equal(trimmed.len, last - first);
  check_equal(unicode_is_xid(input, &actual_identifier), SALTS_UNICODE_OK);
  check_equal(actual_identifier, identifier);
}

static void check_ascii_fold(vstr input) {
  char output[128];
  size_t required = SIZE_MAX, written = SIZE_MAX;
  memset(output, '?', sizeof(output));
  check_equal(unicode_case_fold(input, NULL, 0u, &required), SALTS_UNICODE_OK);
  check_equal(required, input.len);
  check_equal(unicode_case_fold(input, output, input.len, &written), SALTS_UNICODE_OK);
  check_equal(written, input.len);
  for (size_t i = 0u; i < input.len; ++i) {
    const unsigned char byte = (unsigned char)input.data[i];
    check_equal(output[i], (char)(byte >= 'A' && byte <= 'Z' ? byte + 32u : byte));
  }
  check_equal(output[input.len], '?');
}

suite("Unicode counted ASCII and UTF-8 transitions") {
  it("matches scalar properties for every ASCII byte at vector and alignment boundaries") {
    char storage[112];
    for (size_t alignment = 0u; alignment < 16u; ++alignment) {
      char *input = storage + alignment;
      for (size_t length = 0u; length <= 80u; ++length) {
        for (unsigned byte = 0u; byte < 128u; ++byte) {
          memset(input, (int)byte, length);
          check_queries(vstr_from_buf(input, length));
          check_ascii_fold(vstr_from_buf(input, length));
          if (length > 1u) {
            input[0] = 'A';
            check_queries(vstr_from_buf(input, length));
          }
        }
      }
    }
    check_queries(vstr_from_buf(NULL, 0u));
    check_ascii_fold(vstr_from_buf(NULL, 0u));
  }

  it("decodes non-ASCII scalars at every lane and preserves full folding expansions") {
    static const char *const tails[] = {"\xc3\xa9", "\xe3\x80\x80", "\xcc\x81", "\xf0\x9f\x98\x80"};
    char input[112], output[128];
    for (size_t prefix = 0u; prefix <= 64u; ++prefix) {
      for (size_t t = 0u; t < sizeof(tails) / sizeof(tails[0]); ++t) {
        const size_t tail = strlen(tails[t]);
        memset(input, 'A', prefix);
        memcpy(input + prefix, tails[t], tail);
        memcpy(input + prefix + tail, "_9\v\f", 4u);
        check_queries(vstr_from_buf(input, prefix + tail + 4u));
      }
      memset(input, 'A', prefix);
      memcpy(input + prefix, "\xc4\xb0\xc3\x9fZ", 5u);
      const vstr view = vstr_from_buf(input, prefix + 5u);
      size_t written = SIZE_MAX;
      check_equal(unicode_case_fold(view, NULL, 0u, &written), SALTS_UNICODE_OK);
      check_equal(written, prefix + 6u);
      memset(output, '?', sizeof(output));
      written = SIZE_MAX;
      check_equal(unicode_case_fold(view, output, prefix + 5u, &written), SALTS_UNICODE_ERR_CAPACITY);
      check_equal(written, SIZE_MAX);
      for (size_t i = 0u; i < sizeof(output); ++i) check_equal(output[i], '?');
      check_equal(unicode_case_fold(view, output, sizeof(output), &written), SALTS_UNICODE_OK);
      check_equal(written, prefix + 6u);
      for (size_t i = 0u; i < prefix; ++i) check_equal(output[i], 'a');
      check_equal(output + prefix, "i\xcc\x87ssz", 6u);
      check_equal(output[written], '?');
      written = SIZE_MAX;
      check_equal(unicode_case_fold(view, input + 1u, sizeof(input) - 1u, &written), SALTS_UNICODE_ERR_INVALID_ARGUMENT);
      check_equal(written, SIZE_MAX);
    }
  }

  it("validates malformed UTF-8 after ASCII even when the identifier already failed") {
    static const unsigned char invalid[][4] = {
      {0x80, 0, 0, 0}, {0xc0, 0xaf, 0, 0}, {0xed, 0xa0, 0x80, 0},
      {0xf4, 0x90, 0x80, 0x80}, {0xe2, 0x82, 0, 0}
    };
    static const size_t lengths[] = {1u, 2u, 3u, 4u, 2u};
    char input[96], output[128];
    for (size_t prefix = 0u; prefix <= 64u; ++prefix) {
      for (size_t t = 0u; t < sizeof(lengths) / sizeof(lengths[0]); ++t) {
        memset(input, 'A', prefix);
        if (prefix != 0u) input[0] = '!';
        memcpy(input + prefix, invalid[t], lengths[t]);
        const vstr view = vstr_from_buf(input, prefix + lengths[t]);
        const char *sentinel = "sentinel";
        vstr trimmed = vstr_from_buf(sentinel, 8u);
        int identifier = 42;
        size_t written = SIZE_MAX;
        check_equal(unicode_trim_whitespace(view, &trimmed), SALTS_UNICODE_ERR_INVALID_UTF8);
        check(trimmed.data == sentinel);
        check_equal(trimmed.len, 8u);
        check_equal(unicode_is_xid(view, &identifier), SALTS_UNICODE_ERR_INVALID_UTF8);
        check_equal(identifier, 42);
        check_equal(unicode_case_fold(view, NULL, 0u, &written), SALTS_UNICODE_ERR_INVALID_UTF8);
        check_equal(written, SIZE_MAX);
        memset(output, '?', sizeof(output));
        check_equal(unicode_case_fold(view, output, sizeof(output), &written), SALTS_UNICODE_ERR_INVALID_UTF8);
        check_equal(written, SIZE_MAX);
        for (size_t i = 0u; i < sizeof(output); ++i) check_equal(output[i], '?');
      }
    }
  }
}

suite("Unicode counted input at protected memory") {
  static char *mapping;
  static size_t page_size;
  before_all() {
#if defined(_WIN32)
    SYSTEM_INFO info;
    DWORD old_protection;
    GetSystemInfo(&info);
    page_size = (size_t)info.dwPageSize;
    mapping = (char *)VirtualAlloc(NULL, page_size * 2u,
                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check_not_null(mapping);
    check(VirtualProtect(mapping + page_size, page_size, PAGE_NOACCESS, &old_protection) != 0);
#else
    const long native_page_size = sysconf(_SC_PAGESIZE);
    check(native_page_size >= 80);
    page_size = (size_t)native_page_size;
    void *pages = mmap(NULL, page_size * 2u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(pages != MAP_FAILED);
    mapping = (char *)pages;
    check_equal(mprotect(mapping + page_size, page_size, PROT_NONE), 0);
#endif
  }
  after_all() {
    if (mapping != NULL) {
#if defined(_WIN32)
      check(VirtualFree(mapping, 0, MEM_RELEASE) != 0);
#else
      check_equal(munmap(mapping, page_size * 2u), 0);
#endif
      mapping = NULL;
    }
  }
  it("handles empty, unaligned and partial vectors without reading past the view") {
    check(page_size >= 80u);
    char *end = mapping + page_size;
    for (size_t length = 0u; length <= 80u; ++length) {
      char *input = end - length;
      memset(input, 'A', length);
      check_queries(vstr_from_buf(input, length));
      check_ascii_fold(vstr_from_buf(input, length));
      memset(input, '\v', length);
      check_queries(vstr_from_buf(input, length));
      if (length >= 2u) {
        memset(input, 'A', length);
        end[-2] = (char)0xc3;
        end[-1] = (char)0xa9;
        check_queries(vstr_from_buf(input, length));
        char output[128];
        size_t written = SIZE_MAX;
        check_equal(unicode_case_fold(vstr_from_buf(input, length), output, sizeof(output), &written), SALTS_UNICODE_OK);
        check_equal(written, length);
        check_equal(output + length - 2u, "\xc3\xa9", 2u);
      }
    }
  }
}
