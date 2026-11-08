#include "parser_context.h"
#include <stdlib.h>
#include <string.h>

// Thread-local storage
#ifdef _WIN32
  #include <windows.h>
static DWORD tls_key = TLS_OUT_OF_INDEXES;
#else
  #include <pthread.h>
static pthread_key_t tls_key;
static pthread_once_t tls_once = PTHREAD_ONCE_INIT;
#endif

static void tls_init(void) {
#ifdef _WIN32
  tls_key = TlsAlloc();
#else
  pthread_key_create(&tls_key, NULL);
#endif
}

ParserContext *parser_context_create(size_t pool_size) {
  if (pool_size == 0) {
    pool_size = 1024 * 1024; // Default 1MB
  }

  ParserContext *ctx = malloc(sizeof(ParserContext));
  if (!ctx)
    return NULL;

  ctx->pool = pool_create(pool_size);
  if (!ctx->pool) {
    free(ctx);
    return NULL;
  }

  memset(&ctx->last_error, 0, sizeof(ParseErrorInfo));
  parser_stats_init(&ctx->stats);

  return ctx;
}

void parser_context_destroy(ParserContext *ctx) {
  if (ctx) {
    pool_destroy(ctx->pool);
    free(ctx);
  }
}

ParserContext *parser_get_context(void) {
#ifdef _WIN32
  if (tls_key == TLS_OUT_OF_INDEXES) {
    tls_init();
  }

  ParserContext *ctx = (ParserContext *)TlsGetValue(tls_key);
  if (!ctx) {
    ctx = parser_context_create(0);
    TlsSetValue(tls_key, ctx);
  }
#else
  pthread_once(&tls_once, tls_init);

  ParserContext *ctx = (ParserContext *)pthread_getspecific(tls_key);
  if (!ctx) {
    ctx = parser_context_create(0);
    pthread_setspecific(tls_key, ctx);
  }
#endif

  return ctx;
}

ParserStats *parser_context_get_stats(ParserContext *ctx) { return ctx ? &ctx->stats : NULL; }

ParseErrorInfo *parser_context_get_error(ParserContext *ctx) {
  return ctx ? &ctx->last_error : NULL;
}
