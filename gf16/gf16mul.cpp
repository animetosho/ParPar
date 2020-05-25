#include "gf16mul.h"
#include "gf16_global.h"
#include <cstdlib>

extern "C" {
	#include "gf16_lookup.h"
	#include "gf16_shuffle.h"
	#include "gf16_affine.h"
	#include "gf16_xor.h"
}

// CPUID stuff
#include "platform.h"
#ifdef PLATFORM_X86
# ifdef _MSC_VER
	#include <intrin.h>
	#define _cpuid __cpuid
	#define _cpuidX __cpuidex
	#if _MSC_VER >= 1600
		#include <immintrin.h>
		#define _GET_XCR() _xgetbv(_XCR_XFEATURE_ENABLED_MASK)
	#endif
# else
	#include <cpuid.h>
	/* GCC seems to support this, I assume everyone else does too? */
	#define _cpuid(ar, eax) __cpuid(eax, ar[0], ar[1], ar[2], ar[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
	
	static inline int _GET_XCR() {
		int xcr0;
		__asm__ __volatile__("xgetbv" : "=a" (xcr0) : "c" (0) : "%edx");
		return xcr0;
	}
# endif
# include "x86_jit.h"
struct CpuCap {
	bool hasSSE2, hasSSSE3, hasAVX, hasAVX2, hasAVX512VLBW, hasGFNI;
	size_t propPrefShuffleThresh;
	bool propAVX128EU, propHT;
	bool canMemWX;
	CpuCap(bool detect) :
	  hasSSE2(true),
	  hasSSSE3(true),
	  hasAVX(true),
	  hasAVX2(true),
	  hasAVX512VLBW(true),
	  hasGFNI(true),
	  propPrefShuffleThresh(0),
	  propAVX128EU(false),
	  propHT(false),
	  canMemWX(true)
	{
		if(!detect) return;
		
		int cpuInfo[4];
		int cpuInfoX[4];
		int family, model, hasMulticore;
		_cpuid(cpuInfo, 1);
		hasMulticore = (cpuInfo[3] & (1<<28));
		hasSSE2 = (cpuInfo[3] & 0x4000000);
		hasSSSE3 = (cpuInfo[2] & 0x200);
		
		family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>16) & 0xff0);
		model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
		
		propPrefShuffleThresh = 131072; // it seems like XOR JIT is always faster than shuffle at ~128KB sizes
		
		if(family == 6) {
			/* from handy table at http://a4lg.com/tech/x86/database/x86-families-and-models.en.html */
			if(model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4C || model == 0x4D || model == 0x5A || model == 0x5D) {
				/* we have a Bonnell/Silvermont CPU with a really slow pshufb instruction; pretend SSSE3 doesn't exist, as XOR_DEPENDS is much faster */
				propPrefShuffleThresh = 2048;
			}
			if(model == 0x0F || model == 0x16) {
				/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
				propPrefShuffleThresh = 16384;
			}
		}
		if((family == 0x5f && (model == 0 || model == 1 || model == 2)) || (family == 0x6f && (model == 0 || model == 0x10 || model == 0x20 || model == 0x30))) {
			/* Jaguar has a slow shuffle instruction and XOR is much faster; presumably the same for Bobcat/Puma */
			propPrefShuffleThresh = 2048;
		}
		
		propAVX128EU = ( // CPUs with 128-bit AVX units
			   family == 0x6f // AMD Bulldozer family
			|| family == 0x7f // AMD Jaguar/Puma family
			|| (family == 0x8f && (model == 0 /*Summit Ridge ES*/ || model == 1 /*Zen*/ || model == 8 /*Zen+*/ || model == 0x11 /*Zen APU*/ || model == 0x18 /*Zen+ APU*/ || model == 0x50 /*Subor Z+*/)) // AMD Zen1 family
			|| (family == 6 && model == 0xf) // Centaur/Zhaoxin; overlaps with Intel Core 2, but they don't support AVX
		);
		
		hasAVX = false; hasAVX2 = false; hasAVX512VLBW = false; hasGFNI = false;
