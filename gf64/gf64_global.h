#ifndef __GF64_GLOBAL_H
#define __GF64_GLOBAL_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include "../src/platform.h"

#define GF64_POLYNOMIAL 0x100000000000001BULL
#define UNUSED(...) (void)(__VA_ARGS__)
#define MAX_STACK_BUF 256

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
typedef void (*gf64_region_muladd_arr_fn)(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
typedef void (*gf64_region_coupled_muladd_arr_fn)(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, const gf64_t *HEDLEY_RESTRICT coeff_blocks, size_t len, size_t G);
typedef void (*gf64_region_fused_output_muladd_arr_fn)(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_block_starts, size_t len, size_t K);
typedef void (*gf64_region_2d_muladd_arr_fn)(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, size_t K, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, size_t G, const gf64_t *HEDLEY_RESTRICT coeff_block_2d, size_t K_stride, size_t len);
typedef void (*gf64_inverse_batch_fn)(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);

extern gf64_region_mul_fn gf64_region_mul;
extern gf64_region_mul_arr_fn gf64_region_mul_arr;
extern gf64_region_muladd_arr_fn gf64_region_muladd_arr;
extern gf64_region_coupled_muladd_arr_fn gf64_region_coupled_muladd_arr;
extern gf64_region_fused_output_muladd_arr_fn gf64_region_fused_output_muladd_arr;
extern gf64_region_2d_muladd_arr_fn gf64_region_2d_muladd_arr;
extern gf64_inverse_batch_fn gf64_inverse_batch;
extern GF64Method gf64_current_method;
extern gf64_t gf64_inverse(gf64_t a);

GF64Method gf64_detect_method(void);
int gf64_init_dispatch(void);

/* PD2: pick the optimal GF(2^64) ISA for a specific workload. On Zen4,
 * AVX-512 triggers a 2x frequency downclock; if the working set exceeds
 * 16 MiB, AVX-2 wins despite lower per-instruction throughput because it
 * keeps the nominal frequency. Env override PAR3_AVX512_FORCE=1|0 bypasses
 * the heuristic. Returns the chosen method; does NOT bind the dispatch
 * function pointers — caller must call gf64_apply_method() for that. */
GF64Method gf64_method_for_workload(size_t num_in, size_t num_out, size_t block_size);

/* Bind the global GF(2^64) function pointers to the given method.
 * No env lookup, no detection; idempotent. */
void gf64_apply_method(GF64Method method);

HEDLEY_END_C_DECLS

#endif