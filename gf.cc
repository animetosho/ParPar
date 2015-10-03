
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>
//#include <inttypes.h>
#include <string.h>
#include <uv.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

extern "C" {
#ifdef _OPENMP
#include <omp.h>
#endif
#include <gf_complete.h>
}

// memory alignment to 16-bytes for SSE operations (may grow for AVX operations)
int MEM_ALIGN = 16, MEM_WALIGN = 16;
int CHUNK_SIZE = 32768;

#if defined(__cplusplus) && __cplusplus > 201100
	// C++11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#define ALIGN_ALLOC(buf, len) *(void**)&(buf) = aligned_alloc(MEM_ALIGN, ((len) + MEM_ALIGN-1) & ~(MEM_ALIGN-1))
	#define ALIGN_FREE free
#elif defined(_MSC_VER)
	#define ALIGN_ALLOC(buf, len) *(void**)&(buf) = _aligned_malloc((len), MEM_ALIGN)
	#define ALIGN_FREE _aligned_free
#else
	#define ALIGN_ALLOC(buf, len) if(posix_memalign((void**)&(buf), MEM_ALIGN, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


using namespace v8;

// these lookup tables consume a hefty 192KB... oh well
uint16_t input_lookup[32768]; // logarithms of input constants
uint16_t gf_exp[65536]; // pre-calculated exponents in GF(2^16)
static void init_constants() {
	int exp = 0, n = 1;
	for (int i = 0; i < 32768; i++) {
		do {
			gf_exp[exp] = n;
			exp++; // exp will reach 65536 by the end of the loop
			n <<= 1;
			if(n > 65535) n ^= 0x1100B;
		} while( !(exp%3) || !(exp%5) || !(exp%17) || !(exp%257) );
		input_lookup[i] = exp;
	}
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
int gfCount = 0;
bool using_altmap = false;

#ifdef _OPENMP
int maxNumThreads = 1, defaultNumThreads = 1;
static void alloc_gf() {
	if(gfCount == maxNumThreads) return;
	if(gfCount < maxNumThreads) {
		// allocate more
		gf = (gf_t*)realloc(gf, sizeof(gf_t) * maxNumThreads);
		for(int i=gfCount; i<maxNumThreads; i++) {
			gf_init_hard(&gf[i], 16, GF_MULT_DEFAULT, GF_REGION_ALTMAP, GF_DIVIDE_DEFAULT, 0, 0, 0, NULL, NULL);
		}
	} else {
		// free stuff
		for(int i=gfCount; i>maxNumThreads; i--) {
			gf_free(&gf[i], 1);
		}
		gf = (gf_t*)realloc(gf, sizeof(gf_t) * maxNumThreads);
	}
	gfCount = maxNumThreads;
}
#endif

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
static inline void multiply_mat(uint16_t** inputs, uint_fast16_t* iNums, unsigned int numInputs, size_t len, uint16_t** outputs, uint_fast16_t* oNums, unsigned int numOutputs, bool add) {
#ifdef _OPENMP
	int max_threads = omp_get_max_threads();
	if(max_threads != maxNumThreads)
		// handle the possibility that some other module changes this
		omp_set_num_threads(maxNumThreads);
#endif
	
	/*
	if(using_altmap) {
		#pragma omp parallel for
		for(int out = 0; out < (int)numOutputs; out++) {
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
	#pragma omp parallel for
	for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
		size_t offset = (loop / numOutputs) * chunkSize;
		unsigned int out = loop % numOutputs;
		int procSize = MIN(len-offset, chunkSize);
		offset /= sizeof(**outputs);
#ifdef _OPENMP
		gf_t* _gf = &(gf[omp_get_thread_num()]);
#else
		gf_t* _gf = gf;
#endif
		
		_gf->multiply_region.w32(_gf, inputs[0] + offset, outputs[out] + offset, calc_factor(iNums[0], oNums[out]), procSize, add);
		for(unsigned int in = 1; in < numInputs; in++) {
			_gf->multiply_region.w32(_gf, inputs[in] + offset, outputs[out] + offset, calc_factor(iNums[in], oNums[out]), procSize, true);
		}
	}
	
#ifdef _OPENMP
	// if(max_threads != maxNumThreads)
		// omp_set_num_threads(max_threads);
#endif
}

/*******************************************/

#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x
#define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
#define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HandleScope scope(isolate)

#define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)e))); return; }
#define RETURN_VAL(v) args.GetReturnValue().Set(v)
#define RETURN_UNDEF return;
#define ISOLATE isolate,

#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define FUNC_START HandleScope scope
#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_UNDEF RETURN_VAL( Undefined() );
#define ISOLATE

#endif

int mmActiveTasks = 0;

#ifdef _OPENMP
FUNC(SetMaxThreads) {
	FUNC_START;
	
	if (args.Length() < 1)
		RETURN_ERROR("Argument required");
	if (mmActiveTasks)
		RETURN_ERROR("Calculation already in progress");
	
	maxNumThreads = args[0]->ToInt32()->Value();
	if(maxNumThreads < 1) maxNumThreads = defaultNumThreads;
	
	alloc_gf();
	
	RETURN_UNDEF
}
#endif

void free_buffer(char* data, void* _size) {
#if !NODE_VERSION_AT_LEAST(0, 11, 0)
	int size = (int)(size_t)_size;
	V8::AdjustAmountOfExternalAllocatedMemory(-size);
#endif
	ALIGN_FREE(data);
}

FUNC(AlignedBuffer) {
	FUNC_START;
	
	if (args.Length() < 1)
		RETURN_ERROR("Argument required");
	
	size_t len;
	/*
	bool hasString = false;
	node::encoding enc = node::BINARY;
	if(args[0]->IsString()) {
		hasString = true;
		if(args.Length() >= 2)
			enc = node::ParseEncoding(ISOLATE args[1]->ToString());
		len = node::StringBytes::Size(ISOLATE args[0]->ToString(), enc);
	} else
	*/
		len = (size_t)args[0]->ToInteger()->Value();
	
	char* buf = NULL;
	ALIGN_ALLOC(buf, len);
	
	if(!buf)
		RETURN_ERROR("Out Of Memory");
	
	/*
	if(hasString)
		node::StringBytes::Write(ISOLATE buf, len, args[1]->ToString(), end);
	*/
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
	RETURN_VAL( node::Buffer::New(ISOLATE (char*)buf, len, free_buffer, (void*)len).ToLocalChecked() );
	#else
	RETURN_VAL( node::Buffer::New(ISOLATE (char*)buf, len, free_buffer, (void*)len) );
	#endif
#else
	RETURN_VAL(Local<Object>::New(
		node::Buffer::New((char*)buf, len, free_buffer, (void*)len)->handle_
	));
#endif
}

FUNC(PrepInput) {
	FUNC_START;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1]))
		RETURN_ERROR("Two Buffers required");
	
	size_t destLen = node::Buffer::Length(args[1]),
		inputLen = node::Buffer::Length(args[0]);
	char* dest = node::Buffer::Data(args[1]);
	char* src = node::Buffer::Data(args[0]);
	
	if((intptr_t)dest & (MEM_ALIGN-1))
		RETURN_ERROR("Destination not aligned");
	if(inputLen > destLen)
		RETURN_ERROR("Destination not large enough to hold input");
	
	if(using_altmap) {
		// ugly hack to deal with zero filling case
		int lenTail = inputLen & (MEM_WALIGN-1);
		if(inputLen < destLen && lenTail) {
			int lenMain = inputLen - lenTail;
			gf[0].altmap_region(src, lenMain, dest);
			// copy remaining, with zero fill, then ALTMAP over it
			memcpy(dest + lenMain, src + lenMain, lenTail);
			memset(dest + inputLen, 0, destLen - inputLen);
			gf[0].altmap_region(dest + lenMain, MEM_WALIGN, dest + lenMain);
			RETURN_UNDEF
		} else
			gf[0].altmap_region(src, inputLen, dest);
	} else
		memcpy(dest, src, inputLen);
	
	if(inputLen < destLen) // fill empty bytes with 0
		memset(dest + inputLen, 0, destLen - inputLen);
	
	RETURN_UNDEF
}

