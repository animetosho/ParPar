// work around lack of warning functionality in GYP by getting the compiler to emit them
#include "hedley.h"

#ifdef __GYP_WARN_NO_NATIVE
HEDLEY_WARNING("`-march=native` unsupported by compiler. This build may not be properly optimized");
#endif

#ifndef _OPENMP
HEDLEY_WARNING("Compiler does not support OpenMP. This build will not support multi-threading");
#endif
