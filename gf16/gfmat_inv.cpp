#include "gfmat_coeff.h"
#include "gfmat_inv.h"
#include "gf16pmul.h"
#include <algorithm>

#ifdef PARPAR_INVERT_SUPPORT
extern "C" uint16_t* gf16_recip;

#include <cassert>
#include "../src/platform.h" // for ALIGN_*
#include "gf16mul.h"

template<int rows>
int Galois16RecMatrix::processRow(unsigned rec, unsigned validCount, unsigned invalidCount, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs) {
	unsigned missingCol = validCount + rec;
	
	uint16_t baseCoeff;
	uint16_t coeff[rows];
	
	unsigned sw16 = stripeWidth / sizeof(uint16_t);
	// TODO: consider optimisation for numStripes == 1 ?
	
	
	#define MAT_ROW(s, r) (mat + (((s)*invalidCount) + (r)) * sw16)
	#define REPLACE_WORD(r, c, v) gf.replace_word(MAT_ROW((c)/sw16, r), (c)%sw16, v)
	
	void* srcRows[rows];
	srcRows[0] = MAT_ROW(0, rec);
	for(unsigned i=1; i<rows; i++)
		srcRows[i] = (uint8_t*)srcRows[0] + i * stripeWidth;
	
	
	#define SCALE_ROW(row) \
		baseCoeff = REPLACE_WORD(rec+row, missingCol+row, 1); \
		if(HEDLEY_UNLIKELY(baseCoeff == 0)) /* bad recovery coeff */ \
			return row; \
		if(HEDLEY_LIKELY(baseCoeff != 1)) { \
			for(unsigned stripe=0; stripe<numStripes; stripe++) \
				gf.mul(MAT_ROW(stripe, rec+row), MAT_ROW(stripe, rec+row), stripeWidth, gf16_recip[baseCoeff], gfScratch); \
		} void(0)
	// TODO: consider prefetching reciprocal?
	#define MULADD_ROW(rowDst, rowSrc) \
		coeff[0] = REPLACE_WORD(rowDst, missingCol+rowSrc, 0); \
		if(HEDLEY_LIKELY(coeff[0] != 0)) { \
			for(unsigned stripe=0; stripe<numStripes; stripe++) \
				gf.mul_add(MAT_ROW(stripe, rowDst), MAT_ROW(stripe, rec+rowSrc), stripeWidth, coeff[0], gfScratch); \
		} void(0)
	// TODO: is a coefficient of 0 ever correct?
	#define MULADD_ROW_PF(rowDst, rowSrc, rowPf) \
		coeff[0] = REPLACE_WORD(rowDst, missingCol+rowSrc, 0); \
		if(HEDLEY_LIKELY(coeff[0] != 0)) { \
			for(unsigned stripe=0; stripe<numStripes; stripe++) \
				gf.mul_add_pf(MAT_ROW(stripe, rowDst), MAT_ROW(stripe, rec+rowSrc), stripeWidth, coeff[0], gfScratch, (uint8_t*)(rowPf) + stripe*stripeWidth); \
		} void(0)
	#define MULADD_MULTI_ROW(rowDst, srcOffs, numRows) \
		for(unsigned i=0; i<numRows; i++) \
			coeff[i] = REPLACE_WORD(rowDst, missingCol+srcOffs+i, 0); \
		for(unsigned stripe=0; stripe<numStripes; stripe++) \
			gf.mul_add_multi(numRows, stripeWidth*stripe, MAT_ROW(0, rowDst), srcRows+srcOffs, stripeWidth, coeff, gfScratch)
	#define MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, rowPf) \
		for(unsigned i=0; i<numRows; i++) \
			coeff[i] = REPLACE_WORD(rowDst, missingCol+srcOffs+i, 0); \
		for(unsigned stripe=0; stripe<numStripes; stripe++) \
			gf.mul_add_multi_stridepf(numRows, stripeWidth, MAT_ROW(stripe, rowDst), MAT_ROW(stripe, rec+srcOffs), stripeWidth, coeff, gfScratch, (uint8_t*)(rowPf) + stripe*stripeWidth)
	
	#define MULADD_LASTROW(rowDst, rowSrc) \
		if(HEDLEY_LIKELY(recFirst < invalidCount)) { \
			MULADD_ROW_PF(rowDst, rowSrc, MAT_ROW(0, recFirst)); \
		} else { \
			if(nextScaleRow) { \
				MULADD_ROW_PF(rowDst, rowSrc, nextScaleRow); \
			} else { \
				MULADD_ROW(rowDst, rowSrc); \
			} \
			return -1; \
		}
	#define MULADD_MULTI_LASTROW(rowDst, srcOffs, numRows) \
		if(HEDLEY_LIKELY(recFirst < invalidCount)) { \
			MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, MAT_ROW(0, recFirst)); \
		} else { \
			if(nextScaleRow) { \
				MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, nextScaleRow); \
			} else { \
				MULADD_MULTI_ROW(rowDst, srcOffs, numRows); \
			} \
			return -1; \
		}
	
	unsigned recFirst = rec == 0 ? rows : 0;
	// the next row when `processRow` is called; last action will prefetch this row
	uint16_t* nextScaleRow = (rec+rows < invalidCount) ? MAT_ROW(0, rec+rows) : nullptr;
	
	// rescale the row
	SCALE_ROW(0);
	
	// if we're processing multiple source rows, run elimination on the source group first
	if(rows >= 2) {
		// multiply-add to the next row
		MULADD_ROW(rec+1, 0);
		// scale it, and multiply-add back
		SCALE_ROW(1);
		if(rows > 2) {
			MULADD_ROW_PF(rec+0, 1, srcRows[2]);
		} else MULADD_LASTROW(rec+0, 1)
	} else {
		if(recFirst >= invalidCount)
			return -1;
	}
	if(rows >= 3) {
		if(rows >= 4) {
			MULADD_MULTI_ROW_PF(rec+2, 0, 2, srcRows[3]);
			SCALE_ROW(2);
			MULADD_MULTI_ROW(rec+3, 0, 2);
			MULADD_ROW(rec+3, 2);
			SCALE_ROW(3);
			MULADD_ROW(rec+2, 3);
			MULADD_MULTI_ROW(rec+0, 2, 2);
			if(rows > 4) {
				MULADD_MULTI_ROW_PF(rec+1, 2, 2, srcRows[4]);
			} else MULADD_MULTI_LASTROW(rec+1, 2, 2)
		} else {
			MULADD_MULTI_ROW(rec+2, 0, 2);
			SCALE_ROW(2);
			MULADD_ROW(rec+0, 2);
			MULADD_LASTROW(rec+1, 2)
		}
	}
	if(rows >= 5) {
		if(rows >= 6) {
			MULADD_MULTI_ROW_PF(rec+4, 0, 4, srcRows[5]);
			SCALE_ROW(4);
			MULADD_MULTI_ROW(rec+5, 0, 4);
			MULADD_ROW(rec+5, 4);
			SCALE_ROW(5);
			MULADD_ROW(rec+4, 5);
			for(unsigned r = 0; r < 3; r++) {
				MULADD_MULTI_ROW(rec+r, 4, 2);
			}
			MULADD_MULTI_LASTROW(rec+3, 4, 2)
		} else {
			MULADD_MULTI_ROW(rec+4, 0, 4);
			SCALE_ROW(4);
			for(unsigned r = 0; r < 3; r++) {
				MULADD_ROW(rec+r, 4);
			}
			MULADD_LASTROW(rec+3, 4)
		}
	}
	
	// do main elimination, using the source group
	// first, gather all relevant coefficients
	for(unsigned r=0; r<invalidCount; r++) {
		if(HEDLEY_UNLIKELY(r == rec)) {
			r += rows-1;
		} else {
			for(unsigned c=0; c<rows; c++)
				rowCoeffs[r*rows + c] = REPLACE_WORD(r, missingCol+c, 0);
		}
	}
	for(unsigned stripe=0; stripe<numStripes; stripe++) {
		for(unsigned rec2=recFirst; rec2<invalidCount; ) {
			unsigned curRec2 = rec2++;
			if(HEDLEY_UNLIKELY(rec2 == rec))
				rec2 += rows;
			
			void* pf = nextScaleRow;
			if(HEDLEY_LIKELY(rec2 < invalidCount)) {
				pf = MAT_ROW(stripe, rec2);
			} else if(stripe < numStripes-1) {
				pf = MAT_ROW(stripe+1, recFirst);
			}
			
			if(rows > 1) {
				if(HEDLEY_LIKELY(pf))
					gf.mul_add_multi_stridepf(rows, stripeWidth, MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, rowCoeffs + curRec2*rows, gfScratch, pf);
				else
					gf.mul_add_multi(rows, stripeWidth*stripe, MAT_ROW(0, curRec2), srcRows, stripeWidth, rowCoeffs + curRec2*rows, gfScratch);
			} else {
				if(HEDLEY_LIKELY(pf))
					gf.mul_add_pf(MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, rowCoeffs[curRec2], gfScratch, pf);
				else
					gf.mul_add(MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, rowCoeffs[curRec2], gfScratch);
			}
		}
	}
	
	return -1;
	
	#undef MAT_ROW
	#undef REPLACE_WORD
	#undef SCALE_ROW
	#undef MULADD_ROW
	#undef MULADD_ROW_PF
	#undef MULADD_MULTI_ROW
	#undef MULADD_MULTI_ROW_PF
	#undef MULADD_LASTROW
	#undef MULADD_MULTI_LASTROW
}