FUNC(AlignmentOffset) {
	FUNC_START;
	
	if (args.Length() < 1)
		RETURN_ERROR("Argument required");
	
	if (!node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("Argument must be a Buffer");
	
	RETURN_VAL( Integer::New(ISOLATE (intptr_t)node::Buffer::Data(args[0]) & (MEM_ALIGN-1)) );
}

#define CLEANUP_MM { \
	delete[] inputs; \
	delete[] iNums; \
	delete[] outputs; \
	delete[] oNums; \
}


// async stuff
struct MMRequest {
	~MMRequest() {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		inputBuffers.Reset();
		outputBuffers.Reset();
		obj_.Reset();
#else
		inputBuffers.Dispose();
		outputBuffers.Dispose();
		//if (obj_.IsEmpty()) return;
		obj_.Dispose();
		obj_.Clear();
#endif
		CLEANUP_MM
	};
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate;
#endif
	Persistent<Object> obj_;
	uv_work_t work_req_;
	
	uint16_t** inputs;
	uint_fast16_t* iNums;
	unsigned int numInputs;
	size_t len;
	uint16_t** outputs;
	uint_fast16_t* oNums;
	unsigned int numOutputs;
	bool add;
	
	// persist copies of buffers for the duration of the job
	Persistent<Array> inputBuffers;
	Persistent<Array> outputBuffers;
};

