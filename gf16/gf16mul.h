#ifndef __GF16MUL_H
#define __GF16MUL_H

#include <cassert>
#include "../src/stdint.h"
#include "../src/hedley.h"
#include <vector>
#include <cstring>

typedef void(*Galois16MulTransform) (void* dst, const void* src, size_t srcLen);
typedef void(*Galois16MulTransformPacked) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
typedef void(*Galois16MulTransformPackedPartial) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen);
typedef void(*Galois16MulUntransform) (void *HEDLEY_RESTRICT dst, size_t len);
typedef void(*Galois16MulUntransformPacked) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
typedef int(*Galois16MulUntransformPackedCksum) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
typedef int(*Galois16MulUntransformPackedCksumPartial) (void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen);

typedef uint16_t(*Galois16ReplaceWord) (void* data, size_t index, uint16_t newValue);


typedef void(*Galois16MulFunc) (const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16MulRstFunc) (const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16MulPfFunc) (const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch);
typedef void(*Galois16PowFunc) (const void *HEDLEY_RESTRICT scratch, unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16MulMultiFunc) (const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16MulStridePfFunc) (const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch);
typedef void(*Galois16MulPackedFunc) (const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16MulPackPfFunc) (const void *HEDLEY_RESTRICT scratch, unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut);
typedef void(*Galois16AddFunc) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len);
typedef void(*Galois16AddMultiFunc) (unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len);
typedef void(*Galois16AddPackedFunc) (unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len);
typedef void(*Galois16AddPackPfFunc) (unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut);

typedef void(*Galois16CopyCksum) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen);
typedef int(*Galois16CopyCksumCheck) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len);


enum Galois16Methods {
	GF16_AUTO,
	GF16_LOOKUP,
	GF16_LOOKUP_SSE2,
	GF16_LOOKUP3,
	GF16_SHUFFLE_NEON,
	GF16_SHUFFLE_128_SVE,
	GF16_SHUFFLE_128_SVE2,
	GF16_SHUFFLE2X_128_SVE2,
	GF16_SHUFFLE_512_SVE2,
	GF16_SHUFFLE_128_RVV,
	GF16_SHUFFLE_SSSE3,
	GF16_SHUFFLE_AVX,
	GF16_SHUFFLE_AVX2,
	GF16_SHUFFLE_AVX512,
	GF16_SHUFFLE_VBMI,
	GF16_SHUFFLE2X_AVX2,
	GF16_SHUFFLE2X_AVX512,
	GF16_XOR_SSE2,
	GF16_XOR_JIT_SSE2,
	GF16_XOR_JIT_AVX2,
	GF16_XOR_JIT_AVX512,
	GF16_AFFINE_GFNI,
	GF16_AFFINE_AVX2,
	GF16_AFFINE_AVX512,
	GF16_AFFINE2X_GFNI,
	GF16_AFFINE2X_AVX2,
	GF16_AFFINE2X_AVX512,
	GF16_CLMUL_NEON,
	GF16_CLMUL_SHA3,
	GF16_CLMUL_SVE2
	// TODO: consider non-transforming shuffle/affine
};
static const char* Galois16MethodsText[] = {
	"Auto",
	"Lookup",
	"Lookup (SSE2)",
	"3-part Lookup",
	"Shuffle (NEON)",
	"Shuffle-128 (SVE)",
	"Shuffle-128 (SVE2)",
	"Shuffle2x-128 (SVE2)",
	"Shuffle-512 (SVE2)",
	"Shuffle-128 (RVV)",
	"Shuffle (SSSE3)",
	"Shuffle (AVX)",
	"Shuffle (AVX2)",
	"Shuffle (AVX512)",
	"Shuffle (VBMI)",
	"Shuffle2x (AVX2)",
	"Shuffle2x (AVX512)",
	"Xor (SSE2)",
	"Xor-Jit (SSE2)",
	"Xor-Jit (AVX2)",
	"Xor-Jit (AVX512)",
	"Affine (GFNI)",
	"Affine (GFNI+AVX2)",
	"Affine (GFNI+AVX512)",
	"Affine2x (GFNI)",
	"Affine2x (GFNI+AVX2)",
	"Affine2x (GFNI+AVX512)",
	"CLMul (NEON)",
	"CLMul (SHA3)",
	"CLMul (SVE2)"
};

typedef struct {
	Galois16Methods id;
	const char* name;
	size_t alignment;
	size_t stride;
	size_t idealChunkSize;
	unsigned idealInputMultiple;
	unsigned prefetchDownscale;
	unsigned cksumSize;
} Galois16MethodInfo;

class Galois16Mul {
private:
	void* scratch;
	Galois16MethodInfo _info;
	