#if !defined(_MSC_VER) || _MSC_VER >= 1600
		_cpuidX(cpuInfoX, 7, 0);
		if(cpuInfo[2] & 0x8000000) { // has OSXSAVE
			int xcr = _GET_XCR() & 0xff;
			if((xcr & 6) == 6) { // AVX enabled
				hasAVX = cpuInfo[2] & 0x800000;
				hasAVX2 = cpuInfoX[1] & 0x20;
				// checks AVX512BW + AVX512VL + AVX512F
				hasAVX512VLBW = ((cpuInfoX[1] & 0xC0010000) == 0xC0010000) && ((xcr & 0xE0) == 0xE0);
			}
		}
		hasGFNI = (cpuInfoX[2] & 0x100) == 0x100;
#endif
	
		/* try to detect hyper-threading */
		propHT = false;
		if(hasMulticore) {
			/* only Intel CPUs have HT (VMs which obscure CPUID -> too bad); we won't include AMD Zen here */
			int cpuInfoModel[4];
			_cpuid(cpuInfoModel, 0);
			if(cpuInfoModel[1] == 0x756E6547 && cpuInfoModel[2] == 0x6C65746E && cpuInfoModel[3] == 0x49656E69 && cpuInfoModel[0] >= 11) {
				_cpuidX(cpuInfoModel, 11, 0);
				if(((cpuInfoModel[2] >> 8) & 0xFF) == 1 // SMT level
				&& (cpuInfoModel[1] & 0xFFFF) > 1) // multiple threads per core
					propHT = true;
			}
		}
		
		// test for JIT capability
		void* jitTest = jit_alloc(256);
		canMemWX = (jitTest != NULL);
		if(jitTest) jit_free(jitTest, 256);
	}
};
#endif
#ifdef PLATFORM_ARM
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__linux__)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# endif
struct CpuCap {
	bool hasNEON;
	CpuCap(bool detect) : hasNEON(true) {
		if(!detect) return;
		hasNEON = false;
		
# if defined(AT_HWCAP)
#  ifdef __aarch64__
		hasNEON = getauxval(AT_HWCAP) & HWCAP_ASIMD;
#  else
		hasNEON = getauxval(AT_HWCAP) & HWCAP_NEON;
#  endif
# elif defined(ANDROID_CPU_FAMILY_ARM)
#  ifdef __aarch64__
		hasNEON = android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD;
#  else
		hasNEON = android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON;
#  endif
# endif
	}
};
#endif



void Galois16Mul::addGeneric(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len) {
	assert((len & 1) == 0);
	//assert(((uintptr_t)dst & (alignment-1)) == 0);
	//assert(((uintptr_t)src & (alignment-1)) == 0);
	assert(len > 0);
	
	
	len >>= 1;
	uint16_t* _dst = (uint16_t*)dst + len;
	uint16_t* _src = (uint16_t*)src + len;
	
	// let compiler figure out optimal width for target platform
	for(long ptr = -(long)len; ptr; ptr++) {
		_dst[ptr] ^= _src[ptr];
	}
}

