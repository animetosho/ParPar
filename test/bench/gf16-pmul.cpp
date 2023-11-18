#include "gf16pmul.h"
#include "bench.h"
#include "gfmat_coeff.h"

unsigned NUM_TRIALS = 5;
unsigned NUM_ROUNDS = 64;
size_t TEST_SIZE = 8192;
const int REGION_ALIGNMENT = 4096;

struct TestFunc {
	const char* name;
	Gf16PMulFunc fn;
	unsigned blocklen;
};

void gf_exp_test(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	uint16_t* _dst = (uint16_t*)dst;
	const uint16_t* _src1 = (const uint16_t*)src1;
	const uint16_t* _src2 = (const uint16_t*)src2;
	for(unsigned i=0; i<len/2; i++) {
		_dst[i] = gfmat_coeff_from_log(_src1[i], _src2[i]);
	}
}

int main(int, char**) {
	gfmat_init();
	setup_pmul();
	
	std::vector<struct TestFunc> funcs;
	funcs.push_back({
		"GF16Exp", &gf_exp_test, 16
	});
	if(gf16pmul_available_sse)
		funcs.push_back({
			"ClMul (SSE)", &gf16pmul_sse, 16
		});
	if(gf16pmul_available_avx2)
		funcs.push_back({
			"ClMul (AVX2)", &gf16pmul_avx2, 32
		});
	if(gf16pmul_available_vpclmul)
		funcs.push_back({
			"ClMul (VPCLMUL)", &gf16pmul_vpclmul, 32
		});
	if(gf16pmul_available_vpclgfni)
		funcs.push_back({
			"ClMul (VPCLMUL+GFNI)", &gf16pmul_vpclgfni, 64
		});
	if(gf16pmul_available_neon)
		funcs.push_back({
			"ClMul (NEON)", &gf16pmul_neon, 32
		});
	if(gf16pmul_available_sve2)
		funcs.push_back({
			"ClMul (SVE2)", &gf16pmul_sve2, gf16pmul_sve2_width()*2
		});
	if(gf16pmul_available_rvv)
		funcs.push_back({
			"ClMul (RVV)", &gf16pmul_rvv, gf16pmul_rvv_width()
		});
	
	uint16_t* src1, *src2, *dst;
	ALIGN_ALLOC(src1, TEST_SIZE, REGION_ALIGNMENT);
	ALIGN_ALLOC(src2, TEST_SIZE, REGION_ALIGNMENT);
	ALIGN_ALLOC(dst, TEST_SIZE, REGION_ALIGNMENT);
	
	for(size_t i=0; i<TEST_SIZE/sizeof(uint16_t); i++) {
		src1[i] = rand() & 0xffff;
		src2[i] = rand() & 0xffff;
	}
	
	Timer timer;
	
	for(const auto& fn : funcs) {
		size_t regionSize = TEST_SIZE;
		regionSize -= regionSize % fn.blocklen;
		
		double bestTime = DBL_MAX;
		for(unsigned trial=0; trial<NUM_TRIALS; trial++) {
			timer.reset();
			for(unsigned round=0; round<NUM_ROUNDS; round++) {
				fn.fn(dst, src1, src2, regionSize);
			}
			double curTime = timer.elapsed();
			if(curTime < bestTime) bestTime = curTime;
		}
		
		double speed = (double)(regionSize*NUM_ROUNDS) / 1048576 / bestTime;
		printf("%20s: %8.1f\n", fn.name, speed);
	}
	
	ALIGN_FREE(src1);
	ALIGN_FREE(src2);
	ALIGN_FREE(dst);
	
	return 0;
}
