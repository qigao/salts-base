#include "parser_error.h"
#include <string.h>
#include <stdio.h>

const char* parse_error_string(ParseError err) {
    switch (err) {
        case PARSE_OK: return "Success";
        case PARSE_ERR_TRUNCATED: return "Truncated frame";
        case PARSE_ERR_INVALID_HEAD: return "Invalid frame header";
        case PARSE_ERR_INVALID_TAIL: return "Invalid frame tail";
        case PARSE_ERR_CRC_MISMATCH: return "CRC mismatch";
        case PARSE_ERR_INVALID_VERSION: return "Invalid version";
        case PARSE_ERR_PAYLOAD_TOO_LARGE: return "Payload too large";
        case PARSE_ERR_OUT_OF_MEMORY: return "Out of memory";
        case PARSE_ERR_INVALID_PAYLOAD_TYPE: return "Invalid payload type";
        case PARSE_ERR_BUFFER_OVERFLOW: return "Buffer overflow";
        default: return "Unknown error";
    }
}

void parse_error_set(ParseErrorInfo *info, ParseError code,
                     size_t offset, uint32_t msg_id, const char *message) {
    info->code = code;
    info->offset = offset;
    info->msg_id = msg_id;

    if (message) {
        strncpy(info->message, message, sizeof(info->message) - 1);
        info->message[sizeof(info->message) - 1] = '\0';
    } else {
        strncpy(info->message, parse_error_string(code), sizeof(info->message) - 1);
        info->message[sizeof(info->message) - 1] = '\0';
    }
}
