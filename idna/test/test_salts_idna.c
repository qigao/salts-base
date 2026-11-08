#include "idna.h"
#include "tinytest.h"
#include <string.h>

static salts_idna_status convert_profile(vstr in, uint32_t profile, char *out, size_t cap, size_t *n) {
  char mapped[4096], normalized[4096];
  uint32_t scalars[4096];
  salts_idna_workspace w = {mapped, sizeof mapped, normalized, sizeof normalized, scalars, 4096};
  return salts_idna_to_ascii(in, profile, &w, out, cap, n);
}

static salts_idna_status convert(vstr in, char *out, size_t cap, size_t *n) {
  return convert_profile(in, SALTS_IDNA_UNICODE17_UTS46_35_STRICT, out, cap, n);
}

static void accepted_profile(const char *in, const char *expected, uint32_t profile) {
  char out[255], again[255];
  size_t n = 999, second = 999;
  check_equal(convert_profile(vstr_from_cstr(in), profile, out, sizeof out, &n), SALTS_IDNA_OK);
  check_equal(out, expected);
  check_equal(n, strlen(expected));
  check_equal(convert_profile(vstr_from_buf(out, n), profile, again, sizeof again, &second), SALTS_IDNA_OK);
  check_equal(again, expected);
  check_equal(second, n);
}

static void accepted(const char *in, const char *expected) {
  accepted_profile(in, expected, SALTS_IDNA_UNICODE17_UTS46_35_STRICT);
}

static void rejected_profile(const char *in, salts_idna_status expected, uint32_t profile) {
  char out[255];
  memset(out, 0x5a, sizeof out);
  size_t n = 999;
  check_equal(convert_profile(vstr_from_cstr(in), profile, out, sizeof out, &n), expected);
  check_equal(n, (size_t)999);
  for (size_t i = 0; i < sizeof out; ++i) check_equal(out[i], (char)0x5a);
}

static void rejected(const char *in, salts_idna_status expected) {
  rejected_profile(in, expected, SALTS_IDNA_UNICODE17_UTS46_35_STRICT);
}

