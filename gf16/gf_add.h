
#include "../src/hedley.h"

#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf_add_multi_generic(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_sse2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_avx2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_avx512(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_neon(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_sve(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_sve2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
void gf_add_multi_rvv(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
#endif


#ifdef PARPAR_INCLUDE_BASIC_OPS
#define FUNCS(f) \
	void gf_add_multi_packed_##f(unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len); \
	void gf_add_multi_packpf_##f(unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut)
#else
#define FUNCS(f) \
	void gf_add_multi_packpf_##f(unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut)
#endif

#ifdef PLATFORM_AMD64
FUNCS(v1i6_sse2);
FUNCS(v2i3_sse2);
FUNCS(v1i6_avx2);
FUNCS(v16i1_avx2);
FUNCS(v1i6_avx512);
FUNCS(v1i12_avx512);
FUNCS(v1i12_avx10);
FUNCS(v2i3_avx512);
FUNCS(v2i4_avx512);
FUNCS(v2i6_avx512);
FUNCS(v2i6_avx10);
FUNCS(v16i6_avx512);
#else
FUNCS(v1i2_sse2);
FUNCS(v1i1_avx2);
FUNCS(v1i1_avx512);
FUNCS(v1i2_avx512);
FUNCS(v2i1_avx512);
#endif
FUNCS(v2i1_sse2);
FUNCS(v16i1_sse2);
FUNCS(v1i2_avx2);
FUNCS(v2i1_avx2);
FUNCS(v2i3_avx2);
FUNCS(shuffle_neon);
FUNCS(clmul_neon);
FUNCS(sve);
FUNCS(v1i6_sve2);
FUNCS(v2i3_sve2);
FUNCS(v2i4_sve2);
FUNCS(v2i8_sve2);
FUNCS(v2i3_rvv);
FUNCS(v1i12_rvv);

FUNCS(generic);
FUNCS(lookup3);
#undef FUNCS
