#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>


void crc32_generate_table(uint32_t table[256]);
uint32_t crc32_compute(const uint32_t table[256], const void *buf, size_t len);

#endif
