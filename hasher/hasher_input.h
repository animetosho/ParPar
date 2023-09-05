#ifndef __HASHER_INPUT_H
#define __HASHER_INPUT_H

#include "hasher_input_impl.h"
#include <vector>

enum HasherInputMethods {
	INHASH_SCALAR,
	INHASH_SIMD,
	INHASH_CRC,
	INHASH_SIMD_CRC,
	INHASH_BMI1,
	INHASH_AVX512
};

bool set_hasherInput(HasherInputMethods method);
extern IHasherInput*(*HasherInput_Create)();
extern HasherInputMethods HasherInput_Method;
const char* hasherInput_methodName(HasherInputMethods m);
inline const char* hasherInput_methodName() {
	return hasherInput_methodName(HasherInput_Method);
}

#endif /* __HASHER_INPUT_H */