// construct initial matrix (pre-inversion)
void Galois16RecMatrix::Construct(const std::vector<bool>& inputValid, unsigned validCount, const std::vector<uint16_t>& recovery) {
	unsigned validCol = 0;
	unsigned missingCol = validCount;
	unsigned recStart = 0;
	unsigned sw16 = stripeWidth/sizeof(uint16_t);
	unsigned invalidCount = inputValid.size() - validCount;
	if(recovery.at(0) == 0) { // first recovery having exponent 0 is a common case
		for(unsigned stripe=0; stripe<numStripes; stripe++) {
			for(unsigned i=0; i<sw16; i++)
				mat[stripe * invalidCount*sw16 + i] = 1;
		}
		recStart++;
	}
	if(recStart >= recovery.size()) return;
	
	
	unsigned input = 0;
	const unsigned GROUP_AMOUNT = 4;
	#define CONSTRUCT_VIA_EXP(loopcond) \
		for(; input + GROUP_AMOUNT <= inputValid.size(); input+=GROUP_AMOUNT) { \
			uint16_t inputLog[GROUP_AMOUNT]; \
			unsigned targetCol[GROUP_AMOUNT]; \
			for(unsigned i=0; i<GROUP_AMOUNT; i++) { \
				inputLog[i] = gfmat_input_log(input+i); \
				targetCol[i] = inputValid.at(input+i) ? validCol++ : missingCol++; \
				targetCol[i] = (targetCol[i]/sw16)*sw16*invalidCount + (targetCol[i]%sw16); \
			} \
			for(loopcond) { \
				uint16_t exp = recovery.at(rec); \
				for(unsigned i=0; i<GROUP_AMOUNT; i++) { \
					mat[rec * sw16 + targetCol[i]] = gfmat_coeff_from_log(inputLog[i], exp); \
				} \
			} \
		} \
		for(; input < inputValid.size(); input++) { \
			uint16_t inputLog = gfmat_input_log(input); \
			unsigned targetCol = inputValid.at(input) ? validCol++ : missingCol++; \
			targetCol = (targetCol/sw16)*sw16*invalidCount + (targetCol%sw16); \
			for(loopcond) { \
				mat[rec * sw16 + targetCol] = gfmat_coeff_from_log(inputLog, recovery.at(rec)); \
			} \
		} \
		assert(validCol == validCount)
	// TODO: zerofill padding for good measure
	
	if(recovery.at(recStart) == 1) {
		bool canUseFastMul = false;
		if(gf16pmul) {
			// these shouldn't fail, but just in case, check alignments
			// blocklen is assumed to be a multiple of alignment
			assert(gf16pmul_blocklen % gf16pmul_alignment == 0);
			canUseFastMul = (stripeWidth % gf16pmul_blocklen == 0) && ((uintptr_t)mat % gf16pmul_alignment == 0);
		}
		
		if(canUseFastMul) {
			// there's a good chance that we have a mostly sequential sequence of recovery blocks
			// check this by looking for gaps in the sequence
			std::vector<uint16_t> recSkips;
			recSkips.reserve(invalidCount);
			recSkips.push_back(recStart);
			unsigned maxSkips = invalidCount/2; // TODO: tune threshold
			uint16_t lastExp = 1;
			for(unsigned rec = recStart+1; rec < invalidCount; rec++) {
				uint16_t exp = recovery.at(rec);
				if(exp != lastExp+1) {
					recSkips.push_back(rec);
					if(recSkips.size() >= maxSkips) break;
				}
				lastExp = exp;
			}
			
			if(recSkips.size() < maxSkips) {
				// not many gaps - use the strategy of filling these gaps first...
				CONSTRUCT_VIA_EXP(uint16_t rec : recSkips);
				
				// ...then compute most of the rows via multiplication
				for(unsigned stripe=0; stripe<numStripes; stripe++) {
					lastExp = 1;
					uint16_t* matStripe = mat + stripe * invalidCount*sw16;
					uint16_t* src1 = matStripe + recStart * sw16;
					for(unsigned rec = recStart+1; rec < invalidCount; rec++) {
						uint16_t exp = recovery.at(rec);
						bool skip = (exp != lastExp+1);
						lastExp = exp;
						if(skip) continue;
						
						gf16pmul(matStripe + rec * sw16, src1, matStripe + (rec-1) * sw16, stripeWidth);
					}
				}
				
				return;
			}
		}
	}
	
	CONSTRUCT_VIA_EXP(unsigned rec = recStart; rec < invalidCount; rec++);
	#undef CONSTRUCT_VIA_EXP
}