	Galois16MulFunc _mul;
	Galois16MulRstFunc _mul_add;
	Galois16MulPfFunc _mul_add_pf;
	Galois16PowFunc _pow;
	Galois16PowFunc _pow_add;
	Galois16MulMultiFunc _mul_add_multi;
	Galois16MulStridePfFunc _mul_add_multi_stridepf;
	Galois16MulPackedFunc _mul_add_multi_packed;
	Galois16MulPackPfFunc _mul_add_multi_packpf;
	
	static void _prepare_none(void* dst, const void* src, size_t srcLen) {
		if(dst != src)
			memcpy(dst, src, srcLen);
	}
	static void _finish_none(void *HEDLEY_RESTRICT, size_t) {}
	static void _prepare_packed_none(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
	static uint16_t _replace_word(void* data, size_t index, uint16_t newValue) {
		uint8_t* p = (uint8_t*)data + index*2;
		uint16_t oldValue = p[0] | (p[1]<<8);
		p[0] = newValue & 0xff;
		p[1] = newValue>>8;
		return oldValue;
	}
	
	
	Galois16Methods _method;
	void setupMethod(Galois16Methods method);
	
	// disable copy constructor
	Galois16Mul(const Galois16Mul&);
	Galois16Mul& operator=(const Galois16Mul&);
	
#ifdef __cpp_rvalue_references
	void move(Galois16Mul& other);
#endif
	
public:
	static Galois16Methods default_method(size_t regionSizeHint = 1048576, unsigned inputs = 32768, unsigned outputs = 65535, bool forInvert = false);
	Galois16Mul(Galois16Methods method = GF16_AUTO);
	~Galois16Mul();
	
#ifdef __cpp_rvalue_references
	Galois16Mul(Galois16Mul&& other) noexcept {
		move(other);
	}
	Galois16Mul& operator=(Galois16Mul&& other) noexcept {
		move(other);
		return *this;
	}
#endif
	
	inline bool needPrepare() const {
		return prepare != &Galois16Mul::_prepare_none;
	};
	inline bool hasMultiMulAdd() const {
		return _mul_add_multi != NULL;
	};
	inline bool hasMultiMulAddPacked() const {
		return _mul_add_multi_packed != NULL;
	};
	inline bool hasPowAdd() const {
		return _pow_add != NULL;
	};
	
	static std::vector<Galois16Methods> availableMethods(bool checkCpuid);
	static inline const char* methodToText(Galois16Methods m) {
		return Galois16MethodsText[(int)m];
	}
	
	inline const Galois16MethodInfo& info() const {
		return _info;
	}
	static Galois16MethodInfo info(Galois16Methods _method);
	
	inline HEDLEY_CONST bool isMultipleOfStride(size_t len) const {
		return (len & (_info.stride-1)) == 0;
	}
	inline HEDLEY_CONST size_t alignToStride(size_t len) const {
		size_t alignMask = _info.stride-1;
		return (len + alignMask) & ~alignMask;
	}
	
	Galois16MulTransform prepare;
	Galois16MulTransformPacked prepare_packed;
	Galois16MulTransformPacked prepare_packed_cksum;
	Galois16MulTransformPackedPartial prepare_partial_packsum; // TODO: consider a nicer interface for this
	Galois16MulUntransform finish;
	Galois16MulUntransformPacked finish_packed;
	Galois16MulUntransformPackedCksum finish_packed_cksum;
	Galois16MulUntransformPackedCksumPartial finish_partial_packsum;
	Galois16ReplaceWord replace_word;
	Galois16AddMultiFunc add_multi;
	Galois16AddPackedFunc add_multi_packed;
	Galois16AddPackPfFunc add_multi_packpf;
	Galois16CopyCksum copy_cksum;
	Galois16CopyCksumCheck copy_cksum_check;
	
	HEDLEY_MALLOC void* mutScratch_alloc() const;
	void mutScratch_free(void* mutScratch) const;
	
	inline void mul(void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		
		if(HEDLEY_UNLIKELY(!(coefficient & 0xfffe))) {
			if(coefficient == 0)
				memset(dst, 0, len);
			else if(dst != src)
				memcpy(dst, src, len);
		}
		else
			_mul(scratch, dst, src, len, coefficient, mutScratch);
	}
	inline void mul_add(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		
		if(HEDLEY_UNLIKELY(coefficient == 0)) return;
		_mul_add(scratch, dst, src, len, coefficient, mutScratch);
	}
	
	inline void mul_add_pf(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		
		if(HEDLEY_UNLIKELY(coefficient == 0)) return;
		if(_mul_add_pf)
			_mul_add_pf(scratch, dst, src, len, coefficient, mutScratch, prefetch);
		else
			_mul_add(scratch, dst, src, len, coefficient, mutScratch);
	}
	
	inline void pow(unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(outputs > 0);
		
		if(HEDLEY_UNLIKELY(!(coefficient & 0xfffe))) {
			if(coefficient == 0) {
				for(unsigned output = 0; output < outputs; output++)
					memset((uint8_t*)dst[output] + offset, 0, len);
			} else {
				for(unsigned output = 0; output < outputs; output++)
					memcpy((uint8_t*)dst[output] + offset, (uint8_t*)src + offset, len);
			}
		}
		else if(_pow)
			_pow(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
		else if(_pow_add) {
			for(unsigned output = 0; output < outputs; output++)
				memset((uint8_t*)dst[output] + offset, 0, len);
			_pow_add(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
		}
		else {
			void* prev = (uint8_t*)src + offset;
			for(unsigned output = 0; output < outputs; output++) {
				void* cur = (uint8_t*)dst[output] + offset;
				_mul(scratch, cur, prev, len, coefficient, mutScratch);
				prev = cur;
			}
		}
	}
	inline void pow_add(unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(outputs > 0);
		
		if(HEDLEY_UNLIKELY(coefficient == 0)) return;
		_pow_add(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
	}
	
	inline void mul_add_multi(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(regions > 0);
		
		if(_mul_add_multi)
			_mul_add_multi(scratch, regions, offset, dst, src, len, coefficients, mutScratch);
		else {
			for(unsigned region = 0; region<regions; region++) {
				_mul_add(scratch, (uint8_t*)dst+offset, ((const uint8_t*)src[region])+offset, len, coefficients[region], mutScratch);
			}
		}
	}
	
	inline void mul_add_multi_stridepf(unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(srcStride > 0);
		assert(regions > 0);
		
		if(_mul_add_multi_stridepf) {
			_mul_add_multi_stridepf(scratch, regions, srcStride, dst, src, len, coefficients, mutScratch, prefetch);
			return;
		}
		
		// assume _mul_add_multi isn't set (exception: XorJit AVX512)
		// fallback to using single multiplies
		unsigned region = 0;
		size_t pfLen = len>>_info.prefetchDownscale;
		const char* _pf = (const char*)prefetch;
		for(unsigned outputPfRounds = 1<<_info.prefetchDownscale; region<regions && outputPfRounds; region++, outputPfRounds--) {
			_mul_add_pf(scratch, dst, (const uint8_t*)src+region*srcStride, len, coefficients[region], mutScratch, _pf);
			_pf += pfLen;
		}
		for(; region<regions; region++) {
			_mul_add(scratch, dst, (const uint8_t*)src+region*srcStride, len, coefficients[region], mutScratch);
		}
	}
	
	inline void mul_add_multi_packed(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(regions > 0);
		
		if(_mul_add_multi_packed)
			_mul_add_multi_packed(scratch, packedRegions, regions, dst, src, len, coefficients, mutScratch);
		else {
			for(unsigned region = 0; region<regions; region++) {
				_mul_add(scratch, dst, (uint8_t*)src + region*len, len, coefficients[region], mutScratch);
			}
		}
	}
	
	inline void mul_add_multi_packpf(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) const {
		assert(isMultipleOfStride(len));
		assert(len > 0);
		assert(regions > 0);
		
		// TODO: mul by 1?
		
		if(_mul_add_multi_packpf) {
			_mul_add_multi_packpf(scratch, packedRegions, regions, dst, src, len, coefficients, mutScratch, prefetchIn, prefetchOut);
			return;
		}
		if(_mul_add_multi_packed || !_mul_add_pf) {
			// implies no support for prefetching
			mul_add_multi_packed(packedRegions, regions, dst, src, len, coefficients, mutScratch);
			return;
		}
		
		// do using single multiplies
		unsigned region = 0;
		size_t pfLen = len>>_info.prefetchDownscale;
		// firstly, prefetch output
		const char* _pf = (const char*)prefetchOut;
		for(unsigned outputPfRounds = 1<<_info.prefetchDownscale; region<regions && outputPfRounds; region++, outputPfRounds--) {
			_mul_add_pf(scratch, dst, (uint8_t*)src + region*len, len, coefficients[region], mutScratch, _pf);
			_pf += pfLen;
		}
		// next, prefetch inputs
		if(prefetchIn) {
			_pf = (const char*)prefetchIn;
			for(; region<regions; region++) {
				_mul_add_pf(scratch, dst, (uint8_t*)src + region*len, len, coefficients[region], mutScratch, _pf);
				_pf += pfLen;
			}
		} else {
			for(; region<regions; region++) {
				_mul_add(scratch, dst, (uint8_t*)src + region*len, len, coefficients[region], mutScratch);
			}
		}
	}
	
};

#endif