void MMWork(uv_work_t* work_req) {
	MMRequest* req = (MMRequest*)work_req->data;
	multiply_mat(
		req->inputs, req->iNums, req->numInputs, req->len, req->outputs, req->oNums, req->numOutputs, req->add
	);
}
void MMAfter(uv_work_t* work_req, int status) {
	assert(status == 0);
	MMRequest* req = (MMRequest*)work_req->data;
	
	mmActiveTasks--;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	HandleScope scope(req->isolate);
	Local<Object> obj = Local<Object>::New(req->isolate, req->obj_);
	node::MakeCallback(req->isolate, obj, "ondone", 0, NULL);
#else
	HandleScope scope;
	node::MakeCallback(req->obj_, "ondone", 0, NULL);
#endif
	
	delete req;
}

FUNC(MultiplyMulti) {
	FUNC_START;
	
	if (mmActiveTasks)
		RETURN_ERROR("Calculation already in progress");
	if (args.Length() < 4)
		RETURN_ERROR("4 arguments required");
	
	if (!args[0]->IsArray() || !args[1]->IsArray())
		RETURN_ERROR("Inputs and inputBlockNumbers must be arrays");
	if (!args[2]->IsArray() || !args[3]->IsArray())
		RETURN_ERROR("Outputs and recoveryBlockNumbers must be arrays");
	
	unsigned int numInputs = Local<Array>::Cast(args[0])->Length();
	unsigned int numOutputs = Local<Array>::Cast(args[2])->Length();
	
	if(numInputs != Local<Array>::Cast(args[1])->Length())
		RETURN_ERROR("Input and inputBlockNumber arrays must have the same length");
	if(numOutputs != Local<Array>::Cast(args[3])->Length())
		RETURN_ERROR("Output and recoveryBlockNumber arrays must have the same length");
	
	Local<Object> oInputs = args[0]->ToObject();
	Local<Object> oIBNums = args[1]->ToObject();
	uint16_t** inputs = new uint16_t*[numInputs];
	uint_fast16_t* iNums = new uint_fast16_t[numInputs];
	
	Local<Object> oOutputs = args[2]->ToObject();
	Local<Object> oRBNums = args[3]->ToObject();
	uint16_t** outputs = new uint16_t*[numOutputs];
	uint_fast16_t* oNums = new uint_fast16_t[numOutputs];
	
	#define RTN_ERROR(m) { \
		CLEANUP_MM \
		RETURN_ERROR(m); \
	}
	
	size_t len = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = oInputs->Get(i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		intptr_t inputAddr = (intptr_t)inputs[i];
		if (inputAddr & (MEM_ALIGN-1))
			RTN_ERROR("All input buffers must be address aligned");
		
		if(i) {
			if (node::Buffer::Length(input) != len)
				RTN_ERROR("All inputs' length must be equal");
		} else {
			len = node::Buffer::Length(input);
			if (len % 2)
				RTN_ERROR("Length of input must be a multiple of 2");
		}
		
		int ibNum = oIBNums->Get(i)->ToInt32()->Value();
		if (ibNum < 0 || ibNum > 32767)
			RTN_ERROR("Invalid input block number specified");
		iNums[i] = ibNum;
	}
	
	
	for(unsigned int i = 0; i < numOutputs; i++) {
		Local<Value> output = oOutputs->Get(i);
		if (!node::Buffer::HasInstance(output))
			RTN_ERROR("All outputs must be Buffers");
		if (node::Buffer::Length(output) < len)
			RTN_ERROR("All outputs' length must equal or greater than the input's length");
		// the length of output buffers should all be equal, but I'm too lazy to check for that :P
		outputs[i] = (uint16_t*)node::Buffer::Data(output);
		if ((intptr_t)outputs[i] & (MEM_ALIGN-1))
			RTN_ERROR("All output buffers must be address aligned");
		int rbNum = oRBNums->Get(i)->ToInt32()->Value();
		if (rbNum < 0 || rbNum > 32767)
			RTN_ERROR("Invalid recovery block number specified");
		oNums[i] = rbNum;
	}
	
	#undef RTN_ERROR
	
	bool add = false;
	if (args.Length() >= 5) {
		add = args[4]->ToBoolean()->Value();
	}
	
	if (args.Length() >= 6 && args[5]->IsFunction()) {
		MMRequest* req = new MMRequest();
		req->work_req_.data = req;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		// use BaseObject / AsyncWrap instead? meh
		req->isolate = isolate;
#endif
		
		req->inputs = inputs;
		req->iNums = iNums;
		req->numInputs = numInputs;
		req->len = len;
		req->outputs = outputs;
		req->oNums = oNums;
		req->numOutputs = numOutputs;
		req->add = add;
		
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		Local<Object> obj = Object::New(isolate);
		obj->Set(String::NewFromOneByte(ISOLATE (const uint8_t*)"ondone"), args[5]);
		req->obj_.Reset(ISOLATE obj);
		//if (env->in_domain())
		//	req->obj_->Set(env->domain_string(), env->domain_array()->Get(0));
		
		// keep a copy of the buffers so that they don't get GC'd whilst being written to
		req->inputBuffers.Reset(ISOLATE Local<Array>::Cast(args[0]));
		req->outputBuffers.Reset(ISOLATE Local<Array>::Cast(args[2]));
#else
		req->obj_ = Persistent<Object>::New(ISOLATE Object::New());
		req->obj_->Set(String::New(ISOLATE "ondone"), args[5]);
		//SetActiveDomain(req->obj_); // never set in node_zlib.cc - perhaps domains aren't that important?
		
		// keep a copy of the buffers so that they don't get GC'd whilst being written to
		req->inputBuffers = Persistent<Array>::New(ISOLATE Local<Array>::Cast(args[0]));
		req->outputBuffers = Persistent<Array>::New(ISOLATE Local<Array>::Cast(args[2]));
#endif
		
		mmActiveTasks++;
		uv_queue_work(
#if NODE_VERSION_AT_LEAST(0, 11, 0)
			//env->event_loop(),
			uv_default_loop(),
#else
			uv_default_loop(),
#endif
			&req->work_req_,
			MMWork,
			MMAfter
		);
		// does req->obj_ need to be returned?
	} else {
		multiply_mat(
			inputs, iNums, numInputs,
			len, outputs, oNums, numOutputs, add
		);
		CLEANUP_MM
	}
	RETURN_UNDEF
}

