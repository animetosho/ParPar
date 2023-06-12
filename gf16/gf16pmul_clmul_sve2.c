#include "gf16_global.h"
#include "gf16_clmul_sve2.h"

#ifdef __ARM_FEATURE_SVE2
int gf16pmul_clmul_available_sve2 = 1;

void gf16pmul_clmul_sve2(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	assert(len % svcntb()*2 == 0);
	
	const uint8_t* _src1 = (const uint8_t*)src1 + len;
	const uint8_t* _src2 = (const uint8_t*)src2 + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		svuint8x2_t data1 = svld2_u8(svptrue_b8(), _src1+ptr);
		svuint8x2_t data2 = svld2_u8(svptrue_b8(), _src2+ptr);
		svuint8_t low1 = svpmullb_pair_u8(svget2(data1, 0), svget2(data2, 0));
		svuint8_t low2 = svpmullt_pair_u8(svget2(data1, 0), svget2(data2, 0));
		svuint8_t dataMid1 = NOMASK(sveor_u8, svget2(data1, 0), svget2(data1, 1));
		svuint8_t dataMid2 = NOMASK(sveor_u8, svget2(data2, 0), svget2(data2, 1));
		svuint8_t mid1 = svpmullb_pair_u8(dataMid1, dataMid2);
		svuint8_t mid2 = svpmullt_pair_u8(dataMid1, dataMid2);
		svuint8_t high1 = svpmullb_pair_u8(svget2(data1, 1), svget2(data2, 1));
		svuint8_t high2 = svpmullt_pair_u8(svget2(data1, 1), svget2(data2, 1));
		
		gf16_clmul_sve2_reduction(&low1, low2, mid1, mid2, &high1, high2);
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(low1, high1));
	}
}

unsigned gf16pmul_clmul_sve2_width() {
	return svcntb();
}

#else // defined(__ARM_FEATURE_SVE2)
int gf16pmul_clmul_available_sve2 = 0;
void gf16pmul_clmul_sve2(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	UNUSED(dst); UNUSED(src1); UNUSED(src2); UNUSED(len);
}

unsigned gf16pmul_clmul_sve2_width() {
	return 1;
}
#endif
