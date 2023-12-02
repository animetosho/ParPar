
#define GF16_MULADD_MULTI_SRCLIST const int srcCount, \
	const uint8_t* _src1, const uint8_t* _src2, const uint8_t* _src3, const uint8_t* _src4, const uint8_t* _src5, const uint8_t* _src6, \
	const uint8_t* _src7, const uint8_t* _src8, const uint8_t* _src9, const uint8_t* _src10, const uint8_t* _src11, const uint8_t* _src12, \
	const uint8_t* _src13, const uint8_t* _src14, const uint8_t* _src15, const uint8_t* _src16, const uint8_t* _src17, const uint8_t* _src18
#define GF16_MULADD_MULTI_SRC_UNUSED(max) \
	HEDLEY_ASSUME(srcCount <= max); \
	if(max < 2) UNUSED(_src2); \
	if(max < 3) UNUSED(_src3); \
	if(max < 4) UNUSED(_src4); \
	if(max < 5) UNUSED(_src5); \
	if(max < 6) UNUSED(_src6); \
	if(max < 7) UNUSED(_src7); \
	if(max < 8) UNUSED(_src8); \
	if(max < 9) UNUSED(_src9); \
	if(max < 10) UNUSED(_src10); \
	if(max < 11) UNUSED(_src11); \
	if(max < 12) UNUSED(_src12); \
	if(max < 13) UNUSED(_src13); \
	if(max < 14) UNUSED(_src14); \
	if(max < 15) UNUSED(_src15); \
	if(max < 16) UNUSED(_src16); \
	if(max < 17) UNUSED(_src17); \
	if(max < 18) UNUSED(_src18)

