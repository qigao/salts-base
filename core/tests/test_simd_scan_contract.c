#include <core/scan.h>
#include <core/vstr.h>
#include <tinytest.h>
#include "scan_internal.h"
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

spec("Canonical bounded byte scanning") {
  it("skips only the four XML/JSON whitespace bytes at every vector boundary") {
    char storage[112];
    const char whitespace[] = {' ', '\t', '\r', '\n'};
    const char stops[] = {'\0', '\v', '\f', (char)0x80, 'x'};
    for (size_t alignment = 0u; alignment < 16u; ++alignment) {
      char *p = storage + alignment;
      for (size_t length = 0u; length <= 80u; ++length) {
        for (size_t stop = 0u; stop < sizeof(stops); ++stop) {
          for (size_t at = 0u; at <= length; ++at) {
            for (size_t i = 0u; i < length; ++i) p[i] = whitespace[i % sizeof(whitespace)];
            if (at < length) p[at] = stops[stop];
            check(scan_skip_sp_tab_cr_lf(p, p + length) == p + at);
          }
        }
      }
    }
    check(scan_skip_sp_tab_cr_lf(NULL, storage) == storage);
    check(scan_skip_sp_tab_cr_lf(storage, NULL) == NULL);
    check(scan_skip_sp_tab_cr_lf(storage + 1, storage) == storage);
  }

  it("retains the seven-function scan semantics including NUL and boundaries") {
    const char bytes[] = {'a', '\0', 'b', '{', '}', ' ', '\t', '\r', '\n', 'c'};
    const char *end = bytes + sizeof(bytes);
    check(scan_char(bytes, end, '\0') == bytes + 1);
    check(scan_char(bytes, end, 'z') == NULL);
    check(scan_to_char(bytes, end, '{') == bytes + 3);
    check(scan_to_char(bytes, end, 'x') == end);
    check(scan_to_any2(bytes, end, '[', '}') == bytes + 4);
    check(scan_to_any3(bytes, end, 'x', 'y', 'b') == bytes + 2);
    check(scan_to_any4(bytes, end, 'x', 'y', 'z', '\0') == bytes + 1);
    check(scan_to_any5(bytes, end, 'x', 'y', 'z', 'q', '}') == bytes + 4);
    check(scan_to_any5(bytes, end, 'x', 'y', 'z', 'q', 'v') == end);
    check(scan_to_any4(end, end, 'x', 'y', 'z', '\0') == end);
    check(scan_skip_sp_tab(bytes + 5, end) == bytes + 7);
    check(scan_skip_sp_tab_cr_lf(bytes + 5, end) == bytes + 9);
    check(scan_mem(bytes, sizeof(bytes), "b{", 2) == bytes + 2);
    check(scan_mem(bytes, sizeof(bytes), "missing", 7) == NULL);
    check(scan_mem(bytes, sizeof(bytes), "", 0) == bytes);
    check(scan_mem(NULL, 0, "", 0) == NULL);
    char buffer[128];
    memset(buffer, 'a', sizeof(buffer));
    buffer[127] = 'z';
    check(scan_char(buffer, buffer + sizeof(buffer), 'z') == buffer + 127);
    check(scan_to_any2(buffer, buffer + sizeof(buffer), 'x', 'z') == buffer + 127);
    check(scan_to_any3(buffer, buffer + sizeof(buffer), 'x', 'y', 'z') == buffer + 127);
  }
}

spec("Scalar and SIMDe scanning equivalence") {
  it("finds every delimiter at every offset across vector and tail boundaries") {
    char storage[112];
    const char delimiters[] = {'\0', '\r', '\n', (char)0x80, (char)0xff};
    for (size_t offset = 0; offset < 16u; ++offset) {
      char *p = storage + offset;
      for (size_t length = 0; length <= 80u; ++length) {
        const char *end = p + length;
        for (size_t at = 0; at <= length; ++at) {
          for (size_t which = 0; which < sizeof(delimiters); ++which) {
            memset(p, 'x', length + 1u);
            p[at] = delimiters[which];
            const char *expected4 = which < 4u && at < length ? p + at : end;
            const char *expected5 = at < length ? p + at : end;
            check(scan_scalar_to_any4(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3]) == expected4);
            check(scan_simde_to_any4(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3]) == expected4);
            check(scan_to_any4(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3]) == expected4);
            check(scan_scalar_to_any5(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3], delimiters[4]) == expected5);
            check(scan_simde_to_any5(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3], delimiters[4]) == expected5);
            check(scan_to_any5(p, end, delimiters[0], delimiters[1],
                      delimiters[2], delimiters[3], delimiters[4]) == expected5);
          }
        }
      }
    }
  }

  it("stops at the first match when delimiters repeat within one vector") {
    const char input[] = "xx#xxx#xxxx\nxxxxxxxxxxxxxxxx";
    const char *end = input + sizeof(input) - 1u;
    check(scan_scalar_to_any4(input, end, '#', '#', '\n', '\n') == input + 2);
    check(scan_simde_to_any4(input, end, '#', '#', '\n', '\n') == input + 2);
    check(scan_to_any4(input, end, '#', '#', '\n', '\n') == input + 2);
    check(scan_scalar_to_any5(input, end, '#', '#', '\n', '\n', '\0') == input + 2);
    check(scan_simde_to_any5(input, end, '#', '#', '\n', '\n', '\0') == input + 2);
    check(scan_to_any5(input, end, '#', '#', '\n', '\n', '\0') == input + 2);
  }

  it("skips only spaces and tabs at every vector lane and tail offset") {
    char storage[112];
    for (size_t offset = 0; offset < 16u; ++offset) {
      char *p = storage + offset;
      for (size_t length = 0; length <= 80u; ++length) {
        for (size_t at = 0; at <= length; ++at) {
          for (size_t i = 0; i <= length; ++i) p[i] = i % 2u ? '\t' : ' ';
          p[at] = at % 2u ? '\r' : '\0';
          check(scan_scalar_skip_sp_tab(p, p + length) == p + at);
          check(scan_simde_skip_sp_tab(p, p + length) == p + at);
          check(scan_skip_sp_tab(p, p + length) == p + at);
        }
      }
    }
  }

  it("rejects null and reversed endpoints before subtracting pointers") {
    char bytes[32] = {0};
    const char *starts[] = {NULL, bytes, NULL, bytes + sizeof(bytes)};
    const char *ends[] = {bytes, NULL, NULL, bytes};
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
      const char *p = starts[i], *end = ends[i];
      check(scan_scalar_skip_sp_tab(p, end) == end);
      check(scan_simde_skip_sp_tab(p, end) == end);
      check(scan_skip_sp_tab(p, end) == end);
      check(scan_skip_sp_tab_cr_lf(p, end) == end);
      check(scan_scalar_to_any4(p, end, 'a', 'b', 'c', 'd') == end);
      check(scan_simde_to_any4(p, end, 'a', 'b', 'c', 'd') == end);
      check(scan_to_any4(p, end, 'a', 'b', 'c', 'd') == end);
      check(scan_scalar_to_any5(p, end, 'a', 'b', 'c', 'd', 'e') == end);
      check(scan_simde_to_any5(p, end, 'a', 'b', 'c', 'd', 'e') == end);
      check(scan_to_any5(p, end, 'a', 'b', 'c', 'd', 'e') == end);
    }
  }
}

