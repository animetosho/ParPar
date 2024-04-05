#include "hasher_input.h"
#include <string.h>
#include "../src/platform.h"

IHasherInput*(*HasherInput_Create)() = NULL;
HasherInputMethods HasherInput_Method = INHASH_SCALAR;

bool set_hasherInput(HasherInputMethods method) {
#define SET_HASHER(h, x) if(method == h) { \
		if(!x::isAvailable) return false; \
		HasherInput_Create = &x::create; \
		HasherInput_Method = h; \
		return true; \
	}
	
	SET_HASHER(INHASH_SCALAR, HasherInput_Scalar)
#ifdef PLATFORM_X86
	SET_HASHER(INHASH_SIMD, HasherInput_SSE)
	SET_HASHER(INHASH_CRC, HasherInput_ClMulScalar)
	SET_HASHER(INHASH_SIMD_CRC, HasherInput_ClMulSSE)
	SET_HASHER(INHASH_BMI1, HasherInput_BMI1)
	SET_HASHER(INHASH_AVX512, HasherInput_AVX512)
#endif
#ifdef PLATFORM_ARM
	SET_HASHER(INHASH_SIMD, HasherInput_NEON)
	SET_HASHER(INHASH_CRC, HasherInput_ARMCRC)
	SET_HASHER(INHASH_SIMD_CRC, HasherInput_NEONCRC)
#endif
#ifdef __riscv
	SET_HASHER(INHASH_CRC, HasherInput_RVZbc)
#endif
#undef SET_HASHER
	return false;
}

const char* hasherInput_methodName(HasherInputMethods m) {
	const char* names[] = {
		"Scalar+Generic",
#ifdef PLATFORM_X86
		"SSE2+Generic",
		"Scalar+PCLMUL",
		"SSE2+PCLMUL",
#elif defined(PLATFORM_ARM)
		"NEON+Generic",
		"Scalar+ARMCRC",
		"NEON+ARMCRC",
#elif defined(__riscv)
		"SIMD+Generic",
		"Scalar+Zbc",
		"SIMD+Zbc",
#else
		"SIMD+Generic",
		"Scalar+CRC",
		"SIMD+CRC",
#endif
		"BMI1+PCLMUL",
		"AVX512"
	};
	
	return names[(int)m];
}

