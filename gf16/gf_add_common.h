
#ifdef PARPAR_INCLUDE_BASIC_OPS
# define PACKED_STUB(suf, vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_##suf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
} \
void gf_add_multi_packpf_v##vs##i##il##_##suf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#else
# define PACKED_STUB(suf, vs, il, it) \
void gf_add_multi_packpf_v##vs##i##il##_##suf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

#ifndef PARPAR_SLIM_GF16
# define PACKED_FUNC_NOTSLIM(suf, vs, il, it) PACKED_FUNC(vs, il, it)
#else
# define PACKED_FUNC_NOTSLIM PACKED_STUB
#endif