spec("Bounded scanning at protected memory") {
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
    check(VirtualProtect(mapping + page_size, page_size,
                         PAGE_NOACCESS, &old_protection) != 0);
#else
    long native_page_size = sysconf(_SC_PAGESIZE);
    check(native_page_size >= 80);
    page_size = (size_t)native_page_size;
    void *pages = mmap(NULL, page_size * 2u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(pages != MAP_FAILED);
    mapping = (char *)pages;
    check(mprotect(mapping + page_size, page_size, PROT_NONE) == 0);
#endif
  }

  after_all() {
    if (mapping != NULL) {
#if defined(_WIN32)
      check(VirtualFree(mapping, 0, MEM_RELEASE) != 0);
#else
      check(munmap(mapping, page_size * 2u) == 0);
#endif
      mapping = NULL;
    }
  }

  it("never loads beyond end for empty ranges, vectors or partial tails") {
    check(page_size >= 80u);
    char *end = mapping + page_size;
    for (size_t length = 0; length <= 80u; ++length) {
      char *p = end - length;
      memset(p, 'x', length);
      check(scan_char(p, end, '\0') == NULL);
      check(scan_to_any2(p, end, '\0', '\n') == end);
      check(scan_to_any3(p, end, '\0', '\n', '#') == end);
      check(scan_mem(p, length, "x\n", 2u) == NULL);
      check_equal(vstr_find(vstr_from_buf(p, length), vstr_from_buf("x\n", 2u)), VSTR_NPOS);
      check_equal(vstr_find_char(vstr_from_buf(p, length), '\0'), VSTR_NPOS);
      check_equal(vstr_find_any(vstr_from_buf(p, length), vstr_from_buf("\0\r\n#", 4u)), VSTR_NPOS);
      check(scan_scalar_to_any4(p, end, '\0', '\r', '\n', '#') == end);
      check(scan_simde_to_any4(p, end, '\0', '\r', '\n', '#') == end);
      check(scan_to_any4(p, end, '\0', '\r', '\n', '#') == end);
      check(scan_scalar_to_any5(p, end, '\0', '\r', '\n', '#', ':') == end);
      check(scan_simde_to_any5(p, end, '\0', '\r', '\n', '#', ':') == end);
      check(scan_to_any5(p, end, '\0', '\r', '\n', '#', ':') == end);
      if (length != 0u) {
        end[-1] = '\n';
        check(scan_to_any2(p, end, '\0', '\n') == end - 1);
        check(scan_to_any3(p, end, '\0', '\n', '#') == end - 1);
        check_equal(vstr_find_any(vstr_from_buf(p, length), vstr_from_buf("\0\n", 2u)), length - 1u);
        check_equal(vstr_find(vstr_from_buf(p, length), vstr_from_buf("\n", 1u)), length - 1u);
        if (length >= 2u) {
          check(scan_mem(p, length, "x\n", 2u) == end - 2);
          check_equal(vstr_find(vstr_from_buf(p, length), vstr_from_buf("x\n", 2u)), length - 2u);
        }
        check(scan_to_any4(p, end, '\0', '\r', '\n', '#') == end - 1);
        check(scan_to_any5(p, end, '\0', '\r', '\n', '#', ':') == end - 1);
      }
      memset(p, ' ', length);
      check(scan_scalar_skip_sp_tab(p, end) == end);
      check(scan_simde_skip_sp_tab(p, end) == end);
      check(scan_skip_sp_tab(p, end) == end);
      for (size_t i = 0u; i < length; ++i) p[i] = " \t\r\n"[i % 4u];
      check(scan_skip_sp_tab_cr_lf(p, end) == end);
      if (length != 0u) {
        end[-1] = '\f';
        check(scan_skip_sp_tab_cr_lf(p, end) == end - 1);
      }
    }
  }
}
