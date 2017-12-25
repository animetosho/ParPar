/*
 * GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
 * James S. Plank, Ethan L. Miller, Kevin M. Greenan,
 * Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride.
 *
 * gf_w16.h
 *
 * Defines and data structures for 16-bit Galois fields
 */

#ifndef GF_COMPLETE_GF_W16_H
#define GF_COMPLETE_GF_W16_H

#include "../stdint.h"

#define GF_FIELD_WIDTH (16)
#define GF_FIELD_SIZE (1 << GF_FIELD_WIDTH)
#define GF_MULT_GROUP_SIZE GF_FIELD_SIZE-1

#define GF_ANTILOG(x) ltd->antilog_tbl[((x) >> (GF_FIELD_WIDTH)) + ((x) & (GF_MULT_GROUP_SIZE))]

void gf_w16_log_region_alignment(gf_region_data *rd, gf_t *gf, void *src, void *dest, int bytes, uint64_t val, int xor, int align, int walign);

#ifdef INTEL_SSE2
typedef union {
    __m128i p16[2];
#ifdef INTEL_AVX2
    __m256i p32;
#endif
} gf_w16_poly_struct;
#endif

struct gf_w16_logtable_data {
    uint16_t      log_tbl[GF_FIELD_SIZE];
    uint16_t      antilog_tbl[GF_FIELD_SIZE];
    uint8_t  random_padding[64]; /* GF-Complete allocates an additional 64 bytes, but I don't know what it's for... */
    
    /* hijack struct for our polynomial stuff */
#ifdef INTEL_SSE2
    uint8_t _poly[sizeof(gf_w16_poly_struct)*2]; /* storage for *poly; padding allows alignment */
    gf_w16_poly_struct *poly;
#endif
};

#endif /* GF_COMPLETE_GF_W16_H */
