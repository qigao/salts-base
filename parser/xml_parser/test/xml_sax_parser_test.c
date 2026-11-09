#include <xml_parser/xml_sax.h>
#include <tinytest.h>
#include <string.h>

struct events {
  size_t starts, ends, attributes, comments, cdata, pi;
  char text[128];
  size_t text_size;
  int stop;
};
static int start(void *ctx, const char *name, size_t len) {
  struct events *e = ctx;
  if (!name || !len) return -1;
  e->starts++;
  return 0;
}
static int end(void *ctx, const char *name, size_t len) {
  struct events *e = ctx;
  if (!name || !len) return -1;
  e->ends++;
  return e->stop ? -1 : 0;
}
static int attribute(void *ctx, const char *name, size_t name_len,
                     const char *value, size_t value_len) {
  struct events *e = ctx;
  if (name_len != 4 || memcmp(name, "name", 4) ||
      value_len != 3 || memcmp(value, "a\"b", 3)) return -1;
  e->attributes++;
  return 0;
}
static int text(void *ctx, const char *value, size_t len) {
  struct events *e = ctx;
  if (len >= sizeof(e->text) - e->text_size) return -1;
  memcpy(e->text + e->text_size, value, len);
  e->text_size += len;
  e->text[e->text_size] = '\0';
  return 0;
}
static int comment(void *ctx, const char *value, size_t len) {
  struct events *e = ctx;
  if (len != 1 || value[0] != 'c') return -1;
  e->comments++;
  return 0;
}
static int cdata(void *ctx, const char *value, size_t len) {
  struct events *e = ctx;
  if (len != 3 || memcmp(value, "x<y", 3)) return -1;
  e->cdata++;
  return 0;
}
static int pi(void *ctx, const char *target, size_t target_len,
              const char *value, size_t len) {
  struct events *e = ctx;
  if (target_len != 1 || target[0] != 'p' || len != 1 || value[0] != 'd') return -1;
  e->pi++;
  return 0;
}
static const xml_sax_handler_t handler = {
  .on_element_start = start, .on_element_end = end, .on_attribute = attribute,
  .on_text = text, .on_comment = comment, .on_cdata = cdata,
  .on_processing_instruction = pi
};