spec("Unicode 17 IDNA connection identity") {
  it("recognizes absolute DNS roots after mapping without changing STRICT") {
    const char *names[] = {
      "B\xc3\xbc" "cher.de.\xc2\xad",
      "bu\xcc\x88" "cher.de\xe3\x80\x82\xcd\x8f",
      "XN--BCHER-KVA.de\xef\xbc\x8e\xe2\x80\x8b",
      "b\xc3\xbc" "cher.de\xef\xbd\xa1\xc2\xad\xcd\x8f\xe2\x80\x8b",
      "b\xc3\xbc" "cher.de."
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
      accepted_profile(names[i], "xn--bcher-kva.de.", SALTS_IDNA_UNICODE17_UTS46_35_DNS);
      rejected(names[i], SALTS_IDNA_INVALID_LABEL);
    }
    accepted_profile("Example.COM\xc2\xad", "example.com", SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    accepted_profile("abc.\xd7\x90.\xc2\xad", "abc.xn--4db.", SALTS_IDNA_UNICODE17_UTS46_35_DNS);
  }

  it("keeps invalid DNS roots and label failures atomic") {
    const char *invalid[] = {"", ".", ".\xc2\xad", "\xe3\x80\x82\xcd\x8f",
      "a..", "a.\xc2\xad.\xcd\x8f", "a..b.", "a.\xc2\xad.b.", ".a."};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
      rejected_profile(invalid[i], SALTS_IDNA_INVALID_LABEL, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    rejected_profile("a.\xff", SALTS_IDNA_INVALID_UTF8, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    rejected_profile("123.\xd7\x90.\xc2\xad", SALTS_IDNA_BIDI, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    rejected_profile("a\xe2\x80\x8d.b.", SALTS_IDNA_CONTEXTJ, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    rejected_profile("a\xc2\xb7" "b.", SALTS_IDNA_CONTEXTO, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    rejected_profile("xn--abc-.", SALTS_IDNA_INVALID_ALABEL, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
  }

  it("counts the optional DNS root separately and includes it in output capacity") {
    char in[259], out[255]; size_t n = 999;
    memset(in, 'a', 253);
    in[63] = in[127] = in[191] = '.';
    memcpy(in + 253, ".\xc2\xad", 4);
    memset(out, 0x5a, sizeof out);
    check_equal(convert_profile(vstr_from_cstr(in), SALTS_IDNA_UNICODE17_UTS46_35_DNS,
      out, sizeof out - 1, &n), SALTS_IDNA_OUTPUT_CAPACITY);
    check_equal(n, (size_t)999);
    for (size_t i = 0; i < sizeof out; ++i) check_equal(out[i], (char)0x5a);
    check_equal(convert_profile(vstr_from_cstr(in), SALTS_IDNA_UNICODE17_UTS46_35_DNS,
      out, sizeof out, &n), SALTS_IDNA_OK);
    check_equal(n, (size_t)254); check_equal(out[253], '.'); check_equal(out[254], '\0');
    check_equal(out, in, 254);
    accepted_profile(out, out, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    memcpy(in + 253, "a.\xc2\xad", 5);
    rejected_profile(in, SALTS_IDNA_DNS_LENGTH, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
    memset(in, 'a', 64); in[64] = '.'; in[65] = '\0';
    rejected_profile(in, SALTS_IDNA_DNS_LENGTH, SALTS_IDNA_UNICODE17_UTS46_35_DNS);
  }

  it("maps case width delimiters and NFC while keeping nontransitional sharp-s") {
    accepted("Example.COM", "example.com");
    accepted("B\xc3\xbc" "cher.de", "xn--bcher-kva.de");
    accepted("bu\xcc\x88" "cher.de", "xn--bcher-kva.de");
    accepted("fa\xc3\x9f.de", "xn--fa-hia.de");
    accepted("\xef\xbc\xa1\xe3\x80\x82\xef\xbc\xa2\xef\xbc\x8e" "C\xef\xbd\xa1" "DE", "a.b.c.de");
    accepted("XN--BCHER-KVA.DE", "xn--bcher-kva.de");
    accepted("ab\xc2\xad" "cd.com", "abcd.com");
    accepted("\xe4\xbe\x8b\xe5\xad\x90.\xe6\xb5\x8b\xe8\xaf\x95", "xn--fsqu00a.xn--0zwm56d");
  }

  it("rejects malformed UTF8 NUL labels and unvalidated A-labels atomically") {
    rejected("a\xc0\xaf.com", SALTS_IDNA_INVALID_UTF8);
    rejected("a\xed\xa0\x80.com", SALTS_IDNA_INVALID_UTF8);
    rejected("a\xf4\x90\x80\x80.com", SALTS_IDNA_INVALID_UTF8);
    rejected("a\xe2\x82", SALTS_IDNA_INVALID_UTF8);
    rejected("", SALTS_IDNA_INVALID_LABEL);
    rejected(".", SALTS_IDNA_INVALID_LABEL);
    rejected("a.", SALTS_IDNA_INVALID_LABEL);
    rejected("a..b", SALTS_IDNA_INVALID_LABEL);
    rejected("-a", SALTS_IDNA_INVALID_LABEL);
    rejected("a-", SALTS_IDNA_INVALID_LABEL);
    rejected("ab--cd", SALTS_IDNA_INVALID_LABEL);
    rejected("_a", SALTS_IDNA_DISALLOWED);
    rejected("\xcc\x81" "a", SALTS_IDNA_INVALID_LABEL);
    rejected("\xe0\xa4\x83" "a", SALTS_IDNA_INVALID_LABEL); /* CCC=0 mark */
    rejected("xn--", SALTS_IDNA_INVALID_ALABEL);
    rejected("xn--abc-", SALTS_IDNA_INVALID_ALABEL);
    rejected("xn--a", SALTS_IDNA_DISALLOWED);
    rejected("xn--e-xbb", SALTS_IDNA_INVALID_ALABEL); /* decoded e + acute */
    char out[254] = "unchanged";
    size_t n = 9;
    check_equal(convert(vstr_from_buf("a\0b", 3), out, sizeof out, &n), SALTS_IDNA_DISALLOWED);
    check_equal(out, "unchanged");
    check_equal(n, (size_t)9);
  }

  it("activates Bidi for the entire decoded domain including preceding ASCII labels") {
    accepted("abc.\xd7\x90", "abc.xn--4db");
    accepted("123.example", "123.example");
    rejected("123.\xd7\x90", SALTS_IDNA_BIDI);
    rejected("123.xn--4db", SALTS_IDNA_BIDI);
    char out[254]; size_t n;
    check_equal(convert(vstr_from_cstr("\xd7\x90.a1\xcc\x81"), out, sizeof out, &n), SALTS_IDNA_OK);
  }

  it("enforces ContextJ without dropping joiners") {
    rejected("a\xe2\x80\x8c" "b", SALTS_IDNA_CONTEXTJ);
    rejected("a\xe2\x80\x8d" "b", SALTS_IDNA_CONTEXTJ);
    char out[254]; size_t n;
    check_equal(convert(vstr_from_cstr("\xe0\xa4\x95\xe0\xa5\x8d\xe2\x80\x8d\xe0\xa4\xb7"), out, sizeof out, &n), SALTS_IDNA_OK);
    check_equal(convert(vstr_from_cstr("\xd8\xa8\xe2\x80\x8c\xd8\xa8"), out, sizeof out, &n), SALTS_IDNA_OK);
    rejected("\xd8\xa8\xe2\x80\x8d\xd8\xa8", SALTS_IDNA_CONTEXTJ);
  }

  it("applies each ContextO rule separately from UTS46 conformance") {
    char out[254]; size_t n;
    const char *valid[] = {"l\xc2\xb7l", "\xcd\xb5\xce\xb1", "\xd7\x90\xd7\xb3", "\xd7\x90\xd7\xb4", "\xe3\x82\xab\xe3\x83\xbb", "\xd8\xa8\xd9\xa1", "a\xdb\xb1"};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i)
      check_equal(convert(vstr_from_cstr(valid[i]), out, sizeof out, &n), SALTS_IDNA_OK);
    rejected("a\xc2\xb7" "b", SALTS_IDNA_CONTEXTO);
    rejected("\xcd\xb5" "a", SALTS_IDNA_CONTEXTO);
    rejected("a\xd7\xb3", SALTS_IDNA_CONTEXTO);
    rejected("a\xd7\xb4", SALTS_IDNA_CONTEXTO);
    rejected("a\xe3\x83\xbb", SALTS_IDNA_CONTEXTO);
    rejected("\xd8\xa8\xd9\xa1\xdb\xb1", SALTS_IDNA_CONTEXTO);
  }

  it("checks DNS byte limits after conversion and accepts the exact output capacity") {
    char in[256], out[254]; size_t n;
    memset(in, 'a', 64); in[63] = '\0';
    check_equal(convert(vstr_from_cstr(in), out, 64, &n), SALTS_IDNA_OK);
    check_equal(n, (size_t)63);
    in[63] = 'a'; in[64] = '\0';
    rejected(in, SALTS_IDNA_DNS_LENGTH);
    memset(in, 'a', 255);
    in[63] = in[127] = in[191] = '.'; in[253] = '\0';
    check_equal(convert(vstr_from_cstr(in), out, sizeof out, &n), SALTS_IDNA_OK);
    check_equal(n, (size_t)253);
    in[253] = 'a'; in[254] = '\0';
    rejected(in, SALTS_IDNA_DNS_LENGTH);
    memset(in, 'a', 253);
    for (size_t i = 1; i < 253; i += 2) in[i] = '.';
    in[253] = '\0';
    check_equal(convert(vstr_from_cstr(in), out, sizeof out, &n), SALTS_IDNA_OK);
  }

  it("distinguishes each caller budget and rejects aliasing and arithmetic overflow") {
    char mapped[32], normalized[32], out[32] = "unchanged";
    uint32_t cp[32];
    salts_idna_workspace w = {mapped, sizeof mapped, normalized, sizeof normalized, cp, 32};
    size_t n = 99;
    vstr input = vstr_from_cstr("abc");
    check_equal(salts_idna_to_ascii(input, 0, &w, out, sizeof out, &n), SALTS_IDNA_UNSUPPORTED_PROFILE);
    w.mapped_capacity = 2;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, sizeof out, &n), SALTS_IDNA_WORKSPACE);
    w.mapped_capacity = 3; w.normalized_capacity = 3;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, sizeof out, &n), SALTS_IDNA_WORKSPACE);
    w.normalized_capacity = 4; w.scalar_capacity = 2;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, sizeof out, &n), SALTS_IDNA_WORKSPACE);
    w.scalar_capacity = 3;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, 3, &n), SALTS_IDNA_OUTPUT_CAPACITY);
    check_equal(salts_idna_to_ascii(vstr_from_cstr(out), 1, &w, out, sizeof out, &n), SALTS_IDNA_INVALID_ARGUMENT);
    w.scalar_capacity = SIZE_MAX;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, sizeof out, &n), SALTS_IDNA_OVERFLOW);
    check_equal(out, "unchanged"); check_equal(n, (size_t)99);
    w.scalar_capacity = 3;
    check_equal(salts_idna_to_ascii(input, 1, &w, out, 4, &n), SALTS_IDNA_OK);
    check_equal(out, "abc"); check_equal(n, (size_t)3);
  }
}
