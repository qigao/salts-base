#include "soa_parser.h"
#include <string.h>

/* ============================================================================
 * Schema registry
 * ============================================================================ */

static const soa_schema_t *g_schemas[SOA_MAX_SCHEMAS];
static int g_schema_count = 0;

int soa_register_schema(const soa_schema_t *schema) {
    if (!schema || g_schema_count >= SOA_MAX_SCHEMAS)
        return -1;

    /* Check for duplicate */
    for (int i = 0; i < g_schema_count; i++) {
        if (g_schemas[i]->schema_id == schema->schema_id)
            return -1;
    }

    g_schemas[g_schema_count++] = schema;
    return 0;
}

const soa_schema_t *soa_find_schema(uint16_t schema_id) {
    for (int i = 0; i < g_schema_count; i++) {
        if (g_schemas[i]->schema_id == schema_id)
            return g_schemas[i];
    }
    return NULL;
}

uint8_t soa_type_width(SoaColumnType type) {
    static const uint8_t widths[] = {
        1, 1,  /* I8, U8 */
        2, 2,  /* I16, U16 */
        4, 4,  /* I32, U32 */
        8, 8,  /* I64, U64 */
        4, 8,  /* F32, F64 */
    };
    return (type <= SOA_TYPE_F64) ? widths[type] : 0;
}

/* ============================================================================
 * Parsing
 * ============================================================================ */

SoaParseResult soa_peek_header(const uint8_t *data, size_t len,
                               uint32_t *out_count, uint16_t *out_schema) {
    if (!data || !out_count || !out_schema)
        return SOA_PARSE_INVALID_INPUT;

    if (len < 6)
        return SOA_PARSE_NEED_MORE;

    /* Read count (4 bytes LE) */
    *out_count = data[0] | ((uint32_t)data[1] << 8) |
                 ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

    /* Read schema ID (2 bytes LE) */
    *out_schema = data[4] | ((uint16_t)data[5] << 8);

    return SOA_PARSE_OK;
}

SoaParseResult soa_parse(const uint8_t *data, size_t len, soa_batch_t *out) {
    if (!data || !out)
        return SOA_PARSE_INVALID_INPUT;

    /* Peek header */
    uint32_t count;
    uint16_t schema_id;
    SoaParseResult peek = soa_peek_header(data, len, &count, &schema_id);
    if (peek != SOA_PARSE_OK)
        return peek;

    if (count > SOA_MAX_ROW_COUNT)
        return SOA_PARSE_ROW_OVERFLOW;

    /* Find schema */
    const soa_schema_t *schema = soa_find_schema(schema_id);
    if (!schema)
        return SOA_PARSE_UNKNOWN_SCHEMA;

    /* Calculate header size and check for bitmap */
    size_t bitmap_bytes = (schema->column_count + 7) / 8;
    size_t header = 6 + bitmap_bytes;

    if (len < header)
        return SOA_PARSE_NEED_MORE;

    /* Read presence bitmap */
    uint16_t present_mask = 0;
    for (size_t i = 0; i < bitmap_bytes; i++) {
        present_mask |= (uint16_t)data[6 + i] << (i * 8);
    }

    /* Calculate data size and set column pointers */
    size_t offset = header;
    memset(out->columns, 0, sizeof(out->columns));

    for (int col = 0; col < schema->column_count; col++) {
        if (!(present_mask & (1 << col)))
            continue;

        uint8_t width = soa_type_width(schema->columns[col].type);
        size_t col_size = (size_t)count * width;

        if (offset + col_size > len)
            return SOA_PARSE_NEED_MORE;

        out->columns[col] = data + offset;
        offset += col_size;
    }

    out->count = count;
    out->schema_id = schema_id;
    out->schema = schema;
    out->present_mask = present_mask;
    out->consumed = offset;

    return SOA_PARSE_OK;
}

/* ============================================================================
 * Building
 * ============================================================================ */

size_t soa_wire_size(const soa_schema_t *schema, uint32_t count,
                     uint16_t present_mask) {
    if (!schema)
        return 0;

    size_t bitmap_bytes = (schema->column_count + 7) / 8;
    size_t size = 6 + bitmap_bytes;

    for (int col = 0; col < schema->column_count; col++) {
        if (present_mask & (1 << col)) {
            uint8_t width = soa_type_width(schema->columns[col].type);
            size += (size_t)count * width;
        }
    }

    return size;
}

size_t soa_build_header(const soa_schema_t *schema, uint32_t count,
                        uint16_t present_mask, uint8_t *out, size_t out_len) {
    if (!schema || !out)
        return 0;

    size_t bitmap_bytes = (schema->column_count + 7) / 8;
    size_t header_size = 6 + bitmap_bytes;

    if (out_len < header_size)
        return 0;

    /* Write count (4 bytes LE) */
    out[0] = (uint8_t)(count);
    out[1] = (uint8_t)(count >> 8);
    out[2] = (uint8_t)(count >> 16);
    out[3] = (uint8_t)(count >> 24);

    /* Write schema ID (2 bytes LE) */
    out[4] = (uint8_t)(schema->schema_id);
    out[5] = (uint8_t)(schema->schema_id >> 8);

    /* Write presence bitmap */
    for (size_t i = 0; i < bitmap_bytes; i++) {
        out[6 + i] = (uint8_t)(present_mask >> (i * 8));
    }

    return header_size;
}

const char *soa_parse_result_string(SoaParseResult result) {
    switch (result) {
    case SOA_PARSE_OK:              return "OK";
    case SOA_PARSE_NEED_MORE:       return "Need more data";
    case SOA_PARSE_UNKNOWN_SCHEMA:  return "Unknown schema";
    case SOA_PARSE_COLUMN_MISMATCH: return "Column mismatch";
    case SOA_PARSE_ROW_OVERFLOW:    return "Row count overflow";
    case SOA_PARSE_INVALID_INPUT:   return "Invalid input";
    default:                        return "Unknown error";
    }
}