void Galois16Mul::setupMethod(Galois16Methods method) {
	if(method == GF16_AUTO)
		method = default_method();
	
	switch(method) {
		case GF16_SHUFFLE_AVX512:
		case GF16_SHUFFLE_AVX2:
		case GF16_SHUFFLE_AVX:
		case GF16_SHUFFLE_SSSE3:
			alignment = 16;
			scratch = gf16_shuffle_init_x86(GF16_POLYNOMIAL);
			// TODO: set _add
			
			switch(method) {
				case GF16_SHUFFLE_SSSE3:
					if(!gf16_shuffle_available_ssse3) {
						setupMethod(GF16_AUTO);
						return;
					}
					_mul = &gf16_shuffle_mul_ssse3;
					_mul_add = &gf16_shuffle_muladd_ssse3;
					prepare = &gf16_shuffle_prepare_ssse3;
					finish = &gf16_shuffle_finish_ssse3;
				break;
				case GF16_SHUFFLE_AVX:
					if(!gf16_shuffle_available_avx) {
						setupMethod(GF16_AUTO);
						return;
					}
					_mul = &gf16_shuffle_mul_avx;
					_mul_add = &gf16_shuffle_muladd_avx;
					prepare = &gf16_shuffle_prepare_avx;
					finish = &gf16_shuffle_finish_avx;
				break;
				case GF16_SHUFFLE_AVX2:
					if(!gf16_shuffle_available_avx2) {
						setupMethod(GF16_AUTO);
						return;
					}
					_mul = &gf16_shuffle_mul_avx2;
					_mul_add = &gf16_shuffle_muladd_avx2;
					prepare = &gf16_shuffle_prepare_avx2;
					finish = &gf16_shuffle_finish_avx2;
					alignment = 32;
				break;
				case GF16_SHUFFLE_AVX512:
					if(!gf16_shuffle_available_avx512) {
						setupMethod(GF16_AUTO);
						return;
					}
					_mul = &gf16_shuffle_mul_avx512;
					_mul_add = &gf16_shuffle_muladd_avx512;
					prepare = &gf16_shuffle_prepare_avx512;
					finish = &gf16_shuffle_finish_avx512;
					alignment = 64;
				break;
				default: break; // for pedantic compilers
			}
			stride = alignment*2;
		break;
		
		case GF16_SHUFFLE_NEON:
			alignment = 16;
			stride = 32;
			scratch = gf16_shuffle_init_arm(GF16_POLYNOMIAL);
			// TODO: set _add
			
			if(!gf16_shuffle_available_neon) {
				setupMethod(GF16_AUTO);
				return;
			}
			_mul = &gf16_shuffle_mul_neon;
			_mul_add = &gf16_shuffle_muladd_neon;
		break;
		
		case GF16_AFFINE_AVX512:
			scratch = gf16_affine_init_avx512(GF16_POLYNOMIAL);
			alignment = 64;
			stride = 128;
			if(!gf16_affine_available_avx512 || !gf16_shuffle_available_avx512) {
				setupMethod(GF16_AUTO);
				return;
			}
			_mul = &gf16_affine_mul_avx512;
			_mul_add = &gf16_affine_muladd_avx512;
			prepare = &gf16_shuffle_prepare_avx512;
			finish = &gf16_shuffle_finish_avx512;
		break;
		
		case GF16_AFFINE_GFNI:
			scratch = gf16_affine_init_gfni(GF16_POLYNOMIAL);
			alignment = 16;
			stride = 32;
			if(!gf16_affine_available_gfni || !gf16_shuffle_available_ssse3) {
				setupMethod(GF16_AUTO);
				return;
			}
			_mul = &gf16_affine_mul_gfni;
			_mul_add = &gf16_affine_muladd_gfni;
			prepare = &gf16_shuffle_prepare_ssse3;
			finish = &gf16_shuffle_finish_ssse3;
		break;
		
		case GF16_XOR_JIT_AVX512:
		case GF16_XOR_JIT_AVX2:
		//case GF16_XOR_JIT_AVX:
		case GF16_XOR_JIT_SSE2:
		case GF16_XOR_SSE2:
			alignment = 16;
			
			switch(method) {
				case GF16_XOR_JIT_SSE2:
				case GF16_XOR_SSE2:
					if(!gf16_xor_available_sse2) {
						setupMethod(GF16_AUTO);
						return;
					}
					if(method == GF16_XOR_SSE2) {
						scratch = gf16_xor_init_sse2(GF16_POLYNOMIAL);
						_mul = &gf16_xor_mul_sse2;
						_mul_add = &gf16_xor_muladd_sse2;
					} else {
						scratch = gf16_xor_jit_init_sse2(GF16_POLYNOMIAL);
						_mul = &gf16_xor_jit_mul_sse2;
						_mul_add = &gf16_xor_jit_muladd_sse2;
					}
					prepare = &gf16_xor_prepare_sse2;
					finish = &gf16_xor_finish_sse2;
				break;
				/*
				case GF16_XOR_JIT_AVX:
					if(!gf16_xor_available_avx) {
						setupMethod(GF16_AUTO);
						return;
					}
					scratch = gf16_xor_jit_init_sse2(GF16_POLYNOMIAL);
					_mul = &gf16_xor_jit_mul_avx;
					_mul_add = &gf16_xor_jit_muladd_avx;
					prepare = &gf16_xor_prepare_avx;
					finish = &gf16_xor_finish_avx;
				break;
				*/
				case GF16_XOR_JIT_AVX2:
					if(!gf16_xor_available_avx2) {
						setupMethod(GF16_AUTO);
						return;
					}
					scratch = gf16_xor_jit_init_avx2(GF16_POLYNOMIAL);
					_mul = &gf16_xor_jit_mul_avx2;
					_mul_add = &gf16_xor_jit_muladd_avx2;
					prepare = &gf16_xor_prepare_avx2;
					finish = &gf16_xor_finish_avx2;
					alignment = 32;
				break;
				case GF16_XOR_JIT_AVX512:
					if(!gf16_xor_available_avx512) {
						setupMethod(GF16_AUTO);
						return;
					}
					scratch = gf16_xor_jit_init_avx512(GF16_POLYNOMIAL);
					_mul = &gf16_xor_jit_mul_avx512;
					_mul_add = &gf16_xor_jit_muladd_avx512;
					prepare = &gf16_xor_prepare_avx512;
					finish = &gf16_xor_finish_avx512;
					alignment = 64;
				break;
				default: break; // for pedantic compilers
			}
			
			stride = alignment*16;
		break;
		
		case GF16_LOOKUP_SSE2:
			_mul = &gf16_lookup_mul_sse2;
			_mul_add = &gf16_lookup_muladd_sse2;
			alignment = 16;
			stride = 16;
		break;
		
		case GF16_LOOKUP:
		default:
			_mul = &gf16_lookup_mul;
			_mul_add = &gf16_lookup_mul_add;
			stride = gf16_lookup_stride();
			alignment = stride; // assume platform doesn't like misalignment
		break;
	}
	_method = method;
}

