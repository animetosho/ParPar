#include "gfmat_coeff.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <cstring>

#ifdef _MSC_VER
# define ALIGN_TO(a, v) __declspec(align(a)) v
#else
# define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif

#include <stdlib.h>
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	// MSVC doesn't support C11 aligned_alloc: https://stackoverflow.com/a/62963007
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#elif defined(_ISOC11_SOURCE)
	// C11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = aligned_alloc(align, ((len) + (align)-1) & ~((align)-1))
	#define ALIGN_FREE free
#elif defined(__cplusplus) && __cplusplus >= 201700
	// C++17 method
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = std::aligned_alloc(align, ((len) + (align)-1) & ~((align)-1))
	#define ALIGN_FREE free
#else
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif

#ifdef _MSC_VER
# ifndef __BYTE_ORDER__
#  define __BYTE_ORDER__ 1234
# endif
# ifndef __ORDER_BIG_ENDIAN__
#  define __ORDER_BIG_ENDIAN__ 4321
# endif
#endif


const uint8_t guard_magic[] = { 0xdb, 0xef, 0x55, 0xf4 };

static inline size_t roundup_to(size_t n, size_t rounding) {
	return ((n + rounding-1) / rounding) * rounding;
}
static inline size_t rounddown_to(size_t n, size_t rounding) {
	return (n / rounding) * rounding;
}

static uint16_t gf16_log[65536];
static uint16_t gf16_antilog[65536];
static void gf16_generate_log_tables(int polynomial = 0x1100b) {
	int n = 1;
	memset(gf16_log, 0, sizeof(gf16_log));
	for(int i=0; i<65535; i++) {
		gf16_log[n] = i;
		gf16_antilog[i] = n;
		n <<= 1;
		if(n > 0xffff) n ^= polynomial;
	}
	gf16_antilog[65535] = gf16_antilog[0];
}
static inline uint16_t gf16_mul(uint16_t a, uint16_t b) {
	if(a == 0 || b == 0) return 0;
	int log_prod = (int)gf16_log[a] + (int)gf16_log[b];
	return gf16_antilog[(log_prod >> 16) + (log_prod & 0xffff)];
}

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint16_t gf16_mul_le(uint16_t src, uint16_t coeff) {
	uint16_t r = gf16_mul((src>>8) | ((src&0xff)<<8), coeff);
	return (r >> 8) | ((r & 0xff) << 8);
}
#else
# define gf16_mul_le gf16_mul
#endif

static int find_mem_diff(const uint16_t* a, const uint16_t* b, int n) {
	for(int i=0; i<n; i++) {
		if(a[i] != b[i]) {
			return i;
		}
	}
	return -1;
}
static void print_mem_region(const uint16_t* mem, int from, int to) {
	for(int i=from; i<to; i+=8) {
		printf(" %04X | ", i*2);
		for(int j=0; j< (i+8<to ? 8 : to-i); j++) {
			printf("%04X ", mem[i+j]);
		}
		printf("\n");
	}
}
static int display_mem_diff(const uint16_t* a, const uint16_t* b, int n) {
	// find first diff
	int idx = find_mem_diff(a, b, n);
	if(idx < 0) return -1;
	
	// display region
	int from = (idx & 0xfffffff8) - 8;
	int to = from + 24;
	if(from < 0) from = 0;
	if(to > n) to = n;
	
	printf("Expected:\n");
	print_mem_region(a, from, to);
	printf("Actual:\n");
	print_mem_region(b, from, to);
	return from;
}
