#include "modbus_parser.h"
#include <stdlib.h>
#include <string.h>

struct modbus_stream_s {
    ModbusTransport transport;
    uint8_t        *buffer;
    size_t          buffer_size;
    size_t          buffered;
    size_t          expected;
};

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int modbus_pdu_size_valid(size_t data_size) {
    return data_size <= (MODBUS_MAX_PDU_SIZE - 1);
}

ModbusParseResult modbus_tcp_peek_size(const uint8_t *data, size_t len,
                                       size_t *out_size) {
    if (!data || !out_size)
        return MODBUS_PARSE_INVALID_INPUT;

    if (len < MODBUS_TCP_MBAP_SIZE)
        return MODBUS_PARSE_NEED_MORE;

    uint16_t protocol_id = read_be16(data + 2);
    if (protocol_id != 0)
        return MODBUS_PARSE_INVALID_PROTOCOL;

    uint16_t length = read_be16(data + 4);
    if (length < 2 || length > (MODBUS_MAX_PDU_SIZE + 1))
        return MODBUS_PARSE_INVALID_LENGTH;

    *out_size = 6u + (size_t)length;
    return MODBUS_PARSE_OK;
}

ModbusParseResult modbus_tcp_parse(const uint8_t *data, size_t len,
                                   modbus_tcp_adu_t *out) {
    if (!data || !out)
        return MODBUS_PARSE_INVALID_INPUT;

    size_t total;
    ModbusParseResult peek = modbus_tcp_peek_size(data, len, &total);
    if (peek != MODBUS_PARSE_OK)
        return peek;

    if (len < total)
        return MODBUS_PARSE_NEED_MORE;

    uint16_t length = read_be16(data + 4);
    size_t pdu_size = (size_t)length - 1;

    out->transaction_id = read_be16(data);
    out->protocol_id = read_be16(data + 2);
    out->length = length;
    out->unit_id = data[6];
    out->pdu.function_code = data[7];
    out->pdu.data_size = pdu_size - 1;
    out->pdu.data = out->pdu.data_size > 0 ? data + 8 : NULL;
    out->consumed = total;

    return MODBUS_PARSE_OK;
}

ModbusParseResult modbus_tcp_read(const uint8_t *data, size_t len,
                                  modbus_tcp_adu_t *out) {
    return modbus_tcp_parse(data, len, out);
}

size_t modbus_tcp_build(uint16_t transaction_id, uint8_t unit_id,
                        uint8_t function_code, const uint8_t *pdu_data,
                        size_t pdu_data_size, uint8_t *out, size_t out_len) {
    if (!out || !modbus_pdu_size_valid(pdu_data_size))
        return 0;
    if (pdu_data_size > 0 && !pdu_data)
        return 0;

    size_t total = MODBUS_TCP_MIN_ADU_SIZE + pdu_data_size;
    if (out_len < total)
        return 0;

    write_be16(out, transaction_id);
    write_be16(out + 2, 0);
    write_be16(out + 4, (uint16_t)(2 + pdu_data_size));
    out[6] = unit_id;
    out[7] = function_code;
    if (pdu_data_size > 0)
        memcpy(out + 8, pdu_data, pdu_data_size);

    return total;
}

size_t modbus_tcp_write(const modbus_tcp_adu_t *adu, uint8_t *out,
                        size_t out_len) {
    if (!adu || adu->protocol_id != 0)
        return 0;

    return modbus_tcp_build(adu->transaction_id, adu->unit_id,
                            adu->pdu.function_code, adu->pdu.data,
                            adu->pdu.data_size, out, out_len);
}

uint16_t modbus_rtu_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;

    if (!data && len > 0)
        return 0;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

ModbusParseResult modbus_rtu_parse(const uint8_t *data, size_t len,
                                   modbus_rtu_adu_t *out) {
    if (!data || !out)
        return MODBUS_PARSE_INVALID_INPUT;

    if (len < MODBUS_RTU_MIN_ADU_SIZE)
        return MODBUS_PARSE_NEED_MORE;

    if (len > MODBUS_RTU_MAX_ADU_SIZE)
        return MODBUS_PARSE_INVALID_LENGTH;

    uint16_t expected = modbus_rtu_crc16(data, len - 2);
    uint16_t wire_crc = (uint16_t)(data[len - 2] | ((uint16_t)data[len - 1] << 8));
    if (expected != wire_crc)
        return MODBUS_PARSE_CRC_MISMATCH;

    out->address = data[0];
    out->pdu.function_code = data[1];
    out->pdu.data_size = len - 4;
    out->pdu.data = out->pdu.data_size > 0 ? data + 2 : NULL;
    out->crc = wire_crc;
    out->consumed = len;

    return MODBUS_PARSE_OK;
}

ModbusParseResult modbus_rtu_read(const uint8_t *data, size_t len,
                                  modbus_rtu_adu_t *out) {
    return modbus_rtu_parse(data, len, out);
}

size_t modbus_rtu_build(uint8_t address, uint8_t function_code,
                        const uint8_t *pdu_data, size_t pdu_data_size,
                        uint8_t *out, size_t out_len) {
    if (!out || !modbus_pdu_size_valid(pdu_data_size))
        return 0;
    if (pdu_data_size > 0 && !pdu_data)
        return 0;

    size_t total = MODBUS_RTU_MIN_ADU_SIZE + pdu_data_size;
    if (out_len < total)
        return 0;

    out[0] = address;
    out[1] = function_code;
    if (pdu_data_size > 0)
        memcpy(out + 2, pdu_data, pdu_data_size);

    uint16_t crc = modbus_rtu_crc16(out, total - 2);
    out[total - 2] = (uint8_t)crc;
    out[total - 1] = (uint8_t)(crc >> 8);

    return total;
}

size_t modbus_rtu_write(const modbus_rtu_adu_t *adu, uint8_t *out,
                        size_t out_len) {
    if (!adu)
        return 0;

    return modbus_rtu_build(adu->address, adu->pdu.function_code,
                            adu->pdu.data, adu->pdu.data_size, out, out_len);
}

ModbusParseResult modbus_read(ModbusTransport transport, const uint8_t *data,
                              size_t len, modbus_adu_t *out) {
    if (!out)
        return MODBUS_PARSE_INVALID_INPUT;

    if (transport == MODBUS_TRANSPORT_TCP) {
        ModbusParseResult result = modbus_tcp_read(data, len, &out->frame.tcp);
        if (result == MODBUS_PARSE_OK)
            out->transport = MODBUS_TRANSPORT_TCP;
        return result;
    }

    if (transport == MODBUS_TRANSPORT_RTU) {
        ModbusParseResult result = modbus_rtu_read(data, len, &out->frame.rtu);
        if (result == MODBUS_PARSE_OK)
            out->transport = MODBUS_TRANSPORT_RTU;
        return result;
    }

    return MODBUS_PARSE_INVALID_INPUT;
}

size_t modbus_write(const modbus_adu_t *adu, uint8_t *out, size_t out_len) {
    if (!adu)
        return 0;

    if (adu->transport == MODBUS_TRANSPORT_TCP)
        return modbus_tcp_write(&adu->frame.tcp, out, out_len);

    if (adu->transport == MODBUS_TRANSPORT_RTU)
        return modbus_rtu_write(&adu->frame.rtu, out, out_len);

    return 0;
}

modbus_stream_t *modbus_stream_create(ModbusTransport transport,
                                      size_t buffer_size) {
    if (buffer_size == 0)
        buffer_size = MODBUS_TCP_MAX_ADU_SIZE;

    modbus_stream_t *stream = (modbus_stream_t *)calloc(1, sizeof(*stream));
    if (!stream)
        return NULL;

    stream->buffer = (uint8_t *)malloc(buffer_size);
    if (!stream->buffer) {
        free(stream);
        return NULL;
    }

    stream->transport = transport;
    stream->buffer_size = buffer_size;
    return stream;
}

ModbusParseResult modbus_stream_feed(modbus_stream_t *stream,
                                     const uint8_t *data, size_t len,
                                     modbus_adu_t *out) {
    if (!stream || !out)
        return MODBUS_PARSE_INVALID_INPUT;
    if (len > 0 && !data)
        return MODBUS_PARSE_INVALID_INPUT;

    if (stream->buffered + len > stream->buffer_size)
        return MODBUS_PARSE_BUFFER_OVERFLOW;

    if (len > 0) {
        memcpy(stream->buffer + stream->buffered, data, len);
        stream->buffered += len;
    }

    if (stream->transport == MODBUS_TRANSPORT_RTU) {
        if (len > 0)
            return MODBUS_PARSE_NEED_MORE;
        if (stream->buffered == 0)
            return MODBUS_PARSE_NEED_MORE;

        ModbusParseResult result = modbus_rtu_parse(stream->buffer, stream->buffered,
                                                    &out->frame.rtu);
        if (result != MODBUS_PARSE_OK)
            return result;
        out->transport = MODBUS_TRANSPORT_RTU;
        stream->buffered = 0;
        stream->expected = 0;
        return MODBUS_PARSE_OK;
    }

    if (stream->expected == 0) {
        size_t expected;
        ModbusParseResult peek = modbus_tcp_peek_size(stream->buffer, stream->buffered,
                                                      &expected);
        if (peek != MODBUS_PARSE_OK)
            return peek;
        if (expected > stream->buffer_size)
            return MODBUS_PARSE_BUFFER_OVERFLOW;
        stream->expected = expected;
    }

    if (stream->buffered < stream->expected)
        return MODBUS_PARSE_NEED_MORE;

    ModbusParseResult result = modbus_tcp_parse(stream->buffer, stream->expected,
                                                &out->frame.tcp);
    if (result != MODBUS_PARSE_OK)
        return result;

    out->transport = MODBUS_TRANSPORT_TCP;

    size_t remaining = stream->buffered - stream->expected;
    if (remaining > 0)
        memmove(stream->buffer, stream->buffer + stream->expected, remaining);

    stream->buffered = remaining;
    stream->expected = 0;
    return MODBUS_PARSE_OK;
}

void modbus_stream_reset(modbus_stream_t *stream) {
    if (stream) {
        stream->buffered = 0;
        stream->expected = 0;
    }
}

void modbus_stream_destroy(modbus_stream_t *stream) {
    if (stream) {
        free(stream->buffer);
        free(stream);
    }
}

const char *modbus_parse_result_string(ModbusParseResult result) {
    switch (result) {
    case MODBUS_PARSE_OK:               return "OK";
    case MODBUS_PARSE_NEED_MORE:        return "Need more data";
    case MODBUS_PARSE_INVALID_INPUT:    return "Invalid input";
    case MODBUS_PARSE_INVALID_PROTOCOL: return "Invalid protocol";
    case MODBUS_PARSE_INVALID_LENGTH:   return "Invalid length";
    case MODBUS_PARSE_CRC_MISMATCH:     return "CRC mismatch";
    case MODBUS_PARSE_BUFFER_OVERFLOW:  return "Buffer overflow";
    default:                            return "Unknown error";
    }
}
