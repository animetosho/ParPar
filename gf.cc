
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdlib.h>
//#include <inttypes.h>
#include <stdio.h>
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

#ifdef _OPENMP
int maxNumThreads = 1, defaultNumThreads = 1;
#else
const int maxNumThreads = 1;
#endif

// performs multiple multiplies for a region, using threads
/* REQUIRES:
   - input and each pointer in outputs must be aligned to 2 bytes
   - len must be a multiple of two (duh)
   - input and length of each output is the same and == len
   - number of outputs and scales is same and == numOutputs
*/
static inline void multiply_multi(/*const*/ uint16_t** inputs, unsigned int numInputs, size_t len, uint16_t** outputs, uint_fast16_t* scales, unsigned int numOutputs, bool add) {
#ifdef _OPENMP
	int max_threads = omp_get_max_threads();
	if(max_threads != maxNumThreads)
		// handle the possibility that some other module changes this
		omp_set_num_threads(maxNumThreads);
	
	#pragma omp parallel for
	for(int i = 0; i < (int)numOutputs; i++) {
		for(unsigned int j = 0; j < numInputs; j++)
			gf.multiply_region.w32(&gf, inputs[j], outputs[i], scales[i], (int)len, add || j>0);
	}
	
	// if(max_threads != maxNumThreads)
		// omp_set_num_threads(max_threads);
#else
	for(unsigned int i = 0; i < numOutputs; i++) {
		for(unsigned int j = 0; j < numInputs; j++)
			gf.multiply_region.w32(&gf, inputs[j], outputs[i], scales[i], (int)len, add || j>0);
	}
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
#define RETURN_UNDEF
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
	free(data);
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

	
#ifndef NODE_010
	if(!buf)
		node::FatalError("AlignedBuffer", "Out Of Memory");
#endif
	
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

// async stuff
struct MMRequest {
	~MMRequest() {
#ifndef NODE_010
		inputBuffer.Reset();
		buffers.Reset();
		obj_.Reset();
#else
		inputBuffer.Dispose();
		buffers.Dispose();
		//if (obj_.IsEmpty()) return;
		obj_.Dispose();
		obj_.Clear();
#endif
		delete[] inputs;
		delete[] outputs;
		delete[] scales;
	};
#ifndef NODE_010
	Isolate* isolate;
#endif
	Persistent<Object> obj_;
	uv_work_t work_req_;
	
	uint16_t** inputs;
	unsigned int numInputs;
	size_t len;
	uint16_t** outputs;
	uint_fast16_t* scales;
	unsigned int numOutputs;
	bool add;
	
	// persist copies of buffers for the duration of the job
	Persistent<Value> inputBuffer;
	Persistent<Array> buffers;
};

void MMWork(uv_work_t* work_req) {
	MMRequest* req = (MMRequest*)work_req->data;
	multiply_multi(
		req->inputs, req->numInputs, req->len, req->outputs, req->scales, req->numOutputs, req->add
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
	
	// TODO: support multiple input blocks
	if (!node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("Input must be a Buffer");
	
	size_t len = node::Buffer::Length(args[0]);
	if (len % 2)
		RETURN_ERROR("Length of input must be a multiple of 2");
	uint16_t* data = (uint16_t*)node::Buffer::Data(args[0]);
	intptr_t inputAddress = (intptr_t)data;
	if (inputAddress & 1)
		RETURN_ERROR("Input buffer must be address aligned to a 16-bit boundary");
	
	int inputBlock = args[1]->ToInt32()->Value();
	if (inputBlock < 0 || inputBlock > 32767)
		RETURN_ERROR("Invalid input block number");
	
	if (!args[2]->IsArray() || !args[3]->IsArray())
		RETURN_ERROR("Outputs and recoveryBlockNumbers must be arrays");
	
	unsigned int numOutputs = Local<Array>::Cast(args[2])->Length();
	
	if(numOutputs != Local<Array>::Cast(args[3])->Length())
		RETURN_ERROR("Output and recoveryBlockNumber arrays must have the same length");
	
	int inputAlignmentOffset = inputAddress & (MEM_ALIGN-1);
	Local<Object> oOutputs = args[2]->ToObject();
	Local<Object> oRBNums = args[3]->ToObject();
	uint16_t** outputs = new uint16_t*[numOutputs];
	uint_fast16_t* scales = new uint_fast16_t[numOutputs];
	for(unsigned int i = 0; i < numOutputs; i++) {
		Local<Value> output = oOutputs->Get(i);
		if (!node::Buffer::HasInstance(output)) {
			delete[] outputs;
			delete[] scales;
			RETURN_ERROR("All outputs must be Buffers");
		}
		if (node::Buffer::Length(output) != len) {
			delete[] outputs;
			delete[] scales;
			RETURN_ERROR("All outputs' length must equal the input's length");
		}
		outputs[i] = (uint16_t*)node::Buffer::Data(output);
		if ((intptr_t)outputs[i] & 1) {
			delete[] outputs;
			delete[] scales;
			RETURN_ERROR("All output buffers must be address aligned to a 16-bit boundary");
		}
		if (((intptr_t)outputs[i] & (MEM_ALIGN-1)) != inputAlignmentOffset) {
			delete[] outputs;
			delete[] scales;
			RETURN_ERROR("All output buffers must be address aligned to the same alignment as the input buffer for a 128-bit boundary");
		}
		int rbNum = oRBNums->Get(i)->ToInt32()->Value();
		if (rbNum < 0 || rbNum > 32767) {
			delete[] outputs;
			delete[] scales;
			RETURN_ERROR("Invalid recovery block number specified");
		}
		scales[i] = calc_factor(inputBlock, rbNum);
	}
	
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
		
		req->inputs = new uint16_t*[1];
		req->inputs[0] = data;
		req->numInputs = 1;
		req->len = len;
		req->outputs = outputs;
		req->scales = scales;
		req->numOutputs = numOutputs;
		req->add = add;
		
#ifndef NODE_010
		Local<Object> obj = Object::New(isolate);
		obj->Set(String::NewFromOneByte(ISOLATE (const uint8_t*)"ondone"), args[4]);
		req->obj_.Reset(ISOLATE obj);
		//if (env->in_domain())
		//	req->obj_->Set(env->domain_string(), env->domain_array()->Get(0));
		
		// keep a copy of the buffers so that they don't get GC'd whilst being written to
		req->inputBuffer.Reset(ISOLATE args[0]);
		req->buffers.Reset(ISOLATE Local<Array>::Cast(args[2]));
#else
		req->obj_ = Persistent<Object>::New(ISOLATE Object::New());
		req->obj_->Set(String::New(ISOLATE "ondone"), args[5]);
		//SetActiveDomain(req->obj_); // never set in node_zlib.cc - perhaps domains aren't that important?
		
		// keep a copy of the buffers so that they don't get GC'd whilst being written to
		req->inputBuffer = Persistent<Object>::New(ISOLATE args[0]->ToObject());
		req->buffers = Persistent<Array>::New(ISOLATE Local<Array>::Cast(args[2]));
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
		multiply_multi(
			&data, 1,
			len, outputs, scales, numOutputs, add
		);
		delete[] outputs;
		delete[] scales;
	}
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

	#define GF_ARGS 16, GF_MULT_DEFAULT, GF_REGION_DEFAULT, GF_DIVIDE_DEFAULT
	gf_mem = malloc(gf_scratch_size(GF_ARGS, 0, 0));
	gf_init_hard(&gf, GF_ARGS, 0, 0, 0, NULL, gf_mem);

}

NODE_MODULE(parpar_gf, init);