#ifdef PARPAR_INVERT_SUPPORT
# define GF16_MULADD_MULTI_FUNCS(fnpre, fnsuf, xfn, procRegions, blocksize, pfFactor, finisher) \
void TOKENPASTE3(fnpre, _muladd_multi, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi(scratch, &xfn, procRegions, regions, offset, dst, src, len, coefficients); \
	finisher; \
} \
void TOKENPASTE3(fnpre, _muladd_multi_stridepf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetch) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi_stridepf(scratch, &xfn, procRegions, regions, srcStride, dst, src, len, coefficients, pfFactor, prefetch); \
	finisher; \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packed, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi_packed(scratch, &xfn, procRegions, procRegions, packedRegions, regions, dst, src, len, blocksize, coefficients); \
	finisher; \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packpf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi_packpf(scratch, &xfn, procRegions, procRegions, packedRegions, regions, dst, src, len, blocksize, coefficients, pfFactor, prefetchIn, prefetchOut); \
	finisher; \
}
# define GF16_MULADD_MULTI_FUNCS_STUB(fnpre, fnsuf) \
void TOKENPASTE3(fnpre, _muladd_multi, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); \
} \
void TOKENPASTE3(fnpre, _muladd_multi_stridepf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetch) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(regions); UNUSED(srcStride); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetch); \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packed, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packpf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#else
# define GF16_MULADD_MULTI_FUNCS(fnpre, fnsuf, xfn, procRegions, blocksize, pfFactor, finisher) \
void TOKENPASTE3(fnpre, _muladd_multi_packed, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi_packed(scratch, &xfn, procRegions, procRegions, packedRegions, regions, dst, src, len, blocksize, coefficients); \
	finisher; \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packpf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(mutScratch); \
	gf16_muladd_multi_packpf(scratch, &xfn, procRegions, procRegions, packedRegions, regions, dst, src, len, blocksize, coefficients, pfFactor, prefetchIn, prefetchOut); \
	finisher; \
}
# define GF16_MULADD_MULTI_FUNCS_STUB(fnpre, fnsuf) \
void TOKENPASTE3(fnpre, _muladd_multi_packed, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); \
} \
void TOKENPASTE3(fnpre, _muladd_multi_packpf, fnsuf)(const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(mutScratch); \
	UNUSED(scratch); UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif




#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
// GCC, for some reason, doesn't like const pointers when forced to inline without optimizations
typedef void (*fMuladdPF)
#else
typedef void (*const fMuladdPF)
#endif
(const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
);

// suppress UBSan warning about adding to a NULL pointer; `coefficients` can be NULL from gf_add*, but it's never used there, and it's annoying to have to check and branch on these
#if defined(__clang__) && HEDLEY_HAS_ATTRIBUTE(no_sanitize)
# define IGNORE_NULL_ADD __attribute__((no_sanitize("pointer-overflow")))
#else
# define IGNORE_NULL_ADD
#endif

static HEDLEY_ALWAYS_INLINE void gf16_muladd_single(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, uint16_t val) {
	muladd_pf(
		scratch, (uint8_t*)dst + len, 1, 1,
		(const uint8_t*)src + len,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL,
		len, &val, 0, NULL
	);
}

static HEDLEY_ALWAYS_INLINE void gf16_muladd_prefetch_single(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, uint16_t val, const void *HEDLEY_RESTRICT prefetch) {
	muladd_pf(
		scratch, (uint8_t*)dst + len, 1, 1,
		(const uint8_t*)src + len,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL,
		len, &val, 2, prefetch
	);
}

#define REMAINING_CASES CASE(17); CASE(16); CASE(15); CASE(14); CASE(13); CASE(12); CASE(11); CASE(10); CASE( 9); CASE( 8); CASE( 7); CASE( 6); CASE( 5); CASE( 4); CASE( 3); CASE( 2); CASE( 1)

#ifdef PARPAR_INVERT_SUPPORT
static HEDLEY_ALWAYS_INLINE void gf16_muladd_multi(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients) IGNORE_NULL_ADD {
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	#define _SRC(limit, n) limit > n ? (const uint8_t*)src[region+n] + offset + len : NULL
	unsigned region;
	for(region = 0; region + interleave <= regions; region += interleave) {
		muladd_pf(
			scratch, _dst, 1, interleave,
			(const uint8_t*)src[region] + offset + len,
			_SRC(interleave, 1), _SRC(interleave,  2), _SRC(interleave,  3), _SRC(interleave,  4),
			_SRC(interleave, 5), _SRC(interleave,  6), _SRC(interleave,  7), _SRC(interleave,  8),
			_SRC(interleave, 9), _SRC(interleave, 10), _SRC(interleave, 11), _SRC(interleave, 12),
			_SRC(interleave,13), _SRC(interleave, 14), _SRC(interleave, 15), _SRC(interleave, 16),
			_SRC(interleave,17),
			len, coefficients + region, 0, NULL
		);
	}
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < interleave); // doesn't seem to always work, so we have additional checks in the switch cases
	switch(remaining) {
		#define CASE(x) \
			case x: \
				HEDLEY_ASSUME(x < interleave); \
				muladd_pf( \
					scratch, _dst, 1, x, \
					(const uint8_t*)src[region] + offset + len, \
					_SRC(x, 1), _SRC(x,  2), _SRC(x,  3), _SRC(x,  4), \
					_SRC(x, 5), _SRC(x,  6), _SRC(x,  7), _SRC(x,  8), \
					_SRC(x, 9), _SRC(x, 10), _SRC(x, 11), _SRC(x, 12), \
					_SRC(x,13), _SRC(x, 14), _SRC(x, 15), _SRC(x, 16), \
					_SRC(x,17), \
					len, coefficients + region, 0, NULL \
				); \
			break
			REMAINING_CASES;
		#undef CASE
		default: break;
	}
	#undef _SRC
}

