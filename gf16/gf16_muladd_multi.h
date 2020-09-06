
#define GF16_MULADD_MULTI_SRCLIST const int srcCount, \
	const uint8_t* _src1, const uint8_t* _src2, const uint8_t* _src3, const uint8_t* _src4, const uint8_t* _src5, const uint8_t* _src6, \
	const uint8_t* _src7, const uint8_t* _src8, const uint8_t* _src9, const uint8_t* _src10, const uint8_t* _src11, const uint8_t* _src12, const uint8_t* _src13
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
	if(max < 13) UNUSED(_src13)

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


static HEDLEY_ALWAYS_INLINE void gf16_muladd_single(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, uint16_t val) {
	muladd_pf(
		scratch, (uint8_t*)dst + len, 1, 1,
		(const uint8_t*)src + len,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
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
		len, &val, 2, prefetch
	);
}


static HEDLEY_ALWAYS_INLINE unsigned gf16_muladd_multi(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients) {
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	#define _SRC(limit, n) limit > n ? (const uint8_t*)src[region+n] + offset + len : NULL
	unsigned region = 0;
	if(regions >= interleave) do {
		muladd_pf(
			scratch, _dst, 1, interleave,
			(const uint8_t*)src[region] + offset + len,
			_SRC(interleave, 1), _SRC(interleave,  2), _SRC(interleave,  3), _SRC(interleave,  4),
			_SRC(interleave, 5), _SRC(interleave,  6), _SRC(interleave,  7), _SRC(interleave,  8),
			_SRC(interleave, 9), _SRC(interleave, 10), _SRC(interleave, 11), _SRC(interleave, 12),
			len, coefficients + region, 0, NULL
		);
		region += interleave;
	} while(interleave <= regions - region);
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < interleave); // doesn't seem to always work, so we have additional checks in the switch cases
	switch(remaining) {
		#define CASE(x) \
			case x: \
				if(x >= interleave) HEDLEY_UNREACHABLE(); \
				muladd_pf( \
					scratch, _dst, 1, x, \
					(const uint8_t*)src[region] + offset + len, \
					_SRC(x, 1), _SRC(x,  2), _SRC(x,  3), _SRC(x,  4), \
					_SRC(x, 5), _SRC(x,  6), _SRC(x,  7), _SRC(x,  8), \
					_SRC(x, 9), _SRC(x, 10), _SRC(x, 11), _SRC(x, 12), \
					len, coefficients + region, 0, NULL \
				); \
				region += x; \
			break
			CASE(12);
			CASE(11);
			CASE(10);
			CASE( 9);
			CASE( 8);
			CASE( 7);
			CASE( 6);
			CASE( 5);
			CASE( 4);
			CASE( 3);
			CASE( 2);
		#undef CASE
		default: break;
	}
	#undef _SRC
	return region;
}