#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))

bool Galois16RecMatrix::Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb) {
	unsigned invalidCount = inputValid.size() - validCount;
	assert(validCount < inputValid.size()); // i.e. invalidCount > 0
	assert(inputValid.size() <= 32768 && inputValid.size() > 0);
	assert(recovery.size() <= 65535 && recovery.size() > 0);
	
	if(invalidCount > recovery.size()) return false;
	
	
	unsigned matWidth = inputValid.size() * sizeof(uint16_t);
	Galois16Mul gf(Galois16Mul::default_method(matWidth, inputValid.size(), inputValid.size(), true));
	const auto gfInfo = gf.info();
	
	// divide the matrix up into evenly sized stripes (for loop tiling optimisation)
	numStripes = ROUND_DIV(matWidth, gfInfo.idealChunkSize);
	if(numStripes < 1) numStripes = 1;
	stripeWidth = gf.alignToStride(CEIL_DIV(matWidth, numStripes));
	numStripes = CEIL_DIV(matWidth, stripeWidth);
	assert(numStripes >= 1);
	
	void* gfScratch = gf.mutScratch_alloc();
	
	if(mat) ALIGN_FREE(mat);
	unsigned matSize = invalidCount * stripeWidth*numStripes;
	ALIGN_ALLOC(mat, matSize, gfInfo.alignment);
	
	uint16_t totalProgress = invalidCount + (gf.needPrepare() ? 3 : 1); // provision for prepare/finish/init-calc
	
	// easier to handle if exponents are in order
	std::sort(recovery.begin(), recovery.end());
	
	static bool pmulInit = false;
	if(!pmulInit) {
		pmulInit = true;
		setup_pmul();
	}
	
	invert_loop: { // loop, in the unlikely case we hit the PAR2 un-invertability flaw; TODO: is there a faster way than just retrying?
		if(invalidCount > recovery.size()) { // not enough recovery
			gf.mutScratch_free(gfScratch);
			ALIGN_FREE(mat);
			mat = nullptr;
			return false;
		}
		
		if(progressCb) progressCb(0, totalProgress);
		Construct(inputValid, validCount, recovery);
		
		// pre-transform
		uint16_t progressOffset = 1;
		if(gf.needPrepare()) {
			if(progressCb) progressCb(1, totalProgress);
			progressOffset = 2;
			
			gf.prepare(mat, mat, matSize);
		}
		
		// invert
		unsigned rec = 0;
		#define INVERT_GROUP(rows) \
			if(gfInfo.idealInputMultiple >= rows && invalidCount >= rows) { \
				for(; rec <= invalidCount-rows; rec+=rows) { \
					if(progressCb) progressCb(rec + progressOffset, totalProgress); \
					 \
					int badRowOffset = processRow<rows>(rec, validCount, invalidCount, gf, gfScratch, rowCoeffs); \
					if(badRowOffset >= 0) { \
						/* ignore this recovery row and try again */ \
						recovery.erase(recovery.begin() + rec + badRowOffset); \
						goto invert_loop; \
					} \
				} \
			}
		// max out at 6 groups (registers + cache assoc?)
		uint16_t* rowCoeffs = new uint16_t[invalidCount*6];
		INVERT_GROUP(6)
		INVERT_GROUP(5)
		INVERT_GROUP(4)
		INVERT_GROUP(3)
		INVERT_GROUP(2)
		INVERT_GROUP(1)
		delete[] rowCoeffs;
		#undef INVERT_GROUP
		
		// post transform
		if(gf.needPrepare()) {
			if(progressCb) progressCb(totalProgress-1, totalProgress);
			
			gf.finish(mat, matSize);
			// TODO: check for zeroes??
		}
	}
	
	// remove excess recovery
	recovery.resize(invalidCount);
	numRec = invalidCount;
	
	gf.mutScratch_free(gfScratch);
	return true;
}

Galois16RecMatrix::~Galois16RecMatrix() {
	if(mat) ALIGN_FREE(mat);
}

#endif