Galois16Mul::Galois16Mul(Galois16Methods method) {
	scratch = NULL;
	prepare = &Galois16Mul::_prepare_none;
	finish = &Galois16Mul::_finish_none;
	alignment = 2;
	stride = 2;
	
	_mul = NULL;
	_add = &Galois16Mul::addGeneric;
	_mul_add_multi = &Galois16Mul::_mul_add_multi_none;
	
	setupMethod(method);
}

Galois16Mul::~Galois16Mul() {
	if(scratch)
		ALIGN_FREE(scratch);
}

#if __cplusplus >= 201100
void Galois16Mul::move(Galois16Mul& other) {
	scratch = other.scratch;
	other.scratch = NULL;
	
	prepare = other.prepare;
	finish = other.finish;
	alignment = other.alignment;
	stride = other.stride;
	_mul = other._mul;
	_add = other._add;
	_mul_add = other._mul_add;
	_mul_add_multi = other._mul_add_multi;
	_method = other._method;
}
#endif


void* Galois16Mul::mutScratch_alloc() const {
	switch(_method) {
		case GF16_XOR_JIT_SSE2:
			return gf16_xor_jit_init_mut_sse2();
		case GF16_XOR_JIT_AVX2:
			return gf16_xor_jit_init_mut_avx2();
		case GF16_XOR_JIT_AVX512:
			return gf16_xor_jit_init_mut_avx512();
		break;
		default:
			return NULL;
	}
}
void Galois16Mul::mutScratch_free(void* mutScratch) const {
	switch(_method) {
		case GF16_XOR_JIT_SSE2:
		case GF16_XOR_JIT_AVX2:
		case GF16_XOR_JIT_AVX512:
			gf16_xor_jit_uninit(mutScratch);
		break;
		default: break;
	}
}

