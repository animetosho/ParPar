#include "../src/hedley.h"
#include "../src/stdint.h"

static HEDLEY_ALWAYS_INLINE uint32_t crc_multiply(uint32_t a, uint32_t b) {
	uint32_t res = 0;
	for(int i=0; i<31; i++) {
		res ^= -(b>>31) & a;
		a = ((a >> 1) ^ (0xEDB88320 & -(a&1)));
		b <<= 1;
	}
	res ^= -(b>>31) & a;
	return res;
}

static const uint32_t crc_power[] = { // pre-computed 2^n, with first 3 entries removed (saves a shift)
	0x00800000, 0x00008000, 0xedb88320, 0xb1e6b092, 0xa06a2517, 0xed627dae, 0x88d14467, 0xd7bbfe6a,
	0xec447f11, 0x8e7ea170, 0x6427800e, 0x4d47bae0, 0x09fe548f, 0x83852d0f, 0x30362f1a, 0x7b5a9cc3,
	0x31fec169, 0x9fec022a, 0x6c8dedc4, 0x15d6874d, 0x5fde7a4e, 0xbad90e37, 0x2e4e5eef, 0x4eaba214,
	0xa8a472c0, 0x429a969e, 0x148d302a, 0xc40ba6d0, 0xc4e22c3c, 0x40000000, 0x20000000, 0x08000000
};
/* above table can be computed with
	int main(void) {
		uint32_t k = 0x80000000 >> 1;
		for (size_t i = 0; i < 32+3; ++i) {
			if(i>2) printf("0x%08x, ", k);
			k = crc_multiply(k, k);
		}
		return 0;
	}
*/
uint32_t crc_zeroPad(uint32_t crc, uint64_t zeroPad) {
	// multiply by 2^(8n)
	unsigned power = 0;
	crc = ~crc;
	while(zeroPad) {
		if(zeroPad & 1)
			crc = crc_multiply(crc, crc_power[power]);
		zeroPad >>= 1;
		power = (power+1) & 31;
	}
	return ~crc;
}