spec("Salts native XML SAX") {
  it("keeps quote/bracket state and terminators stable across vector and feed boundaries") {
    const char input[] =
        "<!DOCTYPE root [<!ENTITY value '123456789012345[>6789'>]>"
        "<root attr='1234567890123456789'>"
        "<!--1234567890123456789--><![CDATA[1234567890123456789]]>"
        "<?p 1234567890123456789?></root>";
    const xml_sax_handler_t structural = {.on_element_start = start, .on_element_end = end};
    for (size_t step = 1u; step <= 33u; ++step) {
      struct events e = {0};
      xml_sax_parser_t *p = xml_sax_parser_create(&structural, &e, sizeof(input));
      check_not_null(p);
      for (size_t offset = 0u; offset < sizeof(input) - 1u; offset += step) {
        size_t length = sizeof(input) - 1u - offset;
        if (length > step) length = step;
        check_equal(xml_sax_parser_feed(p, input + offset, length), 0);
      }
      check_equal(xml_sax_parser_finish(p), 0);
      check_equal(e.starts, 1u);
      check_equal(e.ends, 1u);
      xml_sax_parser_destroy(p);
    }
  }

  it("retains errors for invalid attributes and incomplete terminators") {
    const char *inputs[] = {"<r a='123456789012345<bad'/>", "<r><!--123456789012345--",
                            "<r><![CDATA[123456789012345]]", "<!DOCTYPE r [<'123456789012345'>"};
    const char *errors[] = {"Invalid <", "comment", "CDATA", "DOCTYPE"};
    const xml_sax_handler_t structural = {0};
    for (size_t i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
      xml_sax_parser_t *p = xml_sax_parser_create(&structural, NULL, 256u);
      check_not_null(p);
      int status = xml_sax_parser_feed(p, inputs[i], strlen(inputs[i]));
      if (status == 0) status = xml_sax_parser_finish(p);
      check_not_equal(status, 0);
      check_contains(xml_sax_parser_error(p), errors[i]);
      xml_sax_parser_destroy(p);
    }
  }

  it("keeps raw lexical events stable at every chunk size") {
    const char input[] =
        "<root><item name='a\"b'>A&amp;B</item><!--c--><![CDATA[x<y]]><?p d?></root>";
    for (size_t step = 1; step <= sizeof(input); ++step) {
      struct events e = {0};
      xml_sax_parser_t *p = xml_sax_parser_create(&handler, &e, sizeof(input));
      check_not_null(p);
      if (!p) continue;
      for (size_t offset = 0; offset < sizeof(input) - 1; offset += step) {
        size_t len = sizeof(input) - 1 - offset;
        if (len > step) len = step;
        check_equal(xml_sax_parser_feed(p, input + offset, len), 0);
      }
      check_equal(e.ends, 2u); /* Delivered by feed, not deferred to EOF. */
      check_equal(xml_sax_parser_finish(p), 0);
      check_equal(e.starts, 2u);
      check_equal(e.attributes, 1u);
      check_equal(e.comments, 1u);
      check_equal(e.cdata, 1u);
      check_equal(e.pi, 1u);
      check_equal(e.text, "A&amp;B");
      xml_sax_parser_destroy(p);
    }
  }
  it("makes callback failure sticky and stops further events") {
    struct events e = {.stop = 1};
    xml_sax_parser_t *p = xml_sax_parser_create(&handler, &e, 128);
    check_not_null(p);
    if (p) {
      check_not_equal(xml_sax_parser_feed(p, "<r><i/></r>", 11), 0);
      size_t count = e.starts + e.ends;
      check_not_equal(xml_sax_parser_feed(p, "<x/>", 4), 0);
      check_not_equal(xml_sax_parser_finish(p), 0);
      check_equal(e.starts + e.ends, count);
      check_contains(xml_sax_parser_error(p), "callback");
      xml_sax_parser_destroy(p);
    }
  }
  it("rejects malformed documents and embedded NUL without sharing errors") {
    const char *invalid[] = {"<r></x>", "<r/ ><x/>", "<r>", "<r/><x/>", "text"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      struct events e = {0};
      xml_sax_parser_t *p = xml_sax_parser_create(&handler, &e, 128);
      check_not_null(p);
      if (!p) continue;
      int rc = xml_sax_parser_feed(p, invalid[i], strlen(invalid[i]));
      if (rc == 0) rc = xml_sax_parser_finish(p);
      check_not_equal(rc, 0);
      check_not_null(xml_sax_parser_error(p));
      xml_sax_parser_destroy(p);
    }
    struct events e = {0};
    xml_sax_parser_t *bad = xml_sax_parser_create(&handler, &e, 128);
    xml_sax_parser_t *good = xml_sax_parser_create(&handler, &e, 128);
    check_not_null(bad);
    check_not_null(good);
    if (bad && good) {
      check_not_equal(xml_sax_parser_feed(bad, "<r>\0</r>", 8), 0);
      check_contains(xml_sax_parser_error(bad), "NUL");
      check_equal(xml_sax_parser_feed(good, "<r/>", 4), 0);
      check_equal(xml_sax_parser_finish(good), 0);
      check_null(xml_sax_parser_error(good));
      check_not_equal(xml_sax_parser_finish(good), 0);
    }
    xml_sax_parser_destroy(bad);
    xml_sax_parser_destroy(good);
  }
  it("bounds pending token storage and permits configuration only before feed") {
    struct events e = {0};
    xml_sax_parser_t *p = xml_sax_parser_create(&handler, &e, 8);
    check_null(xml_sax_parser_create(&handler, &e, 0));
    check_not_null(p);
    if (p) {
      check_equal(xml_sax_parser_set_buffer_limit(p, 16), 0);
      check_equal(xml_sax_parser_feed(p, "<r name='", 9), 0);
      check_not_equal(xml_sax_parser_set_buffer_limit(p, 32), 0);
      check_not_equal(xml_sax_parser_feed(p, "abcdefgh", 8), 0);
      check_contains(xml_sax_parser_error(p), "limit");
      check_equal(e.starts, 0u);
      xml_sax_parser_destroy(p);
    }
  }
}
