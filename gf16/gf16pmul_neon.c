#include "gf16_global.h"

#ifdef __ARM_NEON
# define _AVAILABLE
#endif
#include "gf16_clmul_neon.h"

#ifdef __ARM_NEON
# undef _AVAILABLE

int gf16pmul_available_neon = 1;

void gf16pmul_neon(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	assert(len % sizeof(uint8x16_t)*2 == 0);
	
	const poly8_t* _src1 = (const poly8_t*)src1 + len;
	const poly8_t* _src2 = (const poly8_t*)src2 + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		poly8x16x2_t data1 = vld2q_p8(_src1+ptr);
		poly8x16x2_t data2 = vld2q_p8(_src2+ptr);
		poly16x8_t low1 = vmull_p8(vget_low_p8(data1.val[0]), vget_low_p8(data2.val[0]));
		poly8x16_t dataMid1 = veorq_p8(data1.val[0], data1.val[1]);
		poly8x16_t dataMid2 = veorq_p8(data2.val[0], data2.val[1]);
		poly16x8_t mid1 = vmull_p8(vget_low_p8(dataMid1), vget_low_p8(dataMid2));
		poly16x8_t high1 = vmull_p8(vget_low_p8(data1.val[1]), vget_low_p8(data2.val[1]));
#ifdef __aarch64__
		poly16x8_t low2 = pmull_high(data1.val[0], data2.val[0]);
		poly16x8_t mid2 = pmull_high(dataMid1, dataMid2);
		poly16x8_t high2 = pmull_high(data1.val[1], data2.val[1]);
#else
		poly16x8_t low2 = vmull_p8(vget_high_p8(data1.val[0]), vget_high_p8(data2.val[0]));
		poly16x8_t mid2 = vmull_p8(vget_high_p8(dataMid1), vget_high_p8(dataMid2));
		poly16x8_t high2 = vmull_p8(vget_high_p8(data1.val[1]), vget_high_p8(data2.val[1]));
#endif
		
		gf16_clmul_neon_reduction(&low1, &low2, mid1, mid2, &high1, &high2);
		uint8x16x2_t out;
		out.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vreinterpretq_u8_p16(low2));
		out.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vreinterpretq_u8_p16(high2));
		vst2q_u8(_dst+ptr, out);
	}
}

#else // defined(__ARM_NEON)
int gf16pmul_available_neon = 0;
void gf16pmul_neon(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	UNUSED(dst); UNUSED(src1); UNUSED(src2); UNUSED(len);
}
#endif