static HEDLEY_ALWAYS_INLINE void gf16_muladd_multi_stridepf(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const unsigned pfFactor, const void* HEDLEY_RESTRICT prefetch) IGNORE_NULL_ADD {
	uint8_t* _dst = (uint8_t*)dst + len;
	uint8_t* srcEnd = (uint8_t*)src + len;
	
	size_t pfLen = len>>pfFactor;
	const char* _pf = (const char*)prefetch + pfLen;
	unsigned outputPfRounds = 1<<pfFactor;
	
	#define _SRC(limit, n) limit > n ? srcEnd + srcStride*n : NULL
	unsigned region;
	for(region = 0; region + interleave <= regions && outputPfRounds; region += interleave) {
		muladd_pf(
			scratch, _dst, 1, interleave,
			srcEnd,
			_SRC(interleave, 1), _SRC(interleave,  2), _SRC(interleave,  3), _SRC(interleave,  4),
			_SRC(interleave, 5), _SRC(interleave,  6), _SRC(interleave,  7), _SRC(interleave,  8),
			_SRC(interleave, 9), _SRC(interleave, 10), _SRC(interleave, 11), _SRC(interleave, 12),
			_SRC(interleave,13), _SRC(interleave, 14), _SRC(interleave, 15), _SRC(interleave, 16),
			_SRC(interleave,17),
			len, coefficients + region, 2, _pf
		);
		srcEnd += srcStride*interleave;
		outputPfRounds--;
		_pf += pfLen;
	}
	for(; region + interleave <= regions; region += interleave) {
		muladd_pf(
			scratch, _dst, 1, interleave,
			srcEnd,
			_SRC(interleave, 1), _SRC(interleave,  2), _SRC(interleave,  3), _SRC(interleave,  4),
			_SRC(interleave, 5), _SRC(interleave,  6), _SRC(interleave,  7), _SRC(interleave,  8),
			_SRC(interleave, 9), _SRC(interleave, 10), _SRC(interleave, 11), _SRC(interleave, 12),
			_SRC(interleave,13), _SRC(interleave, 14), _SRC(interleave, 15), _SRC(interleave, 16),
			_SRC(interleave,17),
			len, coefficients + region, 0, NULL
		);
		srcEnd += srcStride*interleave;
	}
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < interleave); // doesn't seem to always work, so we have additional checks in the switch cases
	if(outputPfRounds) {
		switch(remaining) {
			#define CASE(x) \
				case x: \
					HEDLEY_ASSUME(x < interleave); \
					muladd_pf( \
						scratch, _dst, 1, x, \
						srcEnd, \
						_SRC(x, 1), _SRC(x,  2), _SRC(x,  3), _SRC(x,  4), \
						_SRC(x, 5), _SRC(x,  6), _SRC(x,  7), _SRC(x,  8), \
						_SRC(x, 9), _SRC(x, 10), _SRC(x, 11), _SRC(x, 12), \
						_SRC(x,13), _SRC(x, 14), _SRC(x, 15), _SRC(x, 16), \
						_SRC(x,17), \
						len, coefficients + region, 2, _pf \
					); \
				break
				REMAINING_CASES;
			#undef CASE
			default: break;
		}
	} else {
		switch(remaining) {
			#define CASE(x) \
				case x: \
					HEDLEY_ASSUME(x < interleave); \
					muladd_pf( \
						scratch, _dst, 1, x, \
						srcEnd, \
						_SRC(x, 1), _SRC(x,  2), _SRC(x,  3), _SRC(x,  4), \
						_SRC(x, 5), _SRC(x,  6), _SRC(x,  7), _SRC(x,  8), \
						_SRC(x, 9), _SRC(x, 10), _SRC(x, 11), _SRC(x, 12), \
						_SRC(x,13), _SRC(x, 14), _SRC(x, 15), _SRC(x, 16), \
						_SRC(x,17), \
						len, coefficients + region, 0, NULL \
					); \
				break
				REMAINING_CASES;
			#undef CASE
			default: break;
		}
	}
	#undef _SRC
}
#endif

