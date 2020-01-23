extern "C" {
#ifdef _OPENMP
#include <omp.h>
#endif
#include "../src/stdint.h"
#include <gf_complete.h>
#include <string.h>
#include <stdlib.h>
};

// memory alignment to 16-bytes for SSE operations (may grow for AVX operations)
int MEM_ALIGN = 16;
int MEM_WALIGN = 16;
int GF_METHOD = GF_MULT_DEFAULT, GF_METHOD_ARG1 = 0, GF_METHOD_ARG2 = 0;
int CHUNK_SIZE = 0;

// these lookup tables consume a hefty 192KB... oh well
uint16_t input_lookup[32768]; // logarithms of input constants
uint16_t gf_exp[65536]; // pre-calculated exponents in GF(2^16)
// TODO: consider using GF-Complete's antilog table instead of gf_exp
void ppgf_init_constants() {
	int exp = 0, n = 1, i;
	for (i = 0; i < 32768; i++) {
		do {
			gf_exp[exp] = n;
			exp++; // exp will reach 65534 by the end of the loop
			n <<= 1;
			if(n > 65535) n ^= 0x1100B;
		} while( !(exp%3) || !(exp%5) || !(exp%17) || !(exp%257) );
		input_lookup[i] = exp;
	}
	gf_exp[exp] = n;
	gf_exp[65535] = gf_exp[0];
}

static inline uint16_t calc_factor(uint_fast16_t inputBlock, uint_fast16_t recoveryBlock) {
	// calculate POW(inputBlockConstant, recoveryBlock) in GF
	uint_fast32_t result = input_lookup[inputBlock] * recoveryBlock;
	// clever bit hack for 'result %= 65535' from MultiPar sources
	result = (result >> 16) + (result & 65535);
	result = (result >> 16) + (result & 65535);
	
	return gf_exp[result];
}

gf_t* gf = NULL;
size_t size_hint = 0;
int gf_method_wordsize = 0;
int gfCount = 0;
int using_altmap = 0;

static inline void init_gf(gf_t* gf) {
	gf_init_hard(gf, 16, GF_METHOD, GF_REGION_ALTMAP, GF_DIVIDE_DEFAULT, 0, GF_METHOD_ARG1, GF_METHOD_ARG2, size_hint, 0, gf_method_wordsize, NULL, NULL);
}

#ifdef _OPENMP
int maxNumThreads = 1, defaultNumThreads = 1;
static void alloc_gf() {
	int i;
	if(gfCount == maxNumThreads) return;
	if(gfCount < maxNumThreads) {
		// allocate more
		gf = (gf_t*)realloc(gf, sizeof(gf_t) * maxNumThreads);
		for(i=gfCount; i<maxNumThreads; i++) {
			init_gf(&gf[i]);
		}
	} else {
		// free stuff
		for(i=gfCount; i>maxNumThreads; i--) {
			gf_free(&gf[i-1], 1);
		}
		gf = (gf_t*)realloc(gf, sizeof(gf_t) * maxNumThreads);
	}
	gfCount = maxNumThreads;
}
#endif

void ppgf_omp_check_num_threads() {
#ifdef _OPENMP
	int max_threads = omp_get_max_threads();
	if(max_threads != maxNumThreads)
		// handle the possibility that some other module changes this
		omp_set_num_threads(maxNumThreads);
#endif
}

static void setup_gf() {
#ifdef _OPENMP
	// firstly, deallocate
	if(gf) {
		int i;
		for(i=0; i<gfCount; i++) {
			gf_free(&gf[i], 1);
		}
		free(gf);
		gfCount = 0;
		gf = NULL;
	}
	
	// then realloc
	alloc_gf();
#else
	if(gf) {
		gf_free(gf, 1);
	} else {
		gf = (gf_t*)malloc(sizeof(gf_t));
	}
	init_gf(gf);
#endif
	
	MEM_ALIGN = gf[0].alignment;
	MEM_WALIGN = gf[0].walignment;
	using_altmap = gf[0].using_altmap ? 1 : 0;
	
	// select a good chunk size
	// TODO: this needs to be variable depending on the CPU cache size
	// although these defaults are pretty good across most CPUs
	if(!CHUNK_SIZE) {
		unsigned int minChunkTarget;
		switch(gf[0].mult_method) {
			case GF_XOR_JIT_SSE2: /* JIT is a little slow, so larger blocks make things faster */
			case GF_XOR_JIT_AVX2:
			case GF_XOR_JIT_AVX512:
				CHUNK_SIZE = 128*1024; // half L2 cache?
				minChunkTarget = 96*1024; // keep in range 96-192KB
				break;
			case GF_SPLIT8:
			case GF_XOR_SSE2:
				CHUNK_SIZE = 96*1024; // 2* L1 data cache size ?
				minChunkTarget = 64*1024; // keep in range 64-128KB
				break;
			default: // SPLIT4
				CHUNK_SIZE = 48*1024; // ~=L1 * 1-2 data cache size seems to be efficient
				minChunkTarget = 32*1024; // keep in range 32-64KB
				break;
		}
		
		if(size_hint) {
			/* try to keep in range */
			unsigned int numChunks = (size_hint / CHUNK_SIZE) + ((size_hint % CHUNK_SIZE) ? 1 : 0);
			if(size_hint / numChunks < minChunkTarget) {
				CHUNK_SIZE = size_hint / (numChunks-1) + 1;
			}
		}
	}
}
void ppgf_maybe_setup_gf() {
	if(!gf) setup_gf();
}


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))

