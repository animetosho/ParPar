#include "gf16_global.h"
#include "../src/platform.h"
#include <string.h>

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _gfni_avx512
#define _FNPREP(f) f##_gfni_avx512
#define _MM_END _mm256_zeroupper();

#if defined(__GFNI__) && defined(__AVX512F__) && defined(__AVX512BW__)
# define _COMPILED_WITH_GFNI 1
#endif

#if defined(_COMPILED_WITH_GFNI)
static int gfni_available = -1;

/* Threshold for non-temporal stores: use write-combining for large transfers
 * that exceed L3 cache size to avoid cache pollution.
 * 256KB is a conservative threshold for typical L3 cache sizes. */
#define NON_TEMPORAL_THRESHOLD (256 * 1024)

static HEDLEY_ALWAYS_INLINE intptr_t is_aligned(const void* ptr, size_t alignment) {
    return ((intptr_t)ptr & (alignment - 1)) == 0;
}

static HEDLEY_ALWAYS_INLINE int should_use_nt_store(size_t len, const void* dst) {
    return len >= NON_TEMPORAL_THRESHOLD && is_aligned(dst, 16);
}

static void check_gfni_support(void) {
#if defined(__GNUC__) || defined(__clang__)
    #if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
    gfni_available = __builtin_cpu_supports("gfni") ? 1 : 0;
    #else
    gfni_available = 0;
    #endif
#else
    gfni_available = 1;
#endif
}

int gf16_mul_gfni_avx512_available(void) {
    if(gfni_available == -1) {
        check_gfni_support();
    }
    return gfni_available;
}
#else
int gf16_mul_gfni_avx512_available(void) {
    return 0;
}
#endif

static HEDLEY_ALWAYS_INLINE uint16_t gf16_mul_single_scalar(uint16_t a, uint16_t b) {
    uint16_t result = 0;
    uint16_t temp_a = a;
    for(int i = 0; i < 16; i++) {
        if((b >> i) & 1) {
            result ^= temp_a;
        }
        temp_a = (temp_a << 1) ^ (0x1100B & -(temp_a >> 15));
    }
    return result;
}

#if defined(_COMPILED_WITH_GFNI)
static HEDLEY_ALWAYS_INLINE void generate_mul_matrices(uint16_t coefficient, __m512i* mat_lo, __m512i* mat_hi) {
    uint64_t mat_lo_vals[8];
    uint64_t mat_hi_vals[8];
    memset(mat_lo_vals, 0, sizeof(mat_lo_vals));
    memset(mat_hi_vals, 0, sizeof(mat_hi_vals));

    for(int coeff_bit = 0; coeff_bit < 16; coeff_bit++) {
        if((coefficient >> coeff_bit) & 1) {
            uint16_t basis = 1 << coeff_bit;
            for(int input_bit = 0; input_bit < 16; input_bit++) {
                uint16_t input_with_bit = 1 << (input_bit % 8);
                uint16_t result = gf16_mul_single_scalar(basis, input_with_bit);
                for(int output_bit = 0; output_bit < 16; output_bit++) {
                    if((result >> output_bit) & 1) {
                        int out_byte = output_bit / 8;
                        int out_bit_in_byte = output_bit % 8;
                        int pos = (7 - out_byte) * 8 + out_bit_in_byte;
                        if(input_bit < 8) {
                            mat_lo_vals[7 - out_byte] |= (uint64_t)1 << pos;
                        } else {
                            mat_hi_vals[7 - out_byte] |= (uint64_t)1 << pos;
                        }
                    }
                }
            }
        }
    }

    for(int i = 0; i < 8; i++) {
        mat_lo[i] = _mm512_set1_epi64(mat_lo_vals[i]);
        mat_hi[i] = _mm512_set1_epi64(mat_hi_vals[i]);
    }
}
#endif

void gf16_mul_gfni_avx512(
    void *HEDLEY_RESTRICT dst_,
    const void *HEDLEY_RESTRICT src_,
    size_t len,
    uint16_t coefficient,
    void *HEDLEY_RESTRICT mutScratch
) {
    UNUSED(mutScratch);
#if defined(_COMPILED_WITH_GFNI)
    if(!gf16_mul_gfni_avx512_available()) {
        memset(dst_, 0, len);
        return;
    }

    uint8_t* dst = (uint8_t*)dst_;
    const uint8_t* src = (uint8_t*)src_;

    __m512i mat_lo[8];
    __m512i mat_hi[8];
    generate_mul_matrices(coefficient, mat_lo, mat_hi);

    size_t blocks = len / 64;
    size_t remainder = len % 64;

    /* Determine if non-temporal stores are beneficial for this transfer */
    int use_nt = should_use_nt_store(len, dst);

    for(size_t block = 0; block < blocks; block++) {
        size_t offset = block * 64;
        const __m512i* src_vec = (const __m512i*)(src + offset);
        __m512i* dst_vec = (__m512i*)(dst + offset);

        /* Prefetch source data 2 blocks ahead for better cache utilization */
        if(block + 2 < blocks) {
            _mm_prefetch((const char*)(src + offset + 128), _MM_HINT_NTA);
        }

        /* Prefetch destination for write-combining 1 block ahead */
        if(block + 1 < blocks) {
            _mm_prefetch((char*)(dst + offset + 64), _MM_HINT_NTA);
        }

        __m512i data = _mm512_load_si512(src_vec);

        __m512i result = _mm512_setzero_si512();

        for(int lane = 0; lane < 8; lane++) {
            __m512i lane_data = _mm512_srli_epi64(data, lane * 8);
            lane_data = _mm512_and_si512(lane_data, _mm512_set1_epi64(0xFF));

            __m512i lo_part = _mm512_gf2p8affine_epi64_epi8(lane_data, mat_lo[lane], 0);
            __m512i hi_part = _mm512_gf2p8affine_epi64_epi8(lane_data, mat_hi[lane], 0);

            result = _mm512_xor_si512(result, lo_part);
            result = _mm512_xor_si512(result, hi_part);
        }

        /* Use non-temporal stores for large aligned transfers to avoid cache pollution.
         * Requires 16-byte alignment which we checked in should_use_nt_store. */
        if(use_nt) {
            _mm512_stream_si512(dst_vec, result);
        } else {
            _mm512_store_si512(dst_vec, result);
        }
    }

    size_t offset = blocks * 64;
    for(size_t i = 0; i < remainder; i += 2) {
        if(offset + i + 1 < len) {
            uint16_t a = src[offset + i] | ((uint16_t)src[offset + i + 1] << 8);
            uint16_t res = gf16_mul_single_scalar(a, coefficient);
            dst[offset + i] = res & 0xff;
            dst[offset + i + 1] = (res >> 8) & 0xff;
        }
    }

    _mm256_zeroupper();
#else
    UNUSED(dst_); UNUSED(src_); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_mul_region_gfni_avx512(
    void *HEDLEY_RESTRICT dst_,
    const void *HEDLEY_RESTRICT src_,
    size_t len,
    uint16_t coefficient,
    void *HEDLEY_RESTRICT mutScratch
) {
    gf16_mul_gfni_avx512(dst_, src_, len, coefficient, mutScratch);
}