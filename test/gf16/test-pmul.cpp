
#include "gf16pmul.h"
#include "test.h"

const int MAX_TEST_REGIONS = 20;
// earlier GCC doesn't like `const int` used for alignment statements, so use a define instead
#define REGION_ALIGNMENT 4096
const int REGION_SIZE = MAX_TEST_REGIONS * 1024; // largest stride = 1024 bytes from Xor512

struct TestFunc {
	Galois16PointMulMethods id;
	Gf16PMulFunc fn;
	unsigned blocklen;
};
static void show_help() {
	std::cout << "test-pmul [-v]" << std::endl;
	exit(0);
}

int main(int argc, char** argv) {
	bool verbose = false;
	int seeds[] = {0x01020304, 0x50607080 };
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'v':
				verbose = true;
			break;
			default: show_help();
		}
	}
	(void)verbose;
	
	// allocate src/tmp regions
	uint16_t* src1, *src2, *dst, *ref;
	ALIGN_ALLOC(src1, REGION_SIZE, REGION_ALIGNMENT);
	ALIGN_ALLOC(src2, REGION_SIZE, REGION_ALIGNMENT);
	ALIGN_ALLOC(dst, REGION_SIZE, REGION_ALIGNMENT);
	ALIGN_ALLOC(ref, REGION_SIZE, REGION_ALIGNMENT);
	if(!src1 || !src2 || !dst || !ref) {
		std::cout << "Failed to allocate memory" << std::endl;
		return 2;
	}
	
	gf16_generate_log_tables();
	setup_pmul();
	
	std::vector<struct TestFunc> funcs;
	if(gf16pmul_available_sse)
		funcs.push_back({
			GF16PMUL_PCLMUL, &gf16pmul_sse, 16
		});
	if(gf16pmul_available_avx2)
		funcs.push_back({
			GF16PMUL_AVX2, &gf16pmul_avx2, 32
		});
	if(gf16pmul_available_vpclmul)
		funcs.push_back({
			GF16PMUL_VPCLMUL, &gf16pmul_vpclmul, 32
		});
	if(gf16pmul_available_vpclgfni)
		funcs.push_back({
			GF16PMUL_VPCLMUL_GFNI, &gf16pmul_vpclgfni, 64
		});
	if(gf16pmul_available_neon)
		funcs.push_back({
			GF16PMUL_NEON, &gf16pmul_neon, 32
		});
	if(gf16pmul_available_sve2)
		funcs.push_back({
			GF16PMUL_SVE2, &gf16pmul_sve2, gf16pmul_sve2_width()*2
		});
	
	for(int seed : seeds) {
		// generate source regions + ref
		srand(seed);
		for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++) {
			src1[i] = rand() & 0xffff;
			src2[i] = rand() & 0xffff;
			uint16_t coeff = src2[i];
			#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			coeff = (coeff>>8) | ((coeff&0xff))<<8;
			#endif
			ref[i] = gf16_mul_le(src1[i], coeff);
		}
		
		for(const auto& fn : funcs) {
			auto name = gf16pmul_methodName(fn.id);
			/*if(verbose)*/ std::cout << "  " << name << std::endl;
			memset(dst, 0, REGION_SIZE);
			size_t regionSize = REGION_SIZE;
			regionSize -= regionSize % fn.blocklen;
			fn.fn(dst, src1, src2, regionSize);
			if(memcmp(dst, ref, regionSize)) {
				std::cout << "PointMul failure: " << name << std::endl;
				int from = display_mem_diff(ref, dst, regionSize/2);
				int to = (std::min)(from+16, (int)regionSize/2);
				std::cout << "\nSrc1:\n";
				print_mem_region(src1, from, to);
				std::cout << "Src2:\n";
				print_mem_region(src2, from, to);
				return 1;
			}
		}
	}
	
	
	ALIGN_FREE(src1);
	ALIGN_FREE(src2);
	ALIGN_FREE(dst);
	ALIGN_FREE(ref);
	std::cout << "All tests passed" << std::endl;
	return 0;
}