// performs multiple multiplies for a region, using threads
// note that inputs will get trashed
/* REQUIRES:
   - input and each pointer in outputs must be aligned to 2 bytes
   - len must be a multiple of two (duh)
   - input and length of each output is the same and == len
   - number of outputs and scales is same and == numOutputs
*/
void ppgf_multiply_mat(uint16_t** inputs, uint_fast16_t* iNums, unsigned int numInputs, size_t len, uint16_t** outputs, uint_fast16_t* oNums, unsigned int numOutputs, int add) {
	ppgf_omp_check_num_threads();
	
	/*
	if(using_altmap) {
		int out;
		#pragma omp parallel for
		for(out = 0; out < (int)numOutputs; out++) {
			unsigned int in = 0;
			for(; in < numInputs-3; in+=4) {
				gf_val_32_t inNum4[4] = {
					calc_factor(iNums[in], oNums[out]),
					calc_factor(iNums[in+1], oNums[out]),
					calc_factor(iNums[in+2], oNums[out]),
					calc_factor(iNums[in+3], oNums[out])
				};
				gf.multiply_regionX.w16(&gf, inputs + in, outputs[out], inNum4, (int)len, add || in>0);
			}
			for(; in < numInputs; in++)
				gf.multiply_region.w32(&gf, inputs[in], outputs[out], calc_factor(iNums[in], oNums[out]), (int)len, true);
		}
		return;
	}
	*/
	
	// break the slice into smaller chunks so that we maximise CPU cache usage
	int numChunks = (len / CHUNK_SIZE) + ((len % CHUNK_SIZE) ? 1 : 0);
	unsigned int alignMask = MEM_WALIGN-1;
	unsigned int chunkSize = (CEIL_DIV(len, numChunks) + alignMask) & ~alignMask; // we'll assume that input chunks are memory aligned here
	
	// avoid nested loop issues by combining chunk & output loop into one
	// the loop goes through outputs before chunks
	int loop = 0;
	#pragma omp parallel for
	for(loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
		size_t offset = (loop / numOutputs) * chunkSize;
		unsigned int out = loop % numOutputs;
		int procSize = MIN(len-offset, chunkSize);
#ifdef _OPENMP
		gf_t* _gf = &(gf[omp_get_thread_num()]);
#else
		gf_t* _gf = gf;
#endif
		// TODO: perhaps it makes sense to just use a statically allocated array instead and loop?
		gf_val_32_t* vals = (gf_val_32_t*)malloc(sizeof(gf_val_32_t) * numInputs);
		unsigned int i;
		for(i=0; i<numInputs; i++)
			vals[i] = calc_factor(iNums[i], oNums[out]);

		_gf->multiply_regionX.w16(_gf, numInputs, offset, (void**)inputs, outputs[out], vals, procSize, add);
		free(vals);
	}
	
#ifdef _OPENMP
	// if(max_threads != maxNumThreads)
		// omp_set_num_threads(max_threads);
#endif
}


void ppgf_prep_input(size_t destLen, size_t inputLen, char* dest, char* src) {
	if(!gf) setup_gf();
	if(using_altmap) {
		// ugly hack to deal with zero filling case
		size_t lenTail = inputLen & (MEM_WALIGN-1);
		if(inputLen < destLen && lenTail) {
			size_t lenMain = inputLen - lenTail;
			gf[0].altmap_region(src, lenMain, dest);
			// copy remaining, with zero fill, then ALTMAP over it
			memcpy(dest + lenMain, src + lenMain, lenTail);
			memset(dest + inputLen, 0, destLen - inputLen);
			if(lenMain + MEM_WALIGN <= destLen)
				gf[0].altmap_region(dest + lenMain, MEM_WALIGN, dest + lenMain);
			return;
		} else
			gf[0].altmap_region(src, inputLen, dest);
	} else
		memcpy(dest, src, inputLen);
	
	if(inputLen < destLen) // fill empty bytes with 0
		memset(dest + inputLen, 0, destLen - inputLen);
}
void ppgf_finish_input(unsigned int numInputs, uint16_t** inputs, size_t len) {
	if(using_altmap) {
		if(!gf) setup_gf();
		// TODO: multi-thread this?
		int in;
		for(in = 0; in < (int)numInputs; in++)
			gf[0].unaltmap_region(inputs[in], len, inputs[in]);
	}
}

