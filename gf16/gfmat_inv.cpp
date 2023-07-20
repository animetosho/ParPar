#include "gfmat_coeff.h"
#include "gfmat_inv.h"
#include "gf16pmul.h"
#include <algorithm>

#ifdef PARPAR_INVERT_SUPPORT
extern "C" uint16_t* gf16_recip;

#include <cassert>
#include "../src/platform.h" // for ALIGN_*
#include "gf16mul.h"
#include "threadqueue.h"
#include <future>

static const unsigned MIN_THREAD_REC = 10; // minimum number of rows to process on a thread

class Galois16RecMatrixWorker {
	const Galois16Mul& gf;
public:
	MessageThread thread;
	void* gfScratch;
	
	explicit Galois16RecMatrixWorker(const Galois16Mul& _gf) : gf(_gf) {
		gfScratch = _gf.mutScratch_alloc();
	}
	Galois16RecMatrixWorker(Galois16RecMatrixWorker&& other) noexcept : gf(other.gf) {
		thread = std::move(other.thread);
		gfScratch = other.gfScratch;
		other.gfScratch = nullptr;
	}
	~Galois16RecMatrixWorker() {
		thread.end();
		if(gfScratch)
			gf.mutScratch_free(gfScratch);
	}
};

struct Galois16RecMatrixWorkerMessage {
	unsigned stripeStart, stripeEnd;
	unsigned recFirst, recLast;
	unsigned recSrc; unsigned recSrcCount; uint16_t* rowCoeffs; void** srcRows; Galois16Mul* gf; void* gfScratch;
	unsigned coeffWidth;
	void(Galois16RecMatrix::*fn)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, uint16_t*, unsigned, void**, Galois16Mul&, void*, const void*);
	Galois16RecMatrix* parent;
	std::atomic<int>* procRefs;
	std::promise<void>* done;
};

static void invert_worker(ThreadMessageQueue<void*>& q) {
	Galois16RecMatrixWorkerMessage* req;
	while((req = static_cast<Galois16RecMatrixWorkerMessage*>(q.pop())) != NULL) {
		(req->parent->*(req->fn))(req->stripeStart, req->stripeEnd, req->recFirst, req->recLast, req->recSrc, req->recSrcCount, req->rowCoeffs, req->coeffWidth, req->srcRows, *(req->gf), req->gfScratch, nullptr);
		if(req->procRefs->fetch_sub(1, std::memory_order_acq_rel) <= 1) {
			req->done->set_value();
		}
		delete req;
	}
}

#define MAT_ROW(s, r) (mat + (((s)*numRec) + (r)) * (stripeWidth / sizeof(uint16_t)))
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))

template<int rows>
void Galois16RecMatrix::invertLoop(unsigned stripeStart, unsigned stripeEnd, unsigned recFirst, unsigned recLast, unsigned recSrc, unsigned recSrcCount, uint16_t* rowCoeffs, unsigned coeffWidth, void** srcRows, Galois16Mul& gf, void* gfScratch, const void* nextPf) {
	assert(recSrcCount % rows == 0);
	for(unsigned stripe=stripeStart; stripe<stripeEnd; stripe++) {
		for(unsigned recI = 0; recI < recSrcCount; recI += rows) {
			unsigned rec = recI+recSrc;
			for(unsigned rec2=recFirst; rec2<recLast; ) {
				unsigned curRec2 = rec2++;
				if(HEDLEY_UNLIKELY(rec2 == rec))
					rec2 += rows;
				
				// TODO: fixup prefetching
				const void* pf = nextPf;
				if(HEDLEY_LIKELY(rec2 < recLast)) {
					pf = MAT_ROW(stripe, rec2);
				} else if(recI+rows < recSrcCount) {
					pf = nullptr; // TODO: same row group - no need to prefetch as it should be in cache?
				} else if(stripe < stripeEnd-1) {
					pf = MAT_ROW(stripe+1, recFirst);
					// TODO: need to prefetch next stripe's initial matrix?
				}
				
				uint16_t* coeffPtr = rowCoeffs + (curRec2-recFirst)*coeffWidth + recI;
				if(rows > 1) {
					if(HEDLEY_LIKELY(pf))
						gf.mul_add_multi_stridepf(rows, stripeWidth, MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, coeffPtr, gfScratch, pf);
					else
						gf.mul_add_multi(rows, stripeWidth*(numRec*stripe + recI), MAT_ROW(0, curRec2) - recI*stripeWidth/sizeof(uint16_t), srcRows, stripeWidth, coeffPtr, gfScratch);
				} else {
					if(HEDLEY_LIKELY(pf))
						gf.mul_add_pf(MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, *coeffPtr, gfScratch, pf);
					else
						gf.mul_add(MAT_ROW(stripe, curRec2), MAT_ROW(stripe, rec), stripeWidth, *coeffPtr, gfScratch);
				}
			}
		}
	}
}

