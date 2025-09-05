#ifndef ENDIAN_H
#define ENDIAN_H

#include "kernel.h"

static inline uint16_t __bswap16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static inline uint32_t __bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
}

/* Assume little-endian target (x86_64) */
static inline uint16_t htons(uint16_t x) { return __bswap16(x); }
static inline uint16_t ntohs(uint16_t x) { return __bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return __bswap32(x); }

#endif /* ENDIAN_H */
