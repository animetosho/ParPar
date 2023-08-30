#include "../src/platform.h"
#include "../src/stdint.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <arm_acle.h>
#endif

// function unused
/* HEDLEY_MALLOC static void* crc_alloc_arm() {
	uint32_t* mem = (uint32_t*)malloc(4);
	*mem = 0xffffffff;
	return mem;
} */

static HEDLEY_ALWAYS_INLINE void crc_init_arm(void* crc) {
	memset(crc, 0xff, sizeof(uint32_t));
}

static HEDLEY_ALWAYS_INLINE void crc_process_block_arm(void* HEDLEY_RESTRICT crc, const void* HEDLEY_RESTRICT src) {
	uint32_t* _crc = (uint32_t*)crc;
#ifdef __aarch64__
	for(int i=0; i<8; i++)
		*_crc = __crc32d(*_crc, _LE64(read64((uint64_t *)src + i)));
#else
	for(int i=0; i<16; i++)
		*_crc = __crc32w(*_crc, _LE32(read32((uint32_t *)src + i)));
#endif
}

static HEDLEY_ALWAYS_INLINE uint32_t crc_finish_arm(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src, size_t len) {
	uint32_t crc = read32(state);
	uint8_t* src_ = (uint8_t*)src;
#ifdef __aarch64__
	while (len >= sizeof(uint64_t)) {
		crc = __crc32d(crc, _LE64(read64(src_)));
		src_ += sizeof(uint64_t);
		len -= sizeof(uint64_t);
	}
	if (len & sizeof(uint32_t)) {
		crc = __crc32w(crc, _LE32(read32(src_)));
		src_ += sizeof(uint32_t);
	}
#else
	while (len >= sizeof(uint32_t)) {
		crc = __crc32w(crc, _LE32(read32(src_)));
		src_ += sizeof(uint32_t);
		len -= sizeof(uint32_t);
	}
#endif
	if (len & sizeof(uint16_t)) {
		crc = __crc32h(crc, _LE16(read16(src_)));
		src_ += sizeof(uint16_t);
	}
	if (len & sizeof(uint8_t))
		crc = __crc32b(crc, *src_);
	
	return ~crc;
}

