
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdlib.h>
//#include <inttypes.h>
#include <stdio.h>
#include <uv.h>

#if defined(_MSC_VER)
#include <malloc.h>
#include <intrin.h>
#endif


extern "C" {
#ifdef _OPENMP
#include <omp.h>
#endif
#include <gf_complete.h>
}

// memory alignment to 16-bytes for SSE operations
#define MEM_ALIGN 16

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

gf_t gf;
void* gf_mem;
bool using_altmap = false;

#ifdef _OPENMP
int maxNumThreads = 1, defaultNumThreads = 1;
#else
const int maxNumThreads = 1;
#endif

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
	
	if(using_altmap) {
		#pragma omp parallel for
		for(int in = 0; in < (int)numInputs; in++)
			// trash our input!
			gf.altmap_region(inputs[in], len, inputs[in]);
		
		/*
		#pragma omp parallel for
		for(int out = 0; out < (int)numOutputs; out++) {
			unsigned int in = 1;
			gf.multiply_region.w32(&gf, inputs[0], outputs[out], calc_factor(iNums[0], oNums[out]), (int)len, add);
			for(; in < numInputs-3; in+=4) {
				gf_val_32_t inNum4[4] = {
					calc_factor(iNums[in], oNums[out]),
					calc_factor(iNums[in+1], oNums[out]),
					calc_factor(iNums[in+2], oNums[out]),
					calc_factor(iNums[in+3], oNums[out])
				};
				gf.multiply_regionX.w16(&gf, inputs + in, outputs[out], inNum4, (int)len);
			}
			for(; in < numInputs; in++)
				gf.multiply_region.w32(&gf, inputs[in], outputs[out], calc_factor(iNums[in], oNums[out]), (int)len, true);
		}
		*/
	}
	
	// TODO: consider chunking for better cache hits?
	#pragma omp parallel for
	for(int out = 0; out < (int)numOutputs; out++) {
		gf.multiply_region.w32(&gf, inputs[0], outputs[out], calc_factor(iNums[0], oNums[out]), (int)len, add);
		for(unsigned int in = 1; in < numInputs; in++) {
			gf.multiply_region.w32(&gf, inputs[in], outputs[out], calc_factor(iNums[in], oNums[out]), (int)len, true);
		}
	}
	
#ifdef _OPENMP
	// if(max_threads != maxNumThreads)
		// omp_set_num_threads(max_threads);
#endif
}

/*******************************************/

#ifndef NODE_010
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
	
	maxNumThreads = args[0]->ToInt32()->Value();
	if(maxNumThreads < 1) maxNumThreads = defaultNumThreads;
	
	RETURN_UNDEF
}
#endif

