#ifndef __GF64_GLOBAL_H
#define __GF64_GLOBAL_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include "../src/platform.h"

#define GF64_POLYNOMIAL 0x1BULL
#define UNUSED(...) (void)(__VA_ARGS__)
#define MAX_STACK_BUF 256

#ifdef _MSC_VER
# define inline __inline
#endif

HEDLEY_BEGIN_C_DECLS

typedef uint64_t gf64_t;

typedef enum {
	GF64_AVX512=0,
	GF64_AVX2=1,
	GF64_SSSE3=2,
	GF64_SCALAR=3
} GF64Method;

typedef void (*gf64_region_mul_fn)(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
typedef void (*gf64_region_mul_arr_fn)(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);

extern gf64_region_mul_fn gf64_region_mul;
extern gf64_region_mul_arr_fn gf64_region_mul_arr;
extern GF64Method gf64_current_method;

GF64Method gf64_detect_method(void);
int gf64_init_dispatch(void);

HEDLEY_END_C_DECLS

#endif