#include "modbus_parser.h"
#include "tinytest.h"
#include <string.h>

spec("modbus_parser") {
  describe("Modbus TCP") {
    it("should build and parse a Read Holding Registers request") {
      uint8_t pdu_data[] = {0x00, 0x6B, 0x00, 0x03};
      uint8_t buf[MODBUS_TCP_MAX_ADU_SIZE];

      size_t written = modbus_tcp_build(1, 0x11, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));
      check_equal(written, 12);
      check_equal(buf[0], 0x00);
      check_equal(buf[1], 0x01);
      check_equal(buf[2], 0x00);
      check_equal(buf[3], 0x00);
      check_equal(buf[4], 0x00);
      check_equal(buf[5], 0x06);

      modbus_tcp_adu_t adu;
      ModbusParseResult result = modbus_tcp_parse(buf, written, &adu);

      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.transaction_id, 1);
      check_equal(adu.protocol_id, 0);
      check_equal(adu.length, 6);
      check_equal(adu.unit_id, 0x11);
      check_equal(adu.pdu.function_code, 0x03);
      check_equal(adu.pdu.data_size, sizeof(pdu_data));
      check_equal(adu.pdu.data, pdu_data, sizeof(pdu_data));
      check_equal(adu.consumed, written);
    }

    it("should peek TCP ADU size") {
      uint8_t buf[] = {
          0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
          0x01, 0x03, 0x00, 0x6B, 0x00, 0x03
      };
      size_t size = 0;

      ModbusParseResult result = modbus_tcp_peek_size(buf, sizeof(buf), &size);

      check_equal(result, MODBUS_PARSE_OK);
      check_equal(size, sizeof(buf));
    }

    it("should signal need more for partial TCP header") {
      uint8_t buf[] = {0x00, 0x01, 0x00};
      size_t size = 0;

      ModbusParseResult result = modbus_tcp_peek_size(buf, sizeof(buf), &size);

      check_equal(result, MODBUS_PARSE_NEED_MORE);
    }

    it("should reject non-zero protocol id") {
      uint8_t buf[] = {
          0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
          0x01, 0x03
      };
      modbus_tcp_adu_t adu;

      ModbusParseResult result = modbus_tcp_parse(buf, sizeof(buf), &adu);

      check_equal(result, MODBUS_PARSE_INVALID_PROTOCOL);
    }

    it("should write a TCP struct to binary") {
      uint8_t pdu_data[] = {0x00, 0x6B, 0x00, 0x03};
      modbus_tcp_adu_t adu = {
          .transaction_id = 0x1234,
          .protocol_id = 0,
          .unit_id = 0x11,
          .pdu = {
              .function_code = 0x03,
              .data = pdu_data,
              .data_size = sizeof(pdu_data),
          },
      };
      uint8_t buf[MODBUS_TCP_MAX_ADU_SIZE];

      size_t written = modbus_tcp_write(&adu, buf, sizeof(buf));

      check_equal(written, 12);
      check_equal(buf[0], 0x12);
      check_equal(buf[1], 0x34);
      check_equal(buf[4], 0x00);
      check_equal(buf[5], 0x06);

      modbus_tcp_adu_t parsed;
      ModbusParseResult result = modbus_tcp_read(buf, written, &parsed);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(parsed.transaction_id, 0x1234);
      check_equal(parsed.unit_id, 0x11);
      check_equal(parsed.pdu.data, pdu_data, sizeof(pdu_data));
    }

    it("should reject writing a TCP struct with invalid protocol id") {
      modbus_tcp_adu_t adu = {
          .transaction_id = 1,
          .protocol_id = 1,
          .unit_id = 1,
          .pdu = {.function_code = 0x03},
      };
      uint8_t buf[MODBUS_TCP_MAX_ADU_SIZE];

      size_t written = modbus_tcp_write(&adu, buf, sizeof(buf));

      check_equal(written, 0);
    }
  }

  describe("Modbus RTU") {
    it("should calculate known CRC16 example") {
      uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

      uint16_t crc = modbus_rtu_crc16(request, sizeof(request));

      check_equal(crc, 0xCDC5);
    }

    it("should build and parse an RTU request") {
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      uint8_t buf[MODBUS_RTU_MAX_ADU_SIZE];

      size_t written = modbus_rtu_build(0x01, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));
      check_equal(written, 8);
      check_equal(buf[6], 0xC5);
      check_equal(buf[7], 0xCD);

      modbus_rtu_adu_t adu;
      ModbusParseResult result = modbus_rtu_parse(buf, written, &adu);

      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.address, 0x01);
      check_equal(adu.pdu.function_code, 0x03);
      check_equal(adu.pdu.data_size, sizeof(pdu_data));
      check_equal(adu.pdu.data, pdu_data, sizeof(pdu_data));
      check_equal(adu.crc, 0xCDC5);
      check_equal(adu.consumed, written);
    }

    it("should reject corrupted RTU CRC") {
      uint8_t buf[MODBUS_RTU_MAX_ADU_SIZE];
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      size_t written = modbus_rtu_build(0x01, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));
      buf[written - 1] ^= 0xFF;

      modbus_rtu_adu_t adu;
      ModbusParseResult result = modbus_rtu_parse(buf, written, &adu);

      check_equal(result, MODBUS_PARSE_CRC_MISMATCH);
    }

    it("should write an RTU struct to binary") {
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      modbus_rtu_adu_t adu = {
          .address = 0x01,
          .pdu = {
              .function_code = 0x03,
              .data = pdu_data,
              .data_size = sizeof(pdu_data),
          },
      };
      uint8_t buf[MODBUS_RTU_MAX_ADU_SIZE];

      size_t written = modbus_rtu_write(&adu, buf, sizeof(buf));

      check_equal(written, 8);
      check_equal(buf[6], 0xC5);
      check_equal(buf[7], 0xCD);

      modbus_rtu_adu_t parsed;
      ModbusParseResult result = modbus_rtu_read(buf, written, &parsed);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(parsed.address, 0x01);
      check_equal(parsed.pdu.data, pdu_data, sizeof(pdu_data));
    }
  }

  describe("Generic read/write") {
    it("should read and write a generic TCP ADU") {
      uint8_t pdu_data[] = {0x00, 0x01};
      uint8_t buf[MODBUS_TCP_MAX_ADU_SIZE];
      size_t written = modbus_tcp_build(9, 0x01, 0x06, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));

      modbus_adu_t adu;
      ModbusParseResult result = modbus_read(MODBUS_TRANSPORT_TCP, buf, written, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.transport, MODBUS_TRANSPORT_TCP);

      uint8_t out[MODBUS_TCP_MAX_ADU_SIZE];
      size_t out_len = modbus_write(&adu, out, sizeof(out));

      check_equal(out_len, written);
      check_equal(out, buf, written);
    }

    it("should read and write a generic RTU ADU") {
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      uint8_t buf[MODBUS_RTU_MAX_ADU_SIZE];
      size_t written = modbus_rtu_build(1, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));

      modbus_adu_t adu;
      ModbusParseResult result = modbus_read(MODBUS_TRANSPORT_RTU, buf, written, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.transport, MODBUS_TRANSPORT_RTU);

      uint8_t out[MODBUS_RTU_MAX_ADU_SIZE];
      size_t out_len = modbus_write(&adu, out, sizeof(out));

      check_equal(out_len, written);
      check_equal(out, buf, written);
    }
  }

  describe("Stream parser") {
    it("should parse a TCP ADU fed in chunks") {
      uint8_t pdu_data[] = {0x00, 0x6B, 0x00, 0x03};
      uint8_t buf[MODBUS_TCP_MAX_ADU_SIZE];
      size_t written = modbus_tcp_build(7, 0x11, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));
      modbus_stream_t *stream = modbus_stream_create(MODBUS_TRANSPORT_TCP, 0);
      check_not_null(stream);

      modbus_adu_t adu;
      ModbusParseResult result = modbus_stream_feed(stream, buf, 5, &adu);
      check_equal(result, MODBUS_PARSE_NEED_MORE);

      result = modbus_stream_feed(stream, buf + 5, written - 5, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.transport, MODBUS_TRANSPORT_TCP);
      check_equal(adu.frame.tcp.transaction_id, 7);
      check_equal(adu.frame.tcp.pdu.function_code, 0x03);

      modbus_stream_destroy(stream);
    }

    it("should parse multiple TCP ADUs in sequence") {
      uint8_t frame1[MODBUS_TCP_MAX_ADU_SIZE];
      uint8_t frame2[MODBUS_TCP_MAX_ADU_SIZE];
      uint8_t data[] = {0x00, 0x01};
      size_t len1 = modbus_tcp_build(1, 0x01, 0x06, data, sizeof(data),
                                     frame1, sizeof(frame1));
      size_t len2 = modbus_tcp_build(2, 0x01, 0x03, data, sizeof(data),
                                     frame2, sizeof(frame2));
      uint8_t combined[MODBUS_TCP_MAX_ADU_SIZE * 2];
      memcpy(combined, frame1, len1);
      memcpy(combined + len1, frame2, len2);

      modbus_stream_t *stream = modbus_stream_create(MODBUS_TRANSPORT_TCP, sizeof(combined));
      check_not_null(stream);

      modbus_adu_t adu;
      ModbusParseResult result = modbus_stream_feed(stream, combined, len1 + len2, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.frame.tcp.transaction_id, 1);

      result = modbus_stream_feed(stream, NULL, 0, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.frame.tcp.transaction_id, 2);

      modbus_stream_destroy(stream);
    }

    it("should parse RTU only when caller flushes an external frame boundary") {
      uint8_t pdu_data[] = {0x00, 0x00, 0x00, 0x0A};
      uint8_t buf[MODBUS_RTU_MAX_ADU_SIZE];
      size_t written = modbus_rtu_build(0x01, 0x03, pdu_data, sizeof(pdu_data),
                                        buf, sizeof(buf));
      modbus_stream_t *stream = modbus_stream_create(MODBUS_TRANSPORT_RTU, 0);
      check_not_null(stream);

      modbus_adu_t adu;
      ModbusParseResult result = modbus_stream_feed(stream, buf, 4, &adu);
      check_equal(result, MODBUS_PARSE_NEED_MORE);

      result = modbus_stream_feed(stream, buf + 4, written - 4, &adu);
      check_equal(result, MODBUS_PARSE_NEED_MORE);

      result = modbus_stream_feed(stream, NULL, 0, &adu);
      check_equal(result, MODBUS_PARSE_OK);
      check_equal(adu.transport, MODBUS_TRANSPORT_RTU);
      check_equal(adu.frame.rtu.address, 0x01);
      check_equal(adu.frame.rtu.pdu.function_code, 0x03);

      modbus_stream_destroy(stream);
    }
  }

  describe("Utilities") {
    it("should return result strings") {
      check_not_null(modbus_parse_result_string(MODBUS_PARSE_OK));
      check_not_null(modbus_parse_result_string(MODBUS_PARSE_NEED_MORE));
      check_not_null(modbus_parse_result_string(MODBUS_PARSE_CRC_MISMATCH));
    }
  }
}