FUNC(Finish) {
	FUNC_START;
	
	if (args.Length() < 1 || !args[0]->IsArray())
		RETURN_ERROR("First argument must be an array");
	
	unsigned int numInputs = Local<Array>::Cast(args[0])->Length();
	
	Local<Object> oInputs = args[0]->ToObject();
	uint16_t** inputs = new uint16_t*[numInputs];
	
	#define RTN_ERROR(m) { \
		delete[] inputs; \
		RETURN_ERROR(m); \
	}
	
	size_t len = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = oInputs->Get(i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		
		if(i) {
			if (node::Buffer::Length(input) != len)
				RTN_ERROR("All inputs' length must be equal");
		} else {
			len = node::Buffer::Length(input);
			if (len % 2)
				RTN_ERROR("Length of input must be a multiple of 2");
		}
	}
	
	if(using_altmap) {
		// TODO: multi-thread this?
		for(int in = 0; in < (int)numInputs; in++)
			gf[0].unaltmap_region(inputs[in], len, inputs[in]);
	}
	
	delete[] inputs;
	RETURN_UNDEF
}


void init(Handle<Object> target) {
	init_constants();
	
	// generate(Buffer input, int inputBlockNum, Array<Buffer> outputs, Array<int> recoveryBlockNums [, bool add [, Function callback]])
	// ** DON'T modify buffers whilst function is running! **
	NODE_SET_METHOD(target, "generate", MultiplyMulti);
	// int alignment_offset(Buffer buffer)
	NODE_SET_METHOD(target, "alignment_offset", AlignmentOffset);
	// Buffer AlignedBuffer(int size)
	NODE_SET_METHOD(target, "AlignedBuffer", AlignedBuffer);
	NODE_SET_METHOD(target, "copy", PrepInput);
	NODE_SET_METHOD(target, "finish", Finish);
	
#ifdef _OPENMP
	// set_max_threads(int num_threads)
	NODE_SET_METHOD(target, "set_max_threads", SetMaxThreads);
	
	maxNumThreads = omp_get_num_procs();
	if(maxNumThreads < 1) maxNumThreads = 1;
	defaultNumThreads = maxNumThreads;
	
	alloc_gf();
#else
	gf = (gf_t*)malloc(sizeof(gf_t));
	gf_init_hard(gf, 16, GF_MULT_DEFAULT, GF_REGION_ALTMAP, GF_DIVIDE_DEFAULT, 0, 0, 0, NULL, NULL);
#endif
	
	MEM_ALIGN = gf[0].alignment;
	MEM_WALIGN = gf[0].walignment;
	using_altmap = gf[0].using_altmap;
	
	char mult_method[20];
	switch(gf[0].mult_method) {
		case GF_SPLIT8:        strcpy(mult_method, "SPLIT8"); break;
		case GF_SPLIT4:        strcpy(mult_method, "SPLIT4"); break;
		case GF_SPLIT4_NEON:   strcpy(mult_method, "SPLIT4 (NEON)"); break;
		case GF_SPLIT4_SSSE3:  strcpy(mult_method, "SPLIT4 (SSSE3)"); break;
		case GF_SPLIT4_AVX2:   strcpy(mult_method, "SPLIT4 (AVX2)"); break;
		case GF_SPLIT4_AVX512: strcpy(mult_method, "SPLIT4 (AVX512)"); break;
		case GF_XOR_SSE2:      strcpy(mult_method, "XOR (SSE2)"); break;
		case GF_XOR_JIT_SSE2:  strcpy(mult_method, "XOR JIT (SSE2)"); break;
		default:               strcpy(mult_method, "Unknown");
	}
	
	// select a good chunk size
	// TODO: this needs to be variable depending on the CPU cache size
	// although these defaults are pretty good across most CPUs
	switch(gf[0].mult_method) {
		case GF_XOR_JIT_SSE2: /* JIT is a little slow, so larger blocks make things faster */
			CHUNK_SIZE = 128*1024; // maybe 256K is better
			break;
		case GF_SPLIT8:
		case GF_XOR_SSE2:
			CHUNK_SIZE = 32*1024; // 2* L1 data cache size ?
			break;
		default:
			CHUNK_SIZE = 32*1024; // =L1 data cache size seems to be efficient
			break;
	}
	
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	target->Set(String::NewFromUtf8(ISOLATE "alignment"), Integer::New(ISOLATE MEM_ALIGN));
	target->Set(String::NewFromUtf8(ISOLATE "alignment_width"), Integer::New(ISOLATE MEM_WALIGN));
	target->Set(String::NewFromUtf8(ISOLATE "gf_method"), String::NewFromUtf8(ISOLATE mult_method));
#else
	HandleScope scope;
	target->Set(String::New("alignment"), Integer::New(MEM_ALIGN));
	target->Set(String::New("alignment_width"), Integer::New(MEM_WALIGN));
	target->Set(String::New("gf_method"), String::New(mult_method));
#endif
}

NODE_MODULE(parpar_gf, init);
