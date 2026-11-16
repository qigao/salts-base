#ifndef MODBUS_PARSER_H
#define MODBUS_PARSER_H

/**
 * @file modbus_parser.h
 * @brief Modbus TCP and RTU ADU parser with LTV/TLV-style result semantics
 *
 * Modbus TCP wire format:
 *   +----------------+---------------+--------+----------+
 *   | MBAP header    | Unit ID       | Func   | Data     |
 *   | 6 bytes        | 1 byte        | 1 byte | N bytes  |
 *   +----------------+---------------+--------+----------+
 *
 * MBAP header fields are big-endian:
 *   transaction_id(2), protocol_id(2), length(2).
 * The length field counts Unit ID + PDU bytes.
 *
 * Modbus RTU wire format:
 *   +----------+----------+----------+-----------+
 *   | Address  | Func     | Data     | CRC16     |
 *   | 1 byte   | 1 byte   | N bytes  | 2 bytes LE|
 *   +----------+----------+----------+-----------+
 *
 * RTU has no embedded length for all function codes. A generic byte stream
 * parser needs an external frame boundary, usually the RTU silent interval.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_TCP_MBAP_SIZE      7
#define MODBUS_TCP_MIN_ADU_SIZE   8
#define MODBUS_TCP_MAX_ADU_SIZE   260
#define MODBUS_MAX_PDU_SIZE       253

#define MODBUS_RTU_MIN_ADU_SIZE   4
#define MODBUS_RTU_MAX_ADU_SIZE   256

typedef enum {
    MODBUS_PARSE_OK = 0,
    MODBUS_PARSE_NEED_MORE,
    MODBUS_PARSE_INVALID_INPUT,
    MODBUS_PARSE_INVALID_PROTOCOL,
    MODBUS_PARSE_INVALID_LENGTH,
    MODBUS_PARSE_CRC_MISMATCH,
    MODBUS_PARSE_BUFFER_OVERFLOW,
} ModbusParseResult;

typedef enum {
    MODBUS_TRANSPORT_TCP = 0,
    MODBUS_TRANSPORT_RTU = 1,
} ModbusTransport;

typedef struct {
    uint8_t        function_code;
    const uint8_t *data;
    size_t         data_size;
} modbus_pdu_t;

typedef struct {
    uint16_t     transaction_id;
    uint16_t     protocol_id;
    uint16_t     length;
    uint8_t      unit_id;
    modbus_pdu_t pdu;
    size_t       consumed;
} modbus_tcp_adu_t;

typedef struct {
    uint8_t      address;
    modbus_pdu_t pdu;
    uint16_t     crc;
    size_t       consumed;
} modbus_rtu_adu_t;

typedef struct {
    ModbusTransport transport;
    union {
        modbus_tcp_adu_t tcp;
        modbus_rtu_adu_t rtu;
    } frame;
} modbus_adu_t;

typedef struct modbus_stream_s modbus_stream_t;

ModbusParseResult modbus_tcp_peek_size(const uint8_t *data, size_t len,
                                       size_t *out_size);
ModbusParseResult modbus_tcp_parse(const uint8_t *data, size_t len,
                                   modbus_tcp_adu_t *out);
ModbusParseResult modbus_tcp_read(const uint8_t *data, size_t len,
                                  modbus_tcp_adu_t *out);
size_t modbus_tcp_build(uint16_t transaction_id, uint8_t unit_id,
                        uint8_t function_code, const uint8_t *pdu_data,
                        size_t pdu_data_size, uint8_t *out, size_t out_len);
size_t modbus_tcp_write(const modbus_tcp_adu_t *adu, uint8_t *out,
                        size_t out_len);

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t len);
ModbusParseResult modbus_rtu_parse(const uint8_t *data, size_t len,
                                   modbus_rtu_adu_t *out);
ModbusParseResult modbus_rtu_read(const uint8_t *data, size_t len,
                                  modbus_rtu_adu_t *out);
size_t modbus_rtu_build(uint8_t address, uint8_t function_code,
                        const uint8_t *pdu_data, size_t pdu_data_size,
                        uint8_t *out, size_t out_len);
size_t modbus_rtu_write(const modbus_rtu_adu_t *adu, uint8_t *out,
                        size_t out_len);

ModbusParseResult modbus_read(ModbusTransport transport, const uint8_t *data,
                              size_t len, modbus_adu_t *out);
size_t modbus_write(const modbus_adu_t *adu, uint8_t *out, size_t out_len);

modbus_stream_t *modbus_stream_create(ModbusTransport transport,
                                      size_t buffer_size);
/**
 * @brief Feed bytes to a stream parser.
 *
 * For TCP, this returns MODBUS_PARSE_OK as soon as one full MBAP-sized ADU is
 * available. Remaining bytes stay buffered for the next call.
 *
 * For RTU, calls with data only buffer bytes and return MODBUS_PARSE_NEED_MORE.
 * Call with data == NULL and len == 0 at an external RTU frame boundary to
 * parse the buffered bytes as one complete RTU ADU.
 */
ModbusParseResult modbus_stream_feed(modbus_stream_t *stream,
                                     const uint8_t *data, size_t len,
                                     modbus_adu_t *out);
void modbus_stream_reset(modbus_stream_t *stream);
void modbus_stream_destroy(modbus_stream_t *stream);

const char *modbus_parse_result_string(ModbusParseResult result);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_PARSER_H */