void free_buffer(char* data, void* _size) {
#ifdef NODE_010
	int size = (int)(size_t)_size;
	V8::AdjustAmountOfExternalAllocatedMemory(-size);
#endif
#if (!defined(__cplusplus) || __cplusplus <= 201100) && defined(_MSC_VER)
	_aligned_free(data);
#else
	free(data);
#endif
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
#if defined(__cplusplus) && __cplusplus > 201100
	// C++11 method
	buf = (char*)aligned_alloc(MEM_ALIGN, (len + MEM_ALIGN-1) & ~(MEM_ALIGN-1)); // len needs to be a multiple of 16, although it sometimes works if it isn't...
#elif defined(_MSC_VER)
	buf = (char*)_aligned_malloc(len, MEM_ALIGN);
#else
	if(posix_memalign((void**)&buf, MEM_ALIGN, len))
		buf = NULL;
#endif

	
	if(!buf)
		RETURN_ERROR("Out Of Memory");
	
	/*
	if(hasString)
		node::StringBytes::Write(ISOLATE buf, len, args[1]->ToString(), end);
	*/
	
#ifndef NODE_010
	RETURN_VAL( node::Buffer::New(ISOLATE (char*)buf, len, free_buffer, (void*)len) );
#else
	// convert SlowBuffer to JS Buffer object and return it
	Handle<Value> _tmp[] = {
		node::Buffer::New((char*)buf, len, free_buffer, (void*)len)->handle_,
		Integer::New((int32_t)len),
		Integer::New(0)
	};
	RETURN_VAL(
		Local<Function>::Cast(Context::GetCurrent()->Global()->Get(String::New("Buffer")))->NewInstance(3, _tmp)
	);
#endif
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
#ifndef NODE_010
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
#ifndef NODE_010
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
	
#ifndef NODE_010
	HandleScope scope(req->isolate);
	Local<Object> obj = Local<Object>::New(req->isolate, req->obj_);
	node::MakeCallback(req->isolate, obj, "ondone", 0, NULL);
#else
	HandleScope scope;
	node::MakeCallback(req->obj_, "ondone", 0, NULL);
#endif
	
	mmActiveTasks--;
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
	int addressOffset = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = oInputs->Get(i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		intptr_t inputAddr = (intptr_t)inputs[i];
		if (inputAddr & 1)
			RTN_ERROR("All input buffers must be address aligned to a 16-bit boundary");
		
		if(i) {
			if (node::Buffer::Length(input) != len)
				RTN_ERROR("All inputs' length must be equal");
			if ((inputAddr & (MEM_ALIGN-1)) != addressOffset)
				RTN_ERROR("All input buffers must be address aligned to the same alignment for a 128-bit boundary");
		} else {
			len = node::Buffer::Length(input);
			if (len % 2)
				RTN_ERROR("Length of input must be a multiple of 2");
			addressOffset = inputAddr & (MEM_ALIGN-1);
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
		if (node::Buffer::Length(output) != len)
			RTN_ERROR("All outputs' length must equal the input's length");
		outputs[i] = (uint16_t*)node::Buffer::Data(output);
		if (((intptr_t)outputs[i] & (MEM_ALIGN-1)) != addressOffset)
			RTN_ERROR("All output buffers must be address aligned to the same alignment as the input buffer for a 128-bit boundary");
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
#ifndef NODE_010
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
		
#ifndef NODE_010
		Local<Object> obj = Object::New(isolate);
		obj->Set(String::NewFromOneByte(ISOLATE (const uint8_t*)"ondone"), args[4]);
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
#ifndef NODE_010
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

FUNC(Finalise) {
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
	int addressOffset = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = oInputs->Get(i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		intptr_t inputAddr = (intptr_t)inputs[i];
		if (inputAddr & 1)
			RTN_ERROR("All input buffers must be address aligned to a 16-bit boundary");
		
		if(i) {
			if (node::Buffer::Length(input) != len)
				RTN_ERROR("All inputs' length must be equal");
			if ((inputAddr & (MEM_ALIGN-1)) != addressOffset)
				RTN_ERROR("All input buffers must be address aligned to the same alignment for a 128-bit boundary");
		} else {
			len = node::Buffer::Length(input);
			if (len % 2)
				RTN_ERROR("Length of input must be a multiple of 2");
			addressOffset = inputAddr & (MEM_ALIGN-1);
		}
	}
	
	if(using_altmap) {
		// TODO: multi-thread this?
		for(int in = 0; in < (int)numInputs; in++)
			gf.unaltmap_region(inputs[in], len, inputs[in]);
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
	NODE_SET_METHOD(target, "finalise", Finalise);
	
#ifndef NODE_010
	HandleScope scope(Isolate::GetCurrent());
	target->Set(String::NewFromUtf8(isolate, "alignment"), Integer::New(MEM_ALIGN));
#else
	HandleScope scope;
	target->Set(String::New("alignment"), Integer::New(MEM_ALIGN));
#endif

#ifdef _OPENMP
	// set_max_threads(int num_threads)
	NODE_SET_METHOD(target, "set_max_threads", SetMaxThreads);
	
	maxNumThreads = omp_get_num_procs();
	defaultNumThreads = maxNumThreads;
#endif



// if SSSE3 supported, use ALTMAP
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	#ifdef INTEL_SSSE3
	using_altmap = (cpuInfo[2] & 0x200) != 0;
	#endif
#elif defined(_IS_X86)
	uint32_t flags;
	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	#ifdef INTEL_SSSE3
	using_altmap = (flags & 0x200) != 0;
	#endif
#endif

	if(using_altmap) {
		#define GF_ARGS 16, GF_MULT_SPLIT_TABLE, GF_REGION_ALTMAP, GF_DIVIDE_DEFAULT
		gf_mem = malloc(gf_scratch_size(GF_ARGS, 16, 4));
		gf_init_hard(&gf, GF_ARGS, 0, 16, 4, NULL, gf_mem);
		#undef GF_ARGS
	} else {
		#define GF_ARGS 16, GF_MULT_DEFAULT, GF_REGION_DEFAULT, GF_DIVIDE_DEFAULT
		gf_mem = malloc(gf_scratch_size(GF_ARGS, 0, 0));
		gf_init_hard(&gf, GF_ARGS, 0, 0, 0, NULL, gf_mem);
		#undef GF_ARGS
	}
}

NODE_MODULE(parpar_gf, init);