static HEDLEY_ALWAYS_INLINE void gf16_muladd_multi_packed(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regionsPerCall, unsigned inputPackSize, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, size_t blockLen, const uint16_t *HEDLEY_RESTRICT coefficients) IGNORE_NULL_ADD {
	ASSUME(regions <= inputPackSize);
	
	uint8_t* _dst = (uint8_t*)dst + len;
	const uint8_t* _src = (const uint8_t*)src;
	const uint8_t* srcEnd;
	
	ASSUME(regionsPerCall % interleave == 0);
	
	unsigned region = 0;
	if(regions >= regionsPerCall) do {
		srcEnd = _src + region * len + len*interleave;
		muladd_pf(
			scratch, _dst, interleave, regionsPerCall,
			srcEnd,
			srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave),
			srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave),
			srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave),
			srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave),
			srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave),
			srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave),
			srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave),
			srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave),
			srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave),
			srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave),
			srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave),
			srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave),
			srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave),
			srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave),
			srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave),
			srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave),
			srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave),
			len, coefficients + region, 0, NULL
		);
		region += regionsPerCall;
	} while(regionsPerCall <= regions - region);
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < regionsPerCall); // doesn't seem to always work, so we have additional checks in the switch cases
	
	if(regionsPerCall > interleave && remaining >= interleave) { // latter condition implies the former, but leavel in former for optimisation
		srcEnd = _src + region * len + len*interleave;
		regionsPerCall = remaining - (remaining % interleave);
		switch(regionsPerCall) {
			#define CASE(x) \
				case x: \
					muladd_pf( \
						scratch, _dst, interleave, x, \
						srcEnd, \
						srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave), \
						srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave), \
						srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave), \
						srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave), \
						srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave), \
						srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave), \
						srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave), \
						srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave), \
						srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave), \
						srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave), \
						srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave), \
						srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave), \
						srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave), \
						srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave), \
						srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave), \
						srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave), \
						srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave), \
						len, coefficients + region, 0, NULL \
					); \
					region += x; \
				break
				REMAINING_CASES;
			#undef CASE
			default: HEDLEY_UNREACHABLE();
		}
		remaining %= interleave;
	}
	HEDLEY_ASSUME(remaining < interleave);
	
	unsigned lastInterleave = inputPackSize - region > interleave ? interleave : inputPackSize - region;
	switch(remaining) {
		#define CASE(x) \
			case x: \
				HEDLEY_ASSUME(x < interleave); \
				HEDLEY_ASSUME(x <= lastInterleave); \
				srcEnd = _src + region * len + len*lastInterleave; \
				muladd_pf( \
					scratch, _dst, lastInterleave, x, \
					srcEnd, \
					srcEnd + blockLen* 1, \
					srcEnd + blockLen* 2, \
					srcEnd + blockLen* 3, \
					srcEnd + blockLen* 4, \
					srcEnd + blockLen* 5, \
					srcEnd + blockLen* 6, \
					srcEnd + blockLen* 7, \
					srcEnd + blockLen* 8, \
					srcEnd + blockLen* 9, \
					srcEnd + blockLen*10, \
					srcEnd + blockLen*11, \
					srcEnd + blockLen*12, \
					srcEnd + blockLen*13, \
					srcEnd + blockLen*14, \
					srcEnd + blockLen*15, \
					srcEnd + blockLen*16, \
					srcEnd + blockLen*17, \
					len, coefficients + region, 0, NULL \
				); \
			break
			REMAINING_CASES;
		#undef CASE
		default: break;
	}
}

#if defined(__ICC) || (defined(_MSC_VER) && !defined(__clang__)) || !defined(_MM_HINT_ET1)
# define MM_HINT_WT1 _MM_HINT_T1
#else
# define MM_HINT_WT1 _MM_HINT_ET1
#endif

