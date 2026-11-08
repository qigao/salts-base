#include "parser_stats.h"
#include "parser_error.h"

void parser_stats_init(ParserStats *stats) {
  if (!stats)
    return;

  stats->frames_parsed = 0;
  stats->frames_failed = 0;
  stats->bytes_processed = 0;
  stats->parse_time_ns = 0;
  stats->crc_errors = 0;
  stats->truncated_frames = 0;
  stats->invalid_frames = 0;
}

void parser_stats_update(ParserStats *stats, int parse_result, size_t bytes, uint64_t elapsed_ns) {
  if (!stats)
    return;

  stats->parse_time_ns += elapsed_ns;

  if (parse_result == PARSE_OK) {
    stats->frames_parsed++;
    stats->bytes_processed += bytes;
  } else {
    stats->frames_failed++;

    switch (parse_result) {
    case PARSE_ERR_CRC_MISMATCH:
      stats->crc_errors++;
      break;
    case PARSE_ERR_TRUNCATED:
      stats->truncated_frames++;
      break;
    case PARSE_ERR_INVALID_HEAD:
    case PARSE_ERR_INVALID_TAIL:
    case PARSE_ERR_INVALID_VERSION:
    case PARSE_ERR_INVALID_PAYLOAD_TYPE:
      stats->invalid_frames++;
      break;
    }
  }
}

void parser_stats_report(ParserStats *stats, FILE *out) {
  if (!stats || !out)
    return;

  fprintf(out, "=== Parser Statistics ===\n");
  fprintf(out, "Frames parsed:    %llu\n", (unsigned long long)stats->frames_parsed);
  fprintf(out, "Frames failed:    %llu\n", (unsigned long long)stats->frames_failed);
  fprintf(out, "Bytes processed:  %llu\n", (unsigned long long)stats->bytes_processed);

  if (stats->frames_parsed > 0) {
    fprintf(out, "Avg parse time:   %llu ns\n",
            (unsigned long long)(stats->parse_time_ns / stats->frames_parsed));
  }

  double throughput = parser_stats_throughput(stats);
  if (throughput > 0) {
    fprintf(out, "Throughput:       %.2f MB/s\n", throughput);
  }

  double error_rate = parser_stats_error_rate(stats);
  fprintf(out, "Error rate:       %.2f%%\n", error_rate);

  fprintf(out, "CRC errors:       %llu\n", (unsigned long long)stats->crc_errors);
  fprintf(out, "Truncated frames: %llu\n", (unsigned long long)stats->truncated_frames);
  fprintf(out, "Invalid frames:   %llu\n", (unsigned long long)stats->invalid_frames);
}

double parser_stats_throughput(ParserStats *stats) {
  if (!stats || stats->parse_time_ns == 0)
    return 0.0;

  double seconds = stats->parse_time_ns / 1e9;
  double megabytes = stats->bytes_processed / 1e6;
  return megabytes / seconds;
}

uint64_t parser_stats_avg_time(ParserStats *stats) {
  if (!stats || stats->frames_parsed == 0)
    return 0;
  return stats->parse_time_ns / stats->frames_parsed;
}

double parser_stats_error_rate(ParserStats *stats) {
  if (!stats)
    return 0.0;

  uint64_t total = stats->frames_parsed + stats->frames_failed;
  if (total == 0)
    return 0.0;

  return 100.0 * stats->frames_failed / total;
}
