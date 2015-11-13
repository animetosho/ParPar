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

#define GF_ANTILOG(x) ltd->antilog_tbl[((x) >> GF_FIELD_WIDTH) + ((x) & GF_MULT_GROUP_SIZE)]


struct gf_w16_logtable_data {
    uint16_t      log_tbl[GF_FIELD_SIZE];
    uint16_t      antilog_tbl[GF_FIELD_SIZE];
};

#endif /* GF_COMPLETE_GF_W16_H */
