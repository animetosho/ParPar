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

#if defined(_MSC_VER) && _MSC_VER < 1900
#include "msc_stdint.h"
#else
#include <stdint.h>
#endif

#define GF_FIELD_WIDTH (16)
#define GF_FIELD_SIZE (1 << GF_FIELD_WIDTH)
#define GF_MULT_GROUP_SIZE GF_FIELD_SIZE-1

struct gf_w16_logtable_data {
    uint16_t      log_tbl[GF_FIELD_SIZE];
    uint16_t      antilog_tbl[GF_FIELD_SIZE * 2];
    uint16_t      inv_tbl[GF_FIELD_SIZE];
    uint16_t      *d_antilog;
};

struct gf_w16_split_8_8_data {
    uint16_t      tables[3][256][256];
};

void gf_w16_neon_split_init(gf_t *gf);

#endif /* GF_COMPLETE_GF_W16_H */
