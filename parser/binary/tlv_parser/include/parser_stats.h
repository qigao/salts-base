#ifndef PARSER_STATS_H
#define PARSER_STATS_H

#include <stdint.h>
#include <stdio.h>

// Performance statistics
typedef struct {
  uint64_t frames_parsed;
  uint64_t frames_failed;
  uint64_t bytes_processed;
  uint64_t parse_time_ns;
  uint64_t crc_errors;
  uint64_t truncated_frames;
  uint64_t invalid_frames;
} ParserStats;

// Initialize stats
void parser_stats_init(ParserStats *stats);

// Update stats after parsing
void parser_stats_update(ParserStats *stats, int parse_result, size_t bytes, uint64_t elapsed_ns);

// Report statistics
void parser_stats_report(ParserStats *stats, FILE *out);

// Get throughput in MB/s
double parser_stats_throughput(ParserStats *stats);

// Get average parse time in nanoseconds
uint64_t parser_stats_avg_time(ParserStats *stats);

// Get error rate as percentage
double parser_stats_error_rate(ParserStats *stats);

#endif // PARSER_STATS_H