Galois16Methods Galois16Mul::default_method(size_t regionSizeHint, unsigned /*outputs*/, unsigned /*threadCountHint*/) {
	const CpuCap caps(true);
	
#ifdef PLATFORM_X86
	if(caps.hasAVX512VLBW) {
		if(gf16_affine_available_avx512 && caps.hasGFNI)
			return GF16_AFFINE_AVX512;
		if(gf16_shuffle_available_avx512)
			return GF16_SHUFFLE_AVX512;
	}
	if(caps.hasAVX2) {
# ifndef PLATFORM_AMD64
		if(gf16_shuffle_available_avx2 && !caps.propAVX128EU)
			return GF16_SHUFFLE_AVX2;
# endif
		if(gf16_shuffle_available_avx2 && caps.propHT) // Intel AVX2 CPU with HT - it seems that shuffle256 is roughly same as xor256 so prefer former
			return GF16_SHUFFLE_AVX2;
# ifdef PLATFORM_AMD64
		if(gf16_xor_available_avx2 && caps.canMemWX) // TODO: check size hint?
			return GF16_XOR_JIT_AVX2;
		if(gf16_shuffle_available_avx2)
			return GF16_SHUFFLE_AVX2;
# endif
	}
	// TODO: add GFNI affine somewhere here
	if(!regionSizeHint || regionSizeHint > caps.propPrefShuffleThresh) {
		//if(gf16_xor_available_avx && caps.hasAVX && caps.canMemWX)
		//	return GF16_XOR_JIT_AVX;
		if(gf16_xor_available_sse2 && caps.hasSSE2 && caps.canMemWX)
			return GF16_XOR_JIT_SSE2;
	}
	if(gf16_shuffle_available_avx && caps.hasAVX)
		return GF16_SHUFFLE_AVX;
	if(gf16_shuffle_available_ssse3 && caps.hasSSSE3)
		return GF16_SHUFFLE_SSSE3;
	if(gf16_xor_available_sse2 && caps.hasSSE2)
		return GF16_XOR_SSE2;
#endif
#ifdef PLATFORM_ARM
	if(gf16_shuffle_available_neon && caps.hasNEON)
		return GF16_SHUFFLE_NEON;
#endif
	
	return GF16_LOOKUP;
}

std::vector<Galois16Methods> Galois16Mul::availableMethods(bool checkCpuid) {
	std::vector<Galois16Methods> ret;
	ret.push_back(GF16_LOOKUP);
	
	const CpuCap caps(checkCpuid);
#ifdef PLATFORM_X86
	if(gf16_shuffle_available_ssse3 && caps.hasSSSE3)
		ret.push_back(GF16_SHUFFLE_SSSE3);
	if(gf16_shuffle_available_avx && caps.hasAVX)
		ret.push_back(GF16_SHUFFLE_AVX);
	if(gf16_shuffle_available_avx2 && caps.hasAVX2)
		ret.push_back(GF16_SHUFFLE_AVX2);
	if(gf16_shuffle_available_avx512 && caps.hasAVX512VLBW)
		ret.push_back(GF16_SHUFFLE_AVX512);
	
	if(caps.hasGFNI) {
		if(gf16_affine_available_gfni && gf16_shuffle_available_ssse3 && caps.hasSSSE3)
			ret.push_back(GF16_AFFINE_GFNI);
		if(gf16_affine_available_avx512 && gf16_shuffle_available_avx512 && caps.hasAVX512VLBW)
			ret.push_back(GF16_AFFINE_AVX512);
	}
	
	if(gf16_xor_available_sse2 && caps.hasSSE2) {
		ret.push_back(GF16_XOR_SSE2);
		ret.push_back(GF16_LOOKUP_SSE2);
	}
	if(caps.canMemWX) {
		if(gf16_xor_available_sse2 && caps.hasSSE2)
			ret.push_back(GF16_XOR_JIT_SSE2);
		//if(gf16_xor_available_avx && caps.hasAVX)
		//	ret.push_back(GF16_XOR_JIT_AVX);
		if(gf16_xor_available_avx2 && caps.hasAVX2)
			ret.push_back(GF16_XOR_JIT_AVX2);
		if(gf16_xor_available_avx512 && caps.hasAVX512VLBW)
			ret.push_back(GF16_XOR_JIT_AVX512);
	}
#endif
#ifdef PLATFORM_ARM
	if(gf16_shuffle_available_neon && caps.hasNEON)
		ret.push_back(GF16_SHUFFLE_NEON);
#endif
	
	return ret;
}

unsigned Galois16Mul::_mul_add_multi_none(const void *HEDLEY_RESTRICT, unsigned, size_t, void *HEDLEY_RESTRICT, const void* *HEDLEY_RESTRICT, size_t, const uint16_t *HEDLEY_RESTRICT, void *HEDLEY_RESTRICT) {
	return 0;
}