#define REPLACE_WORD(r, c, v) gf.replace_word(MAT_ROW((c)/(stripeWidth / sizeof(uint16_t)), r), (c)%(stripeWidth / sizeof(uint16_t)), v)

template<int rows>
int Galois16RecMatrix::initScale(unsigned rec, unsigned validCount, unsigned recFirst, unsigned recLast, Galois16Mul& gf, void* gfScratch) {
	assert(recFirst <= recLast);
	
	unsigned missingCol = validCount + rec;
	
	uint16_t baseCoeff;
	uint16_t coeff[rows];
	
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
			gf.mul_add_multi(numRows, stripeWidth*numRec*stripe, MAT_ROW(0, rowDst), srcRows+srcOffs, stripeWidth, coeff, gfScratch)
	#define MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, rowPf) \
		for(unsigned i=0; i<numRows; i++) \
			coeff[i] = REPLACE_WORD(rowDst, missingCol+srcOffs+i, 0); \
		for(unsigned stripe=0; stripe<numStripes; stripe++) \
			gf.mul_add_multi_stridepf(numRows, stripeWidth, MAT_ROW(stripe, rowDst), MAT_ROW(stripe, rec+srcOffs), stripeWidth, coeff, gfScratch, (uint8_t*)(rowPf) + stripe*stripeWidth)
	
	#define MULADD_LASTROW(rowDst, rowSrc) \
		if(HEDLEY_LIKELY(recFirst < numRec)) { \
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
		if(HEDLEY_LIKELY(recFirst < numRec)) { \
			MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, MAT_ROW(0, recFirst)); \
		} else { \
			if(nextScaleRow) { \
				MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, nextScaleRow); \
			} else { \
				MULADD_MULTI_ROW(rowDst, srcOffs, numRows); \
			} \
			return -1; \
		}
	
	if(recFirst == rec) recFirst += rows;
	// the next row when `processRow` is called; last action will prefetch this row
	uint16_t* nextScaleRow = (rec+rows < recLast) ? MAT_ROW(0, rec+rows) : nullptr;
	
	// TODO: consider loop tiling this stuff; requires extracting a small matrix (rows*rows), and solving that, which means a scalar multiply is necessary
	
	// rescale the row
	SCALE_ROW(0);
	
	// if we're processing multiple source rows, run elimination on the source group first
	if(rows >= 2) {
		// multiply-add to the next row
		MULADD_ROW(rec+1, 0);
		// scale it, and multiply-add back
		SCALE_ROW(1);
		if(rows > 2) {
			MULADD_ROW_PF(rec+0, 1, MAT_ROW(0, 2));
		} else MULADD_LASTROW(rec+0, 1)
	} else {
		if(recFirst >= numRec)
			return -1;
	}
	if(rows >= 3) {
		if(rows >= 4) {
			MULADD_MULTI_ROW_PF(rec+2, 0, 2, MAT_ROW(0, 3));
			SCALE_ROW(2);
			MULADD_MULTI_ROW(rec+3, 0, 2);
			MULADD_ROW(rec+3, 2);
			SCALE_ROW(3);
			MULADD_ROW(rec+2, 3);
			MULADD_MULTI_ROW(rec+0, 2, 2);
			if(rows > 4) {
				MULADD_MULTI_ROW_PF(rec+1, 2, 2, MAT_ROW(0, 4));
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
			MULADD_MULTI_ROW_PF(rec+4, 0, 4, MAT_ROW(0, 5));
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
	return -1;
	#undef SCALE_ROW
	#undef MULADD_ROW
	#undef MULADD_ROW_PF
	#undef MULADD_MULTI_ROW
	#undef MULADD_MULTI_ROW_PF
	#undef MULADD_LASTROW
	#undef MULADD_MULTI_LASTROW
}

void Galois16RecMatrix::fillCoeffs(uint16_t* rowCoeffs, unsigned rows, unsigned validCount, unsigned recFirst, unsigned recLast, unsigned rec, unsigned coeffWidth, Galois16Mul& gf) {
	unsigned missingCol = validCount + rec;
	if(recFirst == rec) recFirst += rows;
	for(unsigned r=recFirst; r<recLast; r++) {
		if(HEDLEY_UNLIKELY(r == rec)) {
			r += rows-1;
		} else {
			for(unsigned c=0; c<rows; c++)
				rowCoeffs[(r-recFirst)*coeffWidth + c] = REPLACE_WORD(r, missingCol+c, 0);
		}
	}
}

template<int rows>
void Galois16RecMatrix::processRow(unsigned rec, unsigned recCount, unsigned recFirst, unsigned recLast, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs, unsigned coeffWidth, std::vector<Galois16RecMatrixWorker>& workers) {
	// TODO: consider optimisation for numStripes == 1 ?
	
	assert(recFirst <= recLast);
	
	void* srcRows[rows];
	srcRows[0] = MAT_ROW(0, rec);
	for(unsigned i=1; i<rows; i++)
		srcRows[i] = (uint8_t*)srcRows[0] + i * stripeWidth;
	
	if(recFirst == rec) recFirst += rows;
	// the next row when `processRow` is called; last action will prefetch this row
	uint16_t* nextScaleRow = (rec+rows < recLast) ? MAT_ROW(0, rec+rows) : nullptr;
	
	if(recFirst >= recLast) return;
	
	// do main elimination, using the source group
	if(workers.empty())
		// process elimination directly
		invertLoop<rows>(0, numStripes, recFirst, recLast, rec, recCount, rowCoeffs, coeffWidth, srcRows, gf, gfScratch, nextScaleRow);
	else {
		// process using workers
		std::atomic<int> procRefs;
		std::promise<void> done;
		auto makeReq = [&, this]() -> Galois16RecMatrixWorkerMessage* {
			auto* req = new Galois16RecMatrixWorkerMessage;
			req->recFirst = recFirst;
			req->recLast = recLast;
			req->recSrc = rec;
			req->recSrcCount = recCount;
			req->rowCoeffs = rowCoeffs;
			req->srcRows = srcRows;
			req->gf = &gf;
			req->coeffWidth = coeffWidth;
			req->fn = &Galois16RecMatrix::invertLoop<rows>;
			req->parent = this;
			req->procRefs = &procRefs;
			req->done = &done;
			return req;
		};
		if(numStripes >= workers.size()) { // split full stripes across workers
			float stripesPerWorker = (float)numStripes / workers.size();
			float stripe = 0.5;
			procRefs.store(workers.size());
			for(auto& worker : workers) {
				auto* req = makeReq();
				req->stripeStart = (unsigned)stripe;
				req->stripeEnd = (unsigned)(stripe + stripesPerWorker);
				req->gfScratch = worker.gfScratch;
				worker.thread.send(req);
				stripe += stripesPerWorker;
			}
		} else { // each stripe may need >1 worker
			std::vector<Galois16RecMatrixWorkerMessage*> reqs;
			reqs.reserve(workers.size());
			float workersPerStripe = (float)workers.size() / numStripes;
			float workerCnt = 0.5;
			for(unsigned stripe=0; stripe<numStripes; stripe++) {
				unsigned workerNum = (unsigned)(workerCnt + workersPerStripe) - (unsigned)workerCnt;
				unsigned numRows = recLast - recFirst;
				if(rec >= recFirst && rec < recLast) numRows -= rows;
				numRows = CEIL_DIV(numRows, workerNum);
				if(numRows < MIN_THREAD_REC) numRows = MIN_THREAD_REC; // ensure workers have a half decent amount of stuff to do
				unsigned rowPos = recFirst;
				
				while(rowPos < recLast) {
					unsigned sendRows = numRows;
					if(rowPos+sendRows > rec && rowPos <= rec)
						// need to send extra to compensate for the gap
						sendRows += rows;
					if(rowPos+sendRows > recLast)
						sendRows = recLast - rowPos;
					
					auto* req = makeReq();
					req->stripeStart = stripe;
					req->stripeEnd = stripe+1;
					req->recFirst = rowPos;
					req->recLast = rowPos+sendRows;
					req->rowCoeffs += (rowPos-recFirst) * coeffWidth;
					reqs.push_back(req);
					
					rowPos += sendRows;
					if(rowPos == rec) rowPos += rows;
				}
				
				workerCnt += workersPerStripe;
			}
			assert(reqs.size() <= workers.size());
			procRefs.store(reqs.size());
			assert(procRefs > 0);
			
			for(unsigned i=0; i<reqs.size(); i++) {
				auto& worker = workers[i];
				auto* req = reqs[i];
				req->gfScratch = worker.gfScratch;
				worker.thread.send(req);
			}
		}
		
		// wait for threads to finish
		done.get_future().wait();
	}
}
#undef REPLACE_WORD
#undef MAT_ROW


template<int rows>
int Galois16RecMatrix::processRows(unsigned& rec, unsigned rowGroupSize, unsigned validCount, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs, std::vector<Galois16RecMatrixWorker>& workers, std::function<void(uint16_t, uint16_t)> progressCb, uint16_t progressOffset, uint16_t totalProgress) {
	unsigned alignedRowGroupSize = (rowGroupSize / rows) * rows;
	while(rec <= numRec-rows) {
		
		unsigned curRowGroupSize = alignedRowGroupSize;
		if(numRec-rec < curRowGroupSize) {
			curRowGroupSize = numRec-rec;
			curRowGroupSize -= curRowGroupSize % rows;
		}
		assert(curRowGroupSize > 0);
		
		unsigned recStart = rec;
		// for progress indicator, we'll even it out by computing a ratio to advance by
		unsigned progressRatio = (curRowGroupSize<<16)/numRec;
		unsigned progressBase = recStart + progressOffset;
		
		// loop through this row group (normalize values)
		for(; rec < curRowGroupSize+recStart; rec+=rows) {
			if(progressCb) progressCb(progressBase + (((rec-recStart)*progressRatio+32768)>>16), totalProgress);
			int badRowOffset = initScale<rows>(rec, validCount, recStart, curRowGroupSize+recStart, gf, gfScratch);
			if(badRowOffset >= 0) return rec+badRowOffset;
			fillCoeffs(rowCoeffs, rows, validCount, recStart, curRowGroupSize+recStart, rec, rows, gf);
			processRow<rows>(rec, rows, recStart, curRowGroupSize+recStart, gf, gfScratch, rowCoeffs, rows, workers);
		}
		
		
		// apply current row group to all other row groups
		for(unsigned recGroup=0; recGroup<numRec; ) {
			if(recGroup == recStart) {
				recGroup += curRowGroupSize;
				continue;
			}
			if(progressCb) {
				unsigned progressPos = recGroup;
				if(recGroup < recStart) progressPos += curRowGroupSize;
				progressCb(progressBase + ((progressPos*progressRatio+32768)>>16), totalProgress);
			}
			
			unsigned curRowGroupSize2 = rowGroupSize;
			if(numRec-recGroup < curRowGroupSize2)
				curRowGroupSize2 = numRec-recGroup;
			if(recGroup < recStart && recGroup+curRowGroupSize2 > recStart)
				curRowGroupSize2 = recStart-recGroup; // don't let this group cross into the normalized group
			fillCoeffs(rowCoeffs, curRowGroupSize, validCount, recGroup, recGroup+curRowGroupSize2, recStart, curRowGroupSize, gf);
			processRow<rows>(recStart, curRowGroupSize, recGroup, recGroup+curRowGroupSize2, gf, gfScratch, rowCoeffs, curRowGroupSize, workers);
			recGroup += curRowGroupSize2;
		}
	}
	return -1;
}



// construct initial matrix (pre-inversion)
void Galois16RecMatrix::Construct(const std::vector<bool>& inputValid, unsigned validCount, const std::vector<uint16_t>& recovery) {
	unsigned validCol = 0;
	unsigned missingCol = validCount;
	unsigned recStart = 0;
	unsigned sw16 = stripeWidth/sizeof(uint16_t);
	if(recovery.at(0) == 0) { // first recovery having exponent 0 is a common case
		for(unsigned stripe=0; stripe<numStripes; stripe++) {
			for(unsigned i=0; i<sw16; i++)
				mat[stripe * numRec*sw16 + i] = 1;
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
				targetCol[i] = (targetCol[i]/sw16)*sw16*numRec + (targetCol[i]%sw16); \
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
			targetCol = (targetCol/sw16)*sw16*numRec + (targetCol%sw16); \
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
			recSkips.reserve(numRec);
			recSkips.push_back(recStart);
			unsigned maxSkips = numRec/2; // TODO: tune threshold
			uint16_t lastExp = 1;
			for(unsigned rec = recStart+1; rec < numRec; rec++) {
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
					uint16_t* matStripe = mat + stripe * numRec*sw16;
					uint16_t* src1 = matStripe + recStart * sw16;
					for(unsigned rec = recStart+1; rec < numRec; rec++) {
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
	
	CONSTRUCT_VIA_EXP(unsigned rec = recStart; rec < numRec; rec++);
	#undef CONSTRUCT_VIA_EXP
}

bool Galois16RecMatrix::Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb) {
	numRec = inputValid.size() - validCount;
	assert(validCount < inputValid.size()); // i.e. numRec > 0
	assert(inputValid.size() <= 32768 && inputValid.size() > 0);
	assert(recovery.size() <= 65535 && recovery.size() > 0);
	
	if(numRec > recovery.size()) return false;
	
	
	unsigned matWidth = inputValid.size() * sizeof(uint16_t);
	Galois16Mul gf(Galois16Mul::default_method(matWidth, inputValid.size(), inputValid.size(), true));
	const auto gfInfo = gf.info();
	
	// divide the matrix up into evenly sized stripes (for loop tiling optimisation)
	numStripes = ROUND_DIV(matWidth, gfInfo.idealChunkSize);
	if(numStripes < 1) numStripes = 1;
	stripeWidth = gf.alignToStride(CEIL_DIV(matWidth, numStripes));
	numStripes = CEIL_DIV(matWidth, stripeWidth);
	assert(numStripes >= 1);
	
	
	if(mat) ALIGN_FREE(mat);
	unsigned matSize = numRec * stripeWidth*numStripes;
	ALIGN_ALLOC(mat, matSize, gfInfo.alignment);
	
	uint16_t totalProgress = numRec + (gf.needPrepare() ? 3 : 1); // provision for prepare/finish/init-calc
	
	// easier to handle if exponents are in order
	std::sort(recovery.begin(), recovery.end());
	
	static bool pmulInit = false;
	if(!pmulInit) {
		pmulInit = true;
		setup_pmul();
	}
	
	std::vector<Galois16RecMatrixWorker> workers;
	void* gfScratch;
	unsigned _numThreads = numThreads;
	if(numRec < MIN_THREAD_REC) _numThreads = 1; // don't spawn threads if not enough work
	if(_numThreads > 1) {
		workers.reserve(_numThreads);
		for(unsigned i=0; i<_numThreads; i++) {
			workers.emplace_back(gf);
			workers[i].thread.name = "gauss_worker";
			workers[i].thread.setCallback(invert_worker);
		}
		gfScratch = workers[0].gfScratch;
	} else
		gfScratch = gf.mutScratch_alloc();
	
	// target L3 slice? use 1MB target for now; TODO: improve this
	unsigned rowGroupSize = (1024*1024 / stripeWidth);
	// if it's going to be split amongst cores, increase the number of rows in a group
	if(numStripes < _numThreads) rowGroupSize *= _numThreads/numStripes;
	if(rowGroupSize < gfInfo.idealInputMultiple*2) rowGroupSize = gfInfo.idealInputMultiple*2;
	if(rowGroupSize > numRec) rowGroupSize = numRec;
	
	invert_loop: { // loop, in the unlikely case we hit the PAR2 un-invertability flaw; TODO: is there a faster way than just retrying?
		if(numRec > recovery.size()) { // not enough recovery
			if(_numThreads <= 1)
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
			if(gfInfo.idealInputMultiple >= rows && numRec >= rows) { \
				int badRow = processRows<rows>(rec, rowGroupSize, validCount, gf, gfScratch, rowCoeffs, workers, progressCb, progressOffset, totalProgress); \
				if(badRow >= 0) { \
					/* ignore this recovery row and try again */ \
					recovery.erase(recovery.begin() + badRow); \
					goto invert_loop; \
				} \
			}
		// max out at 6 groups (registers + cache assoc?)
		uint16_t* rowCoeffs = new uint16_t[rowGroupSize*rowGroupSize];
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
	recovery.resize(numRec);
	
	if(_numThreads <= 1)
		gf.mutScratch_free(gfScratch);
	return true;
}

Galois16RecMatrix::Galois16RecMatrix() : mat(nullptr) {
	numThreads = hardware_concurrency();
	if(numThreads > 4) numThreads = 4; // by default, cap at 4 threads, as scaling doesn't work so well; TODO: tweak this later
	numRec = 0;
	numStripes = 0;
	stripeWidth = 0;
}

Galois16RecMatrix::~Galois16RecMatrix() {
	if(mat) ALIGN_FREE(mat);
}

#endif
