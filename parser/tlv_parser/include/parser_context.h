#ifndef PARSER_CONTEXT_H
#define PARSER_CONTEXT_H

#include "memory_pool.h"
#include "parser_error.h"
#include "parser_stats.h"
#include <stdint.h>

// Thread-safe parser context
typedef struct {
  MemoryPool *pool;
  ParseErrorInfo last_error;
  ParserStats stats;
} ParserContext;

// Get thread-local parser context
ParserContext *parser_get_context(void);

// Initialize parser context
ParserContext *parser_context_create(size_t pool_size);

// Destroy parser context
void parser_context_destroy(ParserContext *ctx);

// Get statistics from context
ParserStats *parser_context_get_stats(ParserContext *ctx);

// Get last error from context
ParseErrorInfo *parser_context_get_error(ParserContext *ctx);

#endif // PARSER_CONTEXT_H
