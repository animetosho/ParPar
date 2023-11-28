
# define PACKED_STUB(suf, vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_##suf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
}\
void gf_add_multi_packpf_v##vs##i##il##_##suf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}

#ifndef PARPAR_SLIM_GF16
# define PACKED_FUNC_NOTSLIM(suf, ...) PACKED_FUNC(__VA_ARGS__)
#else
# define PACKED_FUNC_NOTSLIM PACKED_STUB
#endif
