#ifndef SOA_PARSER_H
#define SOA_PARSER_H

/**
 * @file soa_parser.h
 * @brief Struct-of-Arrays parser for batch columnar data
 *
 * Wire format:
 *   +--------+--------+--------+-------------+-------------+
 *   | Count  | Schema | Bitmap | Column 0    | Column 1... |
 *   | (4B LE)| (2B LE)| (N bits)| (count * W) |             |
 *   +--------+--------+--------+-------------+-------------+
 *
 * - Count: number of rows (uint32_t little-endian)
 * - Schema: schema ID to look up column definitions (uint16_t little-endian)
 * - Bitmap: presence bitmap, 1 bit per column (ceil(column_count/8) bytes)
 * - Columns: each present column stored contiguously, row-major within column
 *
 * Schema must be registered before parsing.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define SOA_MAX_COLUMNS    16
#define SOA_MAX_SCHEMAS    64
#define SOA_MAX_ROW_COUNT  (1024 * 1024)  /* 1M rows max */

/* Column data types */
typedef enum {
    SOA_TYPE_I8  = 0,
    SOA_TYPE_U8  = 1,
    SOA_TYPE_I16 = 2,
    SOA_TYPE_U16 = 3,
    SOA_TYPE_I32 = 4,
    SOA_TYPE_U32 = 5,
    SOA_TYPE_I64 = 6,
    SOA_TYPE_U64 = 7,
    SOA_TYPE_F32 = 8,
    SOA_TYPE_F64 = 9,
} SoaColumnType;

/* Parse result codes */
typedef enum {
    SOA_PARSE_OK = 0,
    SOA_PARSE_NEED_MORE,        /* Incomplete data */
    SOA_PARSE_UNKNOWN_SCHEMA,   /* Schema ID not registered */
    SOA_PARSE_COLUMN_MISMATCH,  /* Bitmap doesn't match schema */
    SOA_PARSE_ROW_OVERFLOW,     /* Too many rows */
    SOA_PARSE_INVALID_INPUT,    /* NULL pointer or bad parameter */
} SoaParseResult;

/* ============================================================================
 * Schema definition
 * ============================================================================ */

/**
 * @brief Column definition
 */
typedef struct {
    const char   *name;   /* Column name (for debugging, can be NULL) */
    SoaColumnType type;   /* Data type */
} soa_column_def_t;

/**
 * @brief Schema definition (compile-time)
 */
typedef struct {
    uint16_t         schema_id;
    uint8_t          column_count;
    soa_column_def_t columns[SOA_MAX_COLUMNS];
} soa_schema_t;

/**
 * @brief Register schema for parsing
 * @param schema  Schema definition (must remain valid for lifetime)
 * @return 0 on success, -1 if registry full
 */
int soa_register_schema(const soa_schema_t *schema);

/**
 * @brief Find registered schema by ID
 * @param schema_id  Schema identifier
 * @return Schema pointer or NULL if not found
 */
const soa_schema_t *soa_find_schema(uint16_t schema_id);

/**
 * @brief Get byte width of column type
 */
uint8_t soa_type_width(SoaColumnType type);

/* ============================================================================
 * Parsed batch
 * ============================================================================ */

/**
 * @brief Parsed batch (zero-copy)
 */
typedef struct {
    uint32_t           count;                      /* Row count */
    uint16_t           schema_id;                  /* Schema identifier */
    const soa_schema_t *schema;                    /* Resolved schema */
    uint16_t           present_mask;               /* Bitmap: which columns present */
    const uint8_t     *columns[SOA_MAX_COLUMNS];   /* Pointers to column data */
    size_t             consumed;                   /* Bytes consumed from input */
} soa_batch_t;

/* ============================================================================
 * Parsing
 * ============================================================================ */

/**
 * @brief Calculate header size for schema
 * @param column_count  Number of columns in schema
 * @return Header size in bytes (count + schema_id + bitmap)
 */