enum {
	GF_METHOD_DEFAULT,
	GF_METHOD_LH_LOOKUP,
	GF_METHOD_XOR,
	GF_METHOD_SHUFFLE,
	GF_METHOD_AFFINE
};
void ppgf_get_method(int* rMethod, int* rWord, const char** rMethLong, int* walign) {
	switch(gf[0].mult_method) {
		case GF_SPLIT8:
			*rMethod = GF_METHOD_LH_LOOKUP;
			*rWord = FAST_U32_SIZE * 8;
			*rMethLong = "LH Lookup";
		break;
		case GF_SPLIT4:
			*rMethod = GF_METHOD_SHUFFLE;
			*rWord = 64;
			*rMethLong = "Split4 Lookup";
		break;
		case GF_SPLIT4_NEON:
			*rMethod = GF_METHOD_SHUFFLE;
			#ifdef ARCH_AARCH64
				*rWord = 128;
			#else
				*rWord = 64;
			#endif
			*rMethLong = "Shuffle";
		break;
		case GF_SPLIT4_SSSE3:
			*rMethod = GF_METHOD_SHUFFLE;
			*rWord = 128;
			*rMethLong = "Shuffle";
		break;
		case GF_SPLIT4_AVX2:
			*rMethod = GF_METHOD_SHUFFLE;
			*rWord = 256;
			*rMethLong = "Shuffle";
		break;
		case GF_SPLIT4_AVX512:
			*rMethod = GF_METHOD_SHUFFLE;
			*rWord = 512;
			*rMethLong = "Shuffle";
		break;
		case GF_XOR_SSE2:
			*rMethod = GF_METHOD_XOR;
			*rWord = 128;
			*rMethLong = "XOR";
		break;
		case GF_XOR_JIT_SSE2:
			*rMethod = GF_METHOD_XOR;
			*rWord = 128;
			*rMethLong = "XOR JIT";
		break;
		case GF_XOR_JIT_AVX2:
			*rMethod = GF_METHOD_XOR;
			*rWord = 256;
			*rMethLong = "XOR JIT";
		break;
		case GF_XOR_JIT_AVX512:
			*rMethod = GF_METHOD_XOR;
			*rWord = 512;
			*rMethLong = "XOR JIT";
		break;
		case GF_AFFINE_GFNI:
			*rMethod = GF_METHOD_AFFINE;
			*rWord = 128;
			*rMethLong = "Affine";
		break;
		case GF_AFFINE_AVX512:
			*rMethod = GF_METHOD_AFFINE;
			*rWord = 512;
			*rMethLong = "Affine";
		break;
		default:
			*rMethod = 0;
			*rWord = 0;
			*rMethLong = "???";
	}
	*walign = MEM_WALIGN;
}

int ppgf_get_num_threads() {
#ifdef _OPENMP
	return gf ? gfCount : maxNumThreads;
#else
	return 1;
#endif
}
void ppgf_set_num_threads(int threads) {
#ifdef _OPENMP
	maxNumThreads = threads;
	if(maxNumThreads < 1) maxNumThreads = defaultNumThreads;
	
	ppgf_maybe_setup_gf();
	alloc_gf();
#endif
}
void ppgf_init_gf_module() {
	GF_METHOD = GF_MULT_DEFAULT;
	GF_METHOD_ARG1 = 0;
	GF_METHOD_ARG2 = 0;
	CHUNK_SIZE = 0;
	
#ifdef _OPENMP
	maxNumThreads = omp_get_num_procs();
	if(maxNumThreads < 1) maxNumThreads = 1;
	defaultNumThreads = maxNumThreads;
#endif
}

int ppgf_set_method(int meth, int ws, int sh) {
	GF_METHOD_ARG1 = 0;
	GF_METHOD_ARG2 = 0;
	switch(meth) {
		case GF_METHOD_DEFAULT:
			GF_METHOD = GF_MULT_DEFAULT;
		break;
		case GF_METHOD_LH_LOOKUP:
			GF_METHOD = GF_MULT_SPLIT_TABLE;
			GF_METHOD_ARG1 = 16;
			GF_METHOD_ARG2 = 8;
		break;
		case GF_METHOD_XOR:
			GF_METHOD = GF_MULT_XOR_DEPENDS;
		break;
		case GF_METHOD_SHUFFLE:
			GF_METHOD = GF_MULT_SPLIT_TABLE;
			GF_METHOD_ARG1 = 16;
			GF_METHOD_ARG2 = 4;
		break;
		case GF_METHOD_AFFINE:
			GF_METHOD = GF_MULT_AFFINE;
		break;
		default:
			return 1;
	}
	
	gf_method_wordsize = ws;
	size_hint = sh;
	
	setup_gf();
	return 0;
}
