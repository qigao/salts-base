#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <ini_parser.h>
#include <tinytest.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

suite("INI counted input boundaries") {
  it("ignores valid tokens and invalid bytes beyond the supplied length") {
    const char input[] = "[scan]\nk=value\nextra=ignored\n";
    const size_t length = sizeof("[scan]\nk=value\n") - 1u;
    ini_t *ini = ini_parse(input, length);
    check_not_null(ini);
    check_equal(ini_key_count(ini, "scan"), 1u);
    check_equal(ini_get(ini, "scan", "k"), "value");
    check_null(ini_get(ini, "scan", "extra"));
    ini_free(ini);

    const char empty_value[] = "[scan]\nk=outside";
    ini = ini_parse(empty_value, sizeof("[scan]\nk=") - 1u);
    check_not_null(ini);
    check_equal(ini_get(ini, "scan", "k"), "");
    ini_free(ini);

    const char incomplete_section[] = "[scan]";
    check_null(ini_parse(incomplete_section, sizeof(incomplete_section) - 2u));
    const char incomplete_key[] = "[scan]\nkey=value";
    check_null(ini_parse(incomplete_key, sizeof("[scan]\nkey") - 1u));
  }

  it("rejects in-range NUL instead of treating it as end of the counted input") {
    static const struct { const char *input; size_t length; } cases[] = {
      {"\0", 1u},
      {"[scan\0]", sizeof("[scan\0]") - 1u},
      {"[scan]\nk\0=value", sizeof("[scan]\nk\0=value") - 1u},
      {"[scan]\nk=\0value", sizeof("[scan]\nk=\0value") - 1u},
      {"[scan]\nk=value\0tail", sizeof("[scan]\nk=value\0tail") - 1u},
      {"# comment\0tail", sizeof("# comment\0tail") - 1u}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
      check_null(ini_parse(cases[i].input, cases[i].length));
  }
}

suite("INI input ending at protected memory") {
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
    check(native_page_size >= 128);
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
  it("bounds every lookahead for empty input, partial tokens and EOF values") {
    static const struct { const char *input; int valid; } cases[] = {
      {"", 1}, {" \t", 1}, {"# comment", 1}, {"; comment", 1},
      {"\r", 1}, {"\r\n", 1}, {"[scan]", 1}, {"[scan]\nk=", 1},
      {"[scan]\nk= \t", 1}, {"[scan]\nk=value", 1},
      {"[scan]\nk=value\r", 1}, {"[scan]\nk=value\r\n", 1},
      {"[scan]\nk=value ; comment", 1},
      {"[scan", 0}, {"[scan]\nk", 0}, {"[scan]\nk \t", 0}
    };
    check(page_size >= 128u);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      const size_t length = strlen(cases[i].input);
      char *input = mapping + page_size - length;
      memcpy(input, cases[i].input, length);
      ini_t *ini = ini_parse(input, length);
      if (cases[i].valid) {
        check_not_null(ini);
        ini_free(ini);
      } else {
        check_null(ini);
      }
    }
  }
  it("handles complete and partial SIMD key widths without padding") {
    char key[81];
    char *end = mapping + page_size;
    for (size_t length = 1u; length <= 80u; ++length) {
      const size_t total = sizeof("[scan]\n") - 1u + length + 1u;
      char *input = end - total;
      memcpy(input, "[scan]\n", sizeof("[scan]\n") - 1u);
      memset(input + sizeof("[scan]\n") - 1u, 'k', length);
      end[-1] = '=';
      memset(key, 'k', length);
      key[length] = '\0';
      ini_t *ini = ini_parse(input, total);
      check_not_null(ini);
      check_equal(ini_get(ini, "scan", key), "");
      ini_free(ini);
    }
  }
}