static inline size_t soa_header_size(uint8_t column_count) {
    return 4 + 2 + ((column_count + 7) / 8);
}

/**
 * @brief Parse batch (zero-copy)
 * @param data   Input buffer
 * @param len    Buffer length
 * @param out    Output batch structure
 * @return SOA_PARSE_OK on success
 *
 * IMPORTANT: Column pointers point directly into data buffer.
 * Caller must ensure data outlives batch usage.
 */
SoaParseResult soa_parse(const uint8_t *data, size_t len, soa_batch_t *out);

/**
 * @brief Peek header without full parse
 * @param data         Input buffer
 * @param len          Buffer length
 * @param out_count    Output: row count
 * @param out_schema   Output: schema ID
 * @return SOA_PARSE_OK on success
 */
SoaParseResult soa_peek_header(const uint8_t *data, size_t len,
                               uint32_t *out_count, uint16_t *out_schema);

/* ============================================================================
 * Column accessors (inline for performance)
 * ============================================================================ */

static inline int8_t soa_get_i8(const soa_batch_t *b, int col, uint32_t row) {
    return (int8_t)b->columns[col][row];
}

static inline uint8_t soa_get_u8(const soa_batch_t *b, int col, uint32_t row) {
    return b->columns[col][row];
}

static inline int16_t soa_get_i16(const soa_batch_t *b, int col, uint32_t row) {
    int16_t v;
    const uint8_t *p = b->columns[col] + row * 2;
    v = (int16_t)(p[0] | ((uint16_t)p[1] << 8));
    return v;
}

static inline uint16_t soa_get_u16(const soa_batch_t *b, int col, uint32_t row) {
    const uint8_t *p = b->columns[col] + row * 2;
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline int32_t soa_get_i32(const soa_batch_t *b, int col, uint32_t row) {
    int32_t v;
    const uint8_t *p = b->columns[col] + row * 4;
    v = (int32_t)(p[0] | ((uint32_t)p[1] << 8) |
                  ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    return v;
}

static inline uint32_t soa_get_u32(const soa_batch_t *b, int col, uint32_t row) {
    const uint8_t *p = b->columns[col] + row * 4;
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int64_t soa_get_i64(const soa_batch_t *b, int col, uint32_t row) {
    int64_t v;
    const uint8_t *p = b->columns[col] + row * 8;
    v = (int64_t)((uint64_t)p[0] | ((uint64_t)p[1] << 8) |
                  ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
                  ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                  ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56));
    return v;
}

static inline uint64_t soa_get_u64(const soa_batch_t *b, int col, uint32_t row) {
    const uint8_t *p = b->columns[col] + row * 8;
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static inline float soa_get_f32(const soa_batch_t *b, int col, uint32_t row) {
    union { uint32_t u; float f; } conv;
    conv.u = soa_get_u32(b, col, row);
    return conv.f;
}

static inline double soa_get_f64(const soa_batch_t *b, int col, uint32_t row) {
    union { uint64_t u; double f; } conv;
    conv.u = soa_get_u64(b, col, row);
    return conv.f;
}

/* ============================================================================
 * Building
 * ============================================================================ */

/**
 * @brief Calculate wire size for batch
 * @param schema        Schema definition
 * @param count         Row count
 * @param present_mask  Which columns are present
 * @return Total wire size in bytes
 */
size_t soa_wire_size(const soa_schema_t *schema, uint32_t count,
                     uint16_t present_mask);

/**
 * @brief Build batch header
 * @param schema        Schema definition
 * @param count         Row count
 * @param present_mask  Which columns are present
 * @param out           Output buffer
 * @param out_len       Output buffer size
 * @return Bytes written (header size), 0 if buffer too small
 */
size_t soa_build_header(const soa_schema_t *schema, uint32_t count,
                        uint16_t present_mask, uint8_t *out, size_t out_len);

/**
 * @brief Get error string
 */
const char *soa_parse_result_string(SoaParseResult result);

#ifdef __cplusplus
}
#endif

#endif /* SOA_PARSER_H */
