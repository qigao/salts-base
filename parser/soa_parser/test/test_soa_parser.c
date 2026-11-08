/**
 * SoA Parser Tests
 * Tests for Struct-of-Arrays columnar data parser
 */

#include "soa_parser.h"
#include "tinytest.h"
#include <stdlib.h>
#include <string.h>

/* Test schema: sensor data */
static const soa_schema_t sensor_schema = {
    .schema_id = 0x0001,
    .column_count = 3,
    .columns = {
        {"timestamp", SOA_TYPE_I64},
        {"sensor_id", SOA_TYPE_U16},
        {"value",     SOA_TYPE_F32},
    }
};

/* Test schema: game state */
static const soa_schema_t game_schema = {
    .schema_id = 0x0002,
    .column_count = 4,
    .columns = {
        {"entity_id", SOA_TYPE_U32},
        {"x",         SOA_TYPE_F32},
        {"y",         SOA_TYPE_F32},
        {"z",         SOA_TYPE_F32},
    }
};

/* Helper: write little-endian values */
static void write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void write_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (i * 8));
    }
}

static void write_f32(uint8_t *p, float v) {
    union { float f; uint32_t u; } conv = { .f = v };
    write_le32(p, conv.u);
}

spec("soa_parser") {
  before_each() {
    soa_register_schema(&sensor_schema);
    soa_register_schema(&game_schema);
  }

  describe("Schema Management") {
    it("should successfully find a registered schema by its ID") {
        const soa_schema_t *found = soa_find_schema(0x0001);
        check_not_null(found);
        check_equal(found->schema_id, 0x0001);
        check_equal(found->column_count, 3);
    }

    it("should return NULL when searching for a non-existent schema ID") {
        const soa_schema_t *found = soa_find_schema(0xFFFF);
        check_null(found);
    }

    it("should return correct byte width for all SOA types") {
        check_equal(soa_type_width(SOA_TYPE_I8), 1);
        check_equal(soa_type_width(SOA_TYPE_U8), 1);
        check_equal(soa_type_width(SOA_TYPE_I16), 2);
        check_equal(soa_type_width(SOA_TYPE_U16), 2);
        check_equal(soa_type_width(SOA_TYPE_I32), 4);
        check_equal(soa_type_width(SOA_TYPE_U32), 4);
        check_equal(soa_type_width(SOA_TYPE_I64), 8);
        check_equal(soa_type_width(SOA_TYPE_U64), 8);
        check_equal(soa_type_width(SOA_TYPE_F32), 4);
        check_equal(soa_type_width(SOA_TYPE_F64), 8);
    }
  }

  describe("Binary Parsing") {
    it("should successfully parse a complete batch of sensor data") {
        /* 2 rows of sensor data: timestamp(8) + sensor_id(2) + value(4) = 14 bytes per row */
        /* Header: count(4) + schema(2) + bitmap(1) = 7 bytes */
        /* Total: 7 + 2*8 + 2*2 + 2*4 = 7 + 16 + 4 + 8 = 35 bytes */
        uint8_t buf[64];
        uint8_t *p = buf;

        /* Header */
        write_le32(p, 2);  p += 4;           /* count = 2 */
        write_le16(p, 0x0001);  p += 2;      /* schema_id */
        *p++ = 0x07;                          /* bitmap: all 3 columns present */

        /* Column 0: timestamps (8 bytes each) */
        write_le64(p, 1000); p += 8;
        write_le64(p, 2000); p += 8;

        /* Column 1: sensor_ids (2 bytes each) */
        write_le16(p, 42); p += 2;
        write_le16(p, 43); p += 2;

        /* Column 2: values (4 bytes each) */
        write_f32(p, 3.14f); p += 4;
        write_f32(p, 2.71f); p += 4;

        size_t total = p - buf;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, total, &batch);

        check_equal(result, SOA_PARSE_OK);
        check_equal(batch.count, 2);
        check_equal(batch.schema_id, 0x0001);
        check_not_null(batch.schema);
        check_equal(batch.present_mask, 0x07);
        check_equal(batch.consumed, total);

        /* Verify column pointers */
        check_not_null(batch.columns[0]);
        check_not_null(batch.columns[1]);
        check_not_null(batch.columns[2]);

        /* Verify data access */
        check_equal((int)soa_get_i64(&batch, 0, 0), 1000);
        check_equal((int)soa_get_i64(&batch, 0, 1), 2000);
        check_equal(soa_get_u16(&batch, 1, 0), 42);
        check_equal(soa_get_u16(&batch, 1, 1), 43);
        check_within(soa_get_f32(&batch, 2, 0), 3.14f, 1e-6f);
        check_within(soa_get_f32(&batch, 2, 1), 2.71f, 1e-6f);
    }

    it("should correctly handle batches with partial columns present") {
        /* Only timestamp and value columns (skip sensor_id) */
        uint8_t buf[64];
        uint8_t *p = buf;

        /* Header */
        write_le32(p, 2);  p += 4;           /* count = 2 */
        write_le16(p, 0x0001);  p += 2;      /* schema_id */
        *p++ = 0x05;                          /* bitmap: columns 0 and 2 (skip 1) */

        /* Column 0: timestamps */
        write_le64(p, 1000); p += 8;
        write_le64(p, 2000); p += 8;

        /* Column 2: values (column 1 skipped) */
        write_f32(p, 3.14f); p += 4;
        write_f32(p, 2.71f); p += 4;

        size_t total = p - buf;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, total, &batch);

        check_equal(result, SOA_PARSE_OK);
        check_equal(batch.present_mask, 0x05);

        /* Column 0 and 2 should be present, column 1 should be NULL */
        check_not_null(batch.columns[0]);
        check_null(batch.columns[1]);
        check_not_null(batch.columns[2]);
    }

    it("should successfully parse an empty batch (0 rows)") {
        uint8_t buf[16];
        uint8_t *p = buf;

        write_le32(p, 0);  p += 4;           /* count = 0 */
        write_le16(p, 0x0001);  p += 2;      /* schema_id */
        *p++ = 0x07;                          /* bitmap */

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);

        check_equal(result, SOA_PARSE_OK);
        check_equal(batch.count, 0);
    }

    it("should return SOA_PARSE_NEED_MORE when header is incomplete") {
        uint8_t buf[4] = {0x01, 0x00, 0x00, 0x00};  /* Only count, no schema */

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, sizeof(buf), &batch);

        check_equal(result, SOA_PARSE_NEED_MORE);
    }

    it("should return SOA_PARSE_NEED_MORE when column data is missing") {
        uint8_t buf[16];
        uint8_t *p = buf;

        write_le32(p, 10);  p += 4;          /* count = 10 */
        write_le16(p, 0x0001);  p += 2;      /* schema_id */
        *p++ = 0x07;                          /* bitmap */
        /* No column data provided */

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);

        check_equal(result, SOA_PARSE_NEED_MORE);
    }

    it("should return SOA_PARSE_UNKNOWN_SCHEMA for unregistered schema IDs") {
        uint8_t buf[16];
        uint8_t *p = buf;

        write_le32(p, 1);  p += 4;
        write_le16(p, 0xFFFF);  p += 2;      /* Unknown schema */
        *p++ = 0x01;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);

        check_equal(result, SOA_PARSE_UNKNOWN_SCHEMA);
    }
  }

  describe("Header Peeking") {
    it("should correctly peek at batch header without consuming data") {
        uint8_t buf[16];
        write_le32(buf, 100);
        write_le16(buf + 4, 0x0042);

        uint32_t count = 0;
        uint16_t schema = 0;
        SoaParseResult result = soa_peek_header(buf, sizeof(buf), &count, &schema);

        check_equal(result, SOA_PARSE_OK);
        check_equal(count, 100);
        check_equal(schema, 0x0042);
    }
  }

  describe("Serialization (Building)") {
    it("should calculate correct wire size for various batch configurations") {
        /* All 3 columns present: header(7) + 10*(8+2+4) = 7 + 140 = 147 */
        size_t size = soa_wire_size(&sensor_schema, 10, 0x07);
        check_equal(size, 147);

        /* Only column 0 (I64): header(7) + 10*8 = 87 */
        size = soa_wire_size(&sensor_schema, 10, 0x01);
        check_equal(size, 87);

        /* No columns: just header */
        size = soa_wire_size(&sensor_schema, 10, 0x00);
        check_equal(size, 7);
    }

    it("should build a valid batch header correctly") {
        uint8_t buf[16];

        size_t written = soa_build_header(&sensor_schema, 42, 0x05, buf, sizeof(buf));

        check_equal(written, 7);

        /* Verify count */
        uint32_t count = buf[0] | ((uint32_t)buf[1] << 8) |
                         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
        check_equal(count, 42);

        /* Verify schema_id */
        uint16_t schema = buf[4] | ((uint16_t)buf[5] << 8);
        check_equal(schema, 0x0001);

        /* Verify bitmap */
        check_equal(buf[6], 0x05);
    }

    it("should return 0 when destination buffer is too small for header") {
        uint8_t buf[4];
        size_t written = soa_build_header(&sensor_schema, 42, 0x07, buf, sizeof(buf));
        check_equal(written, 0);
    }
  }

  describe("Data Accessors") {
    it("should correctly access I8 and U8 column data") {
        static const soa_schema_t byte_schema = {
            .schema_id = 0x0010,
            .column_count = 2,
            .columns = {
                {"signed", SOA_TYPE_I8},
                {"unsigned", SOA_TYPE_U8},
            }
        };
        soa_register_schema(&byte_schema);

        uint8_t buf[16];
        uint8_t *p = buf;
        write_le32(p, 2); p += 4;
        write_le16(p, 0x0010); p += 2;
        *p++ = 0x03;  /* Both columns */

        /* Column 0: signed bytes */
        *p++ = (uint8_t)-10;
        *p++ = (uint8_t)127;

        /* Column 1: unsigned bytes */
        *p++ = 200;
        *p++ = 255;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);
        check_equal(result, SOA_PARSE_OK);

        check_equal(soa_get_i8(&batch, 0, 0), -10);
        check_equal(soa_get_i8(&batch, 0, 1), 127);
        check_equal(soa_get_u8(&batch, 1, 0), 200);
        check_equal(soa_get_u8(&batch, 1, 1), 255);
    }

    it("should correctly access I32 and U32 column data") {
        static const soa_schema_t int_schema = {
            .schema_id = 0x0011,
            .column_count = 2,
            .columns = {
                {"signed", SOA_TYPE_I32},
                {"unsigned", SOA_TYPE_U32},
            }
        };
        soa_register_schema(&int_schema);

        uint8_t buf[32];
        uint8_t *p = buf;
        write_le32(p, 2); p += 4;
        write_le16(p, 0x0011); p += 2;
        *p++ = 0x03;

        write_le32(p, (uint32_t)-12345); p += 4;
        write_le32(p, (uint32_t)67890); p += 4;

        write_le32(p, 0xDEADBEEF); p += 4;
        write_le32(p, 0xCAFEBABE); p += 4;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);
        check_equal(result, SOA_PARSE_OK);

        check_equal(soa_get_i32(&batch, 0, 0), -12345);
        check_equal(soa_get_i32(&batch, 0, 1), 67890);
        check_equal(soa_get_u32(&batch, 1, 0), 0xDEADBEEF);
        check_equal(soa_get_u32(&batch, 1, 1), 0xCAFEBABE);
    }

    it("should correctly access F64 column data with appropriate precision") {
        static const soa_schema_t double_schema = {
            .schema_id = 0x0012,
            .column_count = 1,
            .columns = {
                {"value", SOA_TYPE_F64},
            }
        };
        soa_register_schema(&double_schema);

        uint8_t buf[32];
        uint8_t *p = buf;
        write_le32(p, 2); p += 4;
        write_le16(p, 0x0012); p += 2;
        *p++ = 0x01;

        union { double f; uint64_t u; } conv;
        conv.f = 3.14159265358979;
        write_le64(p, conv.u); p += 8;
        conv.f = 2.71828182845904;
        write_le64(p, conv.u); p += 8;

        soa_batch_t batch;
        SoaParseResult result = soa_parse(buf, p - buf, &batch);
        check_equal(result, SOA_PARSE_OK);

        check_within(soa_get_f64(&batch, 0, 0), 3.14159265358979, 1e-12);
        check_within(soa_get_f64(&batch, 0, 1), 2.71828182845904, 1e-12);
    }
  }

  describe("Utility Helpers") {
    it("should return human-readable strings for all parse results") {
        check_not_null(soa_parse_result_string(SOA_PARSE_OK));
        check_not_null(soa_parse_result_string(SOA_PARSE_NEED_MORE));
        check_not_null(soa_parse_result_string(SOA_PARSE_UNKNOWN_SCHEMA));
        check_not_null(soa_parse_result_string(SOA_PARSE_COLUMN_MISMATCH));
        check_not_null(soa_parse_result_string(SOA_PARSE_ROW_OVERFLOW));
    }

    it("should calculate correct header byte size based on column count") {
        check_equal(soa_header_size(1), 7);   /* 4 + 2 + 1 */
        check_equal(soa_header_size(8), 7);   /* 4 + 2 + 1 */
        check_equal(soa_header_size(9), 8);   /* 4 + 2 + 2 */
        check_equal(soa_header_size(16), 8);  /* 4 + 2 + 2 */
    }
  }
}
