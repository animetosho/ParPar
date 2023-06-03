#include "gfmat_coeff.h"
#include <stdlib.h>

static int8_t* input_diff = NULL; // difference between predicted input coefficient and actual (number range is -4...5, so could be compressed to 4 bits, but I don't feel it's worth the savings)
static uint16_t* gf_exp = NULL; // pre-calculated exponents in GF(2^16), missing bottom 3 bits, followed by 128-entry polynomial shift table
#ifdef PARPAR_INVERT_SUPPORT
uint16_t* gf16_recip = NULL; // full GF(2^16) reciprocal table
#endif
void gfmat_init() {
	if(input_diff) return;
	
	input_diff = (int8_t*)malloc(32768);
	gf_exp = (uint16_t*)malloc((8192+128)*2);
#ifdef PARPAR_INVERT_SUPPORT
	gf16_recip = (uint16_t*)malloc(65536*2);
#endif
	
	int exp = 0, n = 1;
	for (int i = 0; i < 32768; i++) {
		do {
#ifdef PARPAR_INVERT_SUPPORT
			gf16_recip[n] = exp; // essentially construct a log table, then alter it later to get the reciprocal
#endif
			if((exp & 7) == 0) gf_exp[exp>>3] = n;
			exp++; // exp will reach 65534 by the end of the loop
			n <<= 1;
			if(n > 65535) n ^= 0x1100B;
		} while( !(exp%3) || !(exp%5) || !(exp%17) || !(exp%257) );
		
		input_diff[i] = exp - i*2;
	}
#ifdef PARPAR_INVERT_SUPPORT
	gf16_recip[n] = exp;
#endif
	
	// correction values for handling the missing bottom 3 bits of exp
	// essentially this is a table to speed up multiplication by 0...127 by applying the effects of polynomial masking
	for (int i = 0; i < 128; i++) {
		n = i << 9;
		for (int j = 0; j < 7; j++) {
			n <<= 1;
			if(n > 65535) n ^= 0x1100B;
		}
		gf_exp[8192+i] = n;
	}
	
#ifdef PARPAR_INVERT_SUPPORT
	gf16_recip[1] = 65535;
	// exponentiate for reciprocals
	for (int i = 1; i < 65536; i++) {
		gf16_recip[i] = gf16_exp(65535 - gf16_recip[i]);
	}
#endif
}

void gfmat_free() {
	free(input_diff);
	free(gf_exp);
	input_diff = NULL;
	gf_exp = NULL;
#ifdef PARPAR_INVERT_SUPPORT
	free(gf16_recip);
	gf16_recip = NULL;
#endif
}

HEDLEY_CONST uint16_t gf16_exp(uint_fast16_t v) {
	uint_fast32_t result = gf_exp[v>>3];
	result <<= (v&7);
	return result ^ gf_exp[8192 + (result>>16)];
	
	/* alternative idea which only omits bottom bit of gf_exp lookup, but avoids a second lookup
	// GCC doesn't handle the unpredictable check that well
	uint_fast32_t result0 = gf_exp[result>>1];
	uint_fast32_t result1 = (result0 << 1) ^ (-(result0 >> 15) & 0x1100B); // multiply by 2?
	return HEDLEY_UNPREDICTABLE(result & 1) ? result1 : result0;
	*/
}

HEDLEY_CONST uint16_t gfmat_input_log(uint_fast16_t inputBlock) {
	return (inputBlock*2 + input_diff[inputBlock]);
}

HEDLEY_CONST uint16_t gfmat_coeff_log(uint_fast16_t inputLog, uint_fast16_t recoveryBlock) {
	//assert(recoveryBlock < 65535); // if ==65535, gets an invalid exponent
	
	// calculate POW(inputBlockConstant, recoveryBlock) in GF
	uint_fast32_t result = inputLog * recoveryBlock;
	// clever bit hack for 'result %= 65535' from MultiPar sources
	result = (result >> 16) + (result & 65535);
	result += result >> 16;
	
	return result;
}

HEDLEY_CONST uint16_t gfmat_coeff_from_log(uint_fast16_t inputLog, uint_fast16_t recoveryBlock) {
	return gf16_exp(gfmat_coeff_log(inputLog, recoveryBlock));
}
HEDLEY_CONST uint16_t gfmat_coeff(uint_fast16_t inputBlock, uint_fast16_t recoveryBlock) {
	return gfmat_coeff_from_log(gfmat_input_log(inputBlock), recoveryBlock);
}