static HEDLEY_ALWAYS_INLINE unsigned gf16_muladd_multi_packed(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, size_t blockLen, const uint16_t *HEDLEY_RESTRICT coefficients) {
	uint8_t* _dst = (uint8_t*)dst + len;
	uint8_t* _src = (uint8_t*)src;
	
	unsigned region = 0;
	if(regions >= interleave) do {
		muladd_pf(
			scratch, _dst, interleave, interleave,
			_src + region * len + len*interleave,
			_src + region * len + len*interleave + blockLen*1,
			_src + region * len + len*interleave + blockLen*2,
			_src + region * len + len*interleave + blockLen*3,
			_src + region * len + len*interleave + blockLen*4,
			_src + region * len + len*interleave + blockLen*5,
			_src + region * len + len*interleave + blockLen*6,
			_src + region * len + len*interleave + blockLen*7,
			_src + region * len + len*interleave + blockLen*8,
			_src + region * len + len*interleave + blockLen*9,
			_src + region * len + len*interleave + blockLen*10,
			_src + region * len + len*interleave + blockLen*11,
			_src + region * len + len*interleave + blockLen*12,
			len, coefficients + region, 0, NULL
		);
		region += interleave;
	} while(interleave <= regions - region);
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < interleave); // doesn't seem to always work, so we have additional checks in the switch cases
	switch(remaining) {
		#define CASE(x) \
			case x: \
				if(x >= interleave) HEDLEY_UNREACHABLE(); \
				muladd_pf( \
					scratch, _dst, x, x, \
					_src + region * len + len*x, \
					_src + region * len + len*x + blockLen*1, \
					_src + region * len + len*x + blockLen*2, \
					_src + region * len + len*x + blockLen*3, \
					_src + region * len + len*x + blockLen*4, \
					_src + region * len + len*x + blockLen*5, \
					_src + region * len + len*x + blockLen*6, \
					_src + region * len + len*x + blockLen*7, \
					_src + region * len + len*x + blockLen*8, \
					_src + region * len + len*x + blockLen*9, \
					_src + region * len + len*x + blockLen*10, \
					_src + region * len + len*x + blockLen*11, \
					_src + region * len + len*x + blockLen*12, \
					len, coefficients + region, 0, NULL \
				); \
				region += x; \
			break
			CASE(12);
			CASE(11);
			CASE(10);
			CASE( 9);
			CASE( 8);
			CASE( 7);
			CASE( 6);
			CASE( 5);
			CASE( 4);
			CASE( 3);
			CASE( 2);
		#undef CASE
		default: break;
	}
	return region;
}

#if defined(__ICC) || (defined(_MSC_VER) && !defined(__clang__))
# define MM_HINT_WT1 _MM_HINT_T1
#else
# define MM_HINT_WT1 _MM_HINT_ET1
#endif