static HEDLEY_ALWAYS_INLINE void gf16_muladd_multi_packpf(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regionsPerCall, unsigned inputPackSize, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, size_t blockLen, const uint16_t *HEDLEY_RESTRICT coefficients, const unsigned pfFactor, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) IGNORE_NULL_ADD {
	ASSUME(regions <= inputPackSize);
	
	uint8_t* _dst = (uint8_t*)dst + len;
	const uint8_t* _src = (const uint8_t*)src;
	const uint8_t* srcEnd;
	
	unsigned region = 0;
	size_t pfLen = len>>pfFactor;
	const char* _pf = (const char*)prefetchOut + pfLen;
	unsigned outputPfRounds = 1<<pfFactor;
	if(regions >= regionsPerCall) {
		while(outputPfRounds--) {
			srcEnd = _src + region * len + len*interleave;
			muladd_pf(
				scratch, _dst, interleave, regionsPerCall,
				srcEnd,
				srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave),
				srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave),
				srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave),
				srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave),
				srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave),
				srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave),
				srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave),
				srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave),
				srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave),
				srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave),
				srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave),
				srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave),
				srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave),
				srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave),
				srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave),
				srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave),
				srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave),
				len, coefficients + region, 1, _pf
			);
			region += regionsPerCall;
			if(outputPfRounds)
				_pf += pfLen;
			else
				_pf = NULL;
			if(regionsPerCall > regions - region) break;
		}
	}
	if(_pf && regionsPerCall > regions - region) {
		unsigned remaining = regions - region;
		
		if(regionsPerCall > interleave && remaining >= interleave) {
			srcEnd = _src + region * len + len*interleave;
			switch(remaining - (remaining % interleave)) {
				#define CASE(x) \
					case x: \
						muladd_pf( \
							scratch, _dst, interleave, x, \
							srcEnd, \
							srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave), \
							srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave), \
							srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave), \
							srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave), \
							srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave), \
							srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave), \
							srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave), \
							srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave), \
							srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave), \
							srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave), \
							srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave), \
							srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave), \
							srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave), \
							srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave), \
							srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave), \
							srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave), \
							srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave), \
							len, coefficients + region, 1, _pf \
						); \
						region += x; \
					break
					REMAINING_CASES;
				#undef CASE
				default: HEDLEY_UNREACHABLE();
			}
			remaining %= interleave;
			if(--outputPfRounds)
				_pf += pfLen;
			else
				_pf = NULL;
		}
		
		if(_pf) {
			unsigned lastInterleave = inputPackSize - region > interleave ? interleave : inputPackSize - region;
			switch(remaining) {
				#define CASE(x) \
					case x: \
						HEDLEY_ASSUME(x <= interleave); \
						HEDLEY_ASSUME(x <= lastInterleave); \
						srcEnd = _src + region * len + len*lastInterleave; \
						muladd_pf( \
							scratch, _dst, lastInterleave, x, \
							srcEnd, \
							srcEnd + blockLen* 1, \
							srcEnd + blockLen* 2, \
							srcEnd + blockLen* 3, \
							srcEnd + blockLen* 4, \
							srcEnd + blockLen* 5, \
							srcEnd + blockLen* 6, \
							srcEnd + blockLen* 7, \
							srcEnd + blockLen* 8, \
							srcEnd + blockLen* 9, \
							srcEnd + blockLen*10, \
							srcEnd + blockLen*11, \
							srcEnd + blockLen*12, \
							srcEnd + blockLen*13, \
							srcEnd + blockLen*14, \
							srcEnd + blockLen*15, \
							srcEnd + blockLen*16, \
							srcEnd + blockLen*17, \
							len, coefficients + region, 1, _pf \
						); \
						region += x; \
					break
					REMAINING_CASES;
				#undef CASE
				default: break;
			}
			return;
		}
		// TODO: consider handling prefetchIn?
	}
	
	if(prefetchIn) {
		_pf = (const char*)prefetchIn + pfLen;
		while(regionsPerCall <= regions - region) {
			srcEnd = _src + region * len + len*interleave;
			muladd_pf(
				scratch, _dst, interleave, regionsPerCall,
				srcEnd,
				srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave),
				srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave),
				srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave),
				srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave),
				srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave),
				srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave),
				srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave),
				srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave),
				srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave),
				srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave),
				srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave),
				srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave),
				srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave),
				srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave),
				srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave),
				srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave),
				srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave),
				len, coefficients + region, 2, _pf
			);
			region += regionsPerCall;
			_pf += pfLen;
		}
	}
	else while(regionsPerCall <= regions - region) {
		srcEnd = _src + region * len + len*interleave;
		muladd_pf(
			scratch, _dst, interleave, regionsPerCall,
			srcEnd,
			srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave),
			srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave),
			srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave),
			srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave),
			srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave),
			srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave),
			srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave),
			srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave),
			srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave),
			srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave),
			srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave),
			srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave),
			srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave),
			srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave),
			srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave),
			srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave),
			srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave),
			len, coefficients + region, 0, NULL
		);
		region += regionsPerCall;
	}
	
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < regionsPerCall);
	
	if(regionsPerCall > interleave && remaining >= interleave) {
		srcEnd = _src + region * len + len*interleave;
		regionsPerCall = remaining - (remaining % interleave);
		switch(regionsPerCall) {
			#define CASE(x) \
				case x: \
					muladd_pf( \
						scratch, _dst, interleave, x, \
						srcEnd, \
						srcEnd + len*interleave*( 1/interleave) + blockLen*( 1%interleave), \
						srcEnd + len*interleave*( 2/interleave) + blockLen*( 2%interleave), \
						srcEnd + len*interleave*( 3/interleave) + blockLen*( 3%interleave), \
						srcEnd + len*interleave*( 4/interleave) + blockLen*( 4%interleave), \
						srcEnd + len*interleave*( 5/interleave) + blockLen*( 5%interleave), \
						srcEnd + len*interleave*( 6/interleave) + blockLen*( 6%interleave), \
						srcEnd + len*interleave*( 7/interleave) + blockLen*( 7%interleave), \
						srcEnd + len*interleave*( 8/interleave) + blockLen*( 8%interleave), \
						srcEnd + len*interleave*( 9/interleave) + blockLen*( 9%interleave), \
						srcEnd + len*interleave*(10/interleave) + blockLen*(10%interleave), \
						srcEnd + len*interleave*(11/interleave) + blockLen*(11%interleave), \
						srcEnd + len*interleave*(12/interleave) + blockLen*(12%interleave), \
						srcEnd + len*interleave*(13/interleave) + blockLen*(13%interleave), \
						srcEnd + len*interleave*(14/interleave) + blockLen*(14%interleave), \
						srcEnd + len*interleave*(15/interleave) + blockLen*(15%interleave), \
						srcEnd + len*interleave*(16/interleave) + blockLen*(16%interleave), \
						srcEnd + len*interleave*(17/interleave) + blockLen*(17%interleave), \
						len, coefficients + region, 0, NULL \
					); \
					region += x; \
				break
				REMAINING_CASES;
			#undef CASE
			default: HEDLEY_UNREACHABLE();
		}
		remaining %= interleave;
	}
	HEDLEY_ASSUME(remaining < interleave);
	
	unsigned lastInterleave = inputPackSize - region > interleave ? interleave : inputPackSize - region;
	switch(remaining) {
		#define CASE(x) \
			case x: \
				HEDLEY_ASSUME(x <= interleave); \
				HEDLEY_ASSUME(x <= lastInterleave); \
				srcEnd = _src + region * len + len*lastInterleave; \
				muladd_pf( \
					scratch, _dst, lastInterleave, x, \
					srcEnd, \
					srcEnd + blockLen* 1, \
					srcEnd + blockLen* 2, \
					srcEnd + blockLen* 3, \
					srcEnd + blockLen* 4, \
					srcEnd + blockLen* 5, \
					srcEnd + blockLen* 6, \
					srcEnd + blockLen* 7, \
					srcEnd + blockLen* 8, \
					srcEnd + blockLen* 9, \
					srcEnd + blockLen*10, \
					srcEnd + blockLen*11, \
					srcEnd + blockLen*12, \
					srcEnd + blockLen*13, \
					srcEnd + blockLen*14, \
					srcEnd + blockLen*15, \
					srcEnd + blockLen*16, \
					srcEnd + blockLen*17, \
					len, coefficients + region, 0, NULL \
				); \
				region += x; \
			break
			REMAINING_CASES;
		#undef CASE
		default: break;
	}
}

#undef REMAINING_CASES
#undef IGNORE_NULL_ADD
