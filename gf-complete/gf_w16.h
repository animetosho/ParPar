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
/*#ifdef INTEL_SSSE3
    uint8_t _multbl[1024]; // this variable isn't actually used; table is 1024 bytes long, and we rely on overflowing the above (urgh) and space provided by this to enable aligned access
#endif*/
    gf_w16_poly_struct *poly;
#endif
};

#include <assert.h>
static inline void gf_w16_log_region_alignment(gf_region_data *rd,
  gf_t *gf,
  void *src,
  void *dest,
  int bytes,
  uint64_t val,
  int xor,
  int align,
  int walign)
{
	uintptr_t uls = ((uintptr_t) src) & (align-1);
	struct gf_w16_logtable_data *ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
	int log_val;
  
/* never used, so don't bother setting them
	rd->gf = gf;
	rd->src = src;
	rd->dest = dest;
	rd->bytes = bytes;
	rd->val = val;
	rd->xor = xor;
*/

	if (uls != (((uintptr_t) dest) & (align-1)))
		assert(0);
	if ((bytes & 1) != 0)
		assert(0);
	
	/* slow multiply for init/end area */
	#define MUL_LOOP(op, src, dest, srcto) { \
		uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
		while (s16 < (uint16_t *)(srcto)) { \
			*d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
			s16++; \
			d16++; \
		} \
	}
	
	if(uls != 0) {
		int proc_bytes = bytes;
		uls = (align-uls);
		rd->s_start = (uint8_t *)src + uls;
		rd->d_start = (uint8_t *)dest + uls;
		proc_bytes -= uls;
		proc_bytes -= (proc_bytes & (walign-1));
		rd->s_top = (uint8_t *)rd->s_start + proc_bytes;
		rd->d_top = (uint8_t *)rd->d_start + proc_bytes;
		
		log_val = ltd->log_tbl[val];
		if (xor) {
			MUL_LOOP(^=, src, dest, rd->s_start)
			MUL_LOOP(^=, rd->s_top, rd->d_top, ((uint16_t*)src) + (bytes>>1))
		} else {
			MUL_LOOP(=, src, dest, rd->s_start)
			MUL_LOOP(=, rd->s_top, rd->d_top, ((uint16_t*)src) + (bytes>>1))
		}
	} else {
		unsigned int ule = bytes & (walign-1);
		rd->s_start = (uint8_t *)src;
		rd->d_start = (uint8_t *)dest;
		if(ule != 0) {
			int proc_bytes = bytes - ule;
			rd->s_top = (uint8_t *)rd->s_start + proc_bytes;
			rd->d_top = (uint8_t *)rd->d_start + proc_bytes;
			
			log_val = ltd->log_tbl[val];
			if (xor) {
				MUL_LOOP(^=, rd->s_top, rd->d_top, ((uint16_t*)src) + (bytes>>1))
			} else {
				MUL_LOOP(=, rd->s_top, rd->d_top, ((uint16_t*)src) + (bytes>>1))
			}
		} else {
			rd->s_top = (uint8_t *)rd->s_start + bytes;
			rd->d_top = (uint8_t *)rd->d_start + bytes;
		}
	}
	
	#undef MUL_LOOP
}


#endif /* GF_COMPLETE_GF_W16_H */