static HEDLEY_ALWAYS_INLINE void gf16_muladd_multi_packpf(const void *HEDLEY_RESTRICT scratch, fMuladdPF muladd_pf, const unsigned interleave, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, size_t blockLen, const uint16_t *HEDLEY_RESTRICT coefficients, const unsigned pfFactor, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	uint8_t* _dst = (uint8_t*)dst + len;
	uint8_t* _src = (uint8_t*)src;
	
	unsigned region = 0;
	size_t pfLen = len>>pfFactor;
	const char* _pf = (const char*)prefetchOut + pfLen;
	if(regions >= interleave) {
		unsigned outputPfRounds = 1<<pfFactor;
		while(outputPfRounds--) {
			muladd_pf(
				scratch, _dst, interleave, interleave,
				_src + region * len + len*interleave,
				_src + region * len + len*interleave + blockLen*1,
				_src + region * len + len*interleave + blockLen*2,
				_src + region * len + len*interleave + blockLen*3,
				_src + region * len + len*interleave + blockLen*4,
				_src + region * len + len*interleave + blockLen*5,
				_src + region * len + len*interleave + blockLen*6,
				_src + region * len + len*interleave + blockLen*7,
				_src + region * len + len*interleave + blockLen*8,
				_src + region * len + len*interleave + blockLen*9,
				_src + region * len + len*interleave + blockLen*10,
				_src + region * len + len*interleave + blockLen*11,
				_src + region * len + len*interleave + blockLen*12,
				len, coefficients + region, 1, _pf
			);
			region += interleave;
			if(outputPfRounds)
				_pf += pfLen;
			else
				_pf = NULL;
			if(interleave > regions - region) break;
		}
	}
	if(_pf && interleave > regions - region) {
		unsigned remaining = regions - region;
		switch(remaining) {
			#define CASE(x) \
				case x: \
					if(x > interleave) HEDLEY_UNREACHABLE(); \
					muladd_pf( \
						scratch, _dst, x, x, \
						_src + region * len + len*x, \
						_src + region * len + len*x + blockLen*1, \
						_src + region * len + len*x + blockLen*2, \
						_src + region * len + len*x + blockLen*3, \
						_src + region * len + len*x + blockLen*4, \
						_src + region * len + len*x + blockLen*5, \
						_src + region * len + len*x + blockLen*6, \
						_src + region * len + len*x + blockLen*7, \
						_src + region * len + len*x + blockLen*8, \
						_src + region * len + len*x + blockLen*9, \
						_src + region * len + len*x + blockLen*10, \
						_src + region * len + len*x + blockLen*11, \
						_src + region * len + len*x + blockLen*12, \
						len, coefficients + region, 1, _pf \
					); \
					region += x; \
				break
				CASE(12);
				CASE(11);
				CASE(10);
				CASE( 9);
				CASE( 8);
				CASE( 7);
				CASE( 6);
				CASE( 5);
				CASE( 4);
				CASE( 3);
				CASE( 2);
				CASE( 1);
			#undef CASE
			default: break;
		}
		return;
	}
	
	if(prefetchIn) {
		_pf = (const char*)prefetchIn + pfLen;
		while(interleave <= regions - region) {
			muladd_pf(
				scratch, _dst, interleave, interleave,
				_src + region * len + len*interleave,
				_src + region * len + len*interleave + blockLen*1,
				_src + region * len + len*interleave + blockLen*2,
				_src + region * len + len*interleave + blockLen*3,
				_src + region * len + len*interleave + blockLen*4,
				_src + region * len + len*interleave + blockLen*5,
				_src + region * len + len*interleave + blockLen*6,
				_src + region * len + len*interleave + blockLen*7,
				_src + region * len + len*interleave + blockLen*8,
				_src + region * len + len*interleave + blockLen*9,
				_src + region * len + len*interleave + blockLen*10,
				_src + region * len + len*interleave + blockLen*11,
				_src + region * len + len*interleave + blockLen*12,
				len, coefficients + region, 2, _pf
			);
			region += interleave;
			_pf += pfLen;
		}
	}
	else while(interleave <= regions - region) {
		muladd_pf(
			scratch, _dst, interleave, interleave,
			_src + region * len + len*interleave,
			_src + region * len + len*interleave + blockLen*1,
			_src + region * len + len*interleave + blockLen*2,
			_src + region * len + len*interleave + blockLen*3,
			_src + region * len + len*interleave + blockLen*4,
			_src + region * len + len*interleave + blockLen*5,
			_src + region * len + len*interleave + blockLen*6,
			_src + region * len + len*interleave + blockLen*7,
			_src + region * len + len*interleave + blockLen*8,
			_src + region * len + len*interleave + blockLen*9,
			_src + region * len + len*interleave + blockLen*10,
			_src + region * len + len*interleave + blockLen*11,
			_src + region * len + len*interleave + blockLen*12,
			len, coefficients + region, 0, NULL
		);
		region += interleave;
	}
	
	unsigned remaining = regions - region;
	HEDLEY_ASSUME(remaining < interleave);
	switch(remaining) {
		#define CASE(x) \
			case x: \
				if(x > interleave) HEDLEY_UNREACHABLE(); \
				muladd_pf( \
					scratch, _dst, x, x, \
					_src + region * len + len*x, \
					_src + region * len + len*x + blockLen*1, \
					_src + region * len + len*x + blockLen*2, \
					_src + region * len + len*x + blockLen*3, \
					_src + region * len + len*x + blockLen*4, \
					_src + region * len + len*x + blockLen*5, \
					_src + region * len + len*x + blockLen*6, \
					_src + region * len + len*x + blockLen*7, \
					_src + region * len + len*x + blockLen*8, \
					_src + region * len + len*x + blockLen*9, \
					_src + region * len + len*x + blockLen*10, \
					_src + region * len + len*x + blockLen*11, \
					_src + region * len + len*x + blockLen*12, \
					len, coefficients + region, 0, NULL \
				); \
				region += x; \
			break
			CASE(12);
			CASE(11);
			CASE(10);
			CASE( 9);
			CASE( 8);
			CASE( 7);
			CASE( 6);
			CASE( 5);
			CASE( 4);
			CASE( 3);
			CASE( 2);
			CASE( 1);
		#undef CASE
		default: break;
	}
}
