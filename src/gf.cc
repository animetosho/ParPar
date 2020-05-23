
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

#include "../gf16/module.h"

extern "C" {
#ifdef _OPENMP
#include <omp.h>
#endif
#include "../md5/md5.h"
}

static int MEM_ALIGN, MEM_STRIDE;

#if defined(__cplusplus) && __cplusplus >= 201100 && !(defined(_MSC_VER) && defined(__clang__)) && !defined(__APPLE__)
	// C++11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len) *(void**)&(buf) = aligned_alloc(MEM_ALIGN, ((len) + MEM_ALIGN-1) & ~(MEM_ALIGN-1))
	#define ALIGN_FREE free
#elif defined(_MSC_VER)
	#define ALIGN_ALLOC(buf, len) *(void**)&(buf) = _aligned_malloc((len), MEM_ALIGN)
	#define ALIGN_FREE _aligned_free
#else
	#include <stdlib.h>
	#define ALIGN_ALLOC(buf, len) if(posix_memalign((void**)&(buf), MEM_ALIGN, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


using namespace v8;

/*******************************************/

#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x
#define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
#define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HandleScope scope(isolate)

# if NODE_VERSION_AT_LEAST(8, 0, 0)
#  define NEW_STRING(s) String::NewFromOneByte(isolate, (const uint8_t*)(s), NewStringType::kNormal).ToLocalChecked()
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)(e), NewStringType::kNormal).ToLocalChecked())); return; }
#  define ARG_TO_INT(a) (a).As<Integer>()->Value()
#  define ARG_TO_OBJ(a) (a).As<Object>()
# else
#  define NEW_STRING(s) String::NewFromUtf8(isolate, s)
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, e))); return; }
#  define ARG_TO_INT(a) (a)->ToInteger()->Value()
#  define ARG_TO_OBJ(a) (a)->ToObject()
# endif

#define RETURN_VAL(v) args.GetReturnValue().Set(v)
#define RETURN_UNDEF return;
#define ISOLATE isolate,

#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define FUNC_START HandleScope scope
#define NEW_STRING String::New
#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_UNDEF RETURN_VAL( Undefined() );
#define ISOLATE

#endif

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__).ToLocalChecked()
#else
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__)
#endif

#if NODE_VERSION_AT_LEAST(12, 0, 0)
# define SET_OBJ(obj, key, val) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), val).Check()
# define GET_ARR(obj, idx) (obj)->Get(isolate->GetCurrentContext(), idx).ToLocalChecked()
#else
# define SET_OBJ(obj, key, val) (obj)->Set(NEW_STRING(key), val)
# define GET_ARR(obj, idx) (obj)->Get(idx)
#endif



int mmActiveTasks = 0;

#ifdef _OPENMP
FUNC(SetMaxThreads) {
	FUNC_START;
	
	if (args.Length() < 1)
		RETURN_ERROR("Argument required");
	if (mmActiveTasks)
		RETURN_ERROR("Calculation already in progress");
	
	ppgf_set_num_threads(ARG_TO_INT(args[0]));
	
	RETURN_UNDEF
}
#endif

FUNC(GetNumThreads) {
	FUNC_START;
	RETURN_VAL(Integer::New(ISOLATE ppgf_get_num_threads()));
}

#if !NODE_VERSION_AT_LEAST(3, 0, 0)
static void free_buffer(char* data, void* _size) {
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
		len = (size_t)ARG_TO_INT(args[0]);
	
	char* buf = NULL;
	ALIGN_ALLOC(buf, len);
	
	if(!buf)
		RETURN_ERROR("Out Of Memory");
	
	/*
	if(hasString)
		node::StringBytes::Write(ISOLATE buf, len, args[1]->ToString(), end);
	*/
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	RETURN_VAL( BUFFER_NEW((char*)buf, len, free_buffer, (void*)len) );
#else
	RETURN_VAL(Local<Object>::New(
		node::Buffer::New((char*)buf, len, free_buffer, (void*)len)->handle_
	));
#endif
}
#endif

FUNC(PrepInput) {
	FUNC_START;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1]))
		RETURN_ERROR("Two Buffers required");
	
	size_t destLen = node::Buffer::Length(args[1]),
		inputLen = node::Buffer::Length(args[0]);
	char* dest = node::Buffer::Data(args[1]);
	char* src = node::Buffer::Data(args[0]);
	
	if((uintptr_t)dest & (MEM_ALIGN-1))
		RETURN_ERROR("Destination not aligned");
	if(((inputLen + (MEM_STRIDE-1)) & ~(MEM_STRIDE-1)) > destLen)
		RETURN_ERROR("Destination not large enough to hold input");
	
	ppgf_prep_input(destLen, inputLen, dest, src);
	
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

static void MMWork(uv_work_t* work_req) {
	MMRequest* req = (MMRequest*)work_req->data;
	ppgf_multiply_mat(
		req->inputs, req->iNums, req->numInputs, req->len, req->outputs, req->oNums, req->numOutputs, req->add
	);
}
static void MMAfter(uv_work_t* work_req, int status) {
	assert(status == 0);
	MMRequest* req = (MMRequest*)work_req->data;
	
	mmActiveTasks--;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	HandleScope scope(req->isolate);
	Local<Object> obj = Local<Object>::New(req->isolate, req->obj_);
# if NODE_VERSION_AT_LEAST(10, 0, 0)
	node::async_context ac;
	memset(&ac, 0, sizeof(ac));
	node::MakeCallback(req->isolate, obj, "ondone", 0, NULL, ac);
# else
	node::MakeCallback(req->isolate, obj, "ondone", 0, NULL);
# endif
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
	
	Local<Object> oInputs = ARG_TO_OBJ(args[0]);
	Local<Object> oIBNums = ARG_TO_OBJ(args[1]);
	uint16_t** inputs = new uint16_t*[numInputs];
	uint_fast16_t* iNums = new uint_fast16_t[numInputs];
	
	Local<Object> oOutputs = ARG_TO_OBJ(args[2]);
	Local<Object> oRBNums = ARG_TO_OBJ(args[3]);
	uint16_t** outputs = new uint16_t*[numOutputs];
	uint_fast16_t* oNums = new uint_fast16_t[numOutputs];
	
	#define RTN_ERROR(m) { \
		CLEANUP_MM \
		RETURN_ERROR(m); \
	}
	
	size_t len = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = GET_ARR(oInputs, i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		uintptr_t inputAddr = (uintptr_t)inputs[i];
		if (inputAddr & (MEM_ALIGN-1))
			RTN_ERROR("All input buffers must be address aligned");
		
		if(i) {
			if (node::Buffer::Length(input) != len)
				RTN_ERROR("All inputs' length must be equal");
		} else {
			len = node::Buffer::Length(input);
			if ((len & (MEM_STRIDE-1)) != 0)
				RTN_ERROR("Length of input must be a multiple of stride");
		}
		
		int ibNum = ARG_TO_INT(GET_ARR(oIBNums, i));
		if (ibNum < 0 || ibNum > 32767)
			RTN_ERROR("Invalid input block number specified");
		iNums[i] = ibNum;
	}
	
	
	for(unsigned int i = 0; i < numOutputs; i++) {
		Local<Value> output = GET_ARR(oOutputs, i);
		if (!node::Buffer::HasInstance(output))
			RTN_ERROR("All outputs must be Buffers");
		if (node::Buffer::Length(output) < len)
			RTN_ERROR("All outputs' length must equal or greater than the input's length");
		// the length of output buffers should all be equal, but I'm too lazy to check for that :P
		outputs[i] = (uint16_t*)node::Buffer::Data(output);
		if ((uintptr_t)outputs[i] & (MEM_ALIGN-1))
			RTN_ERROR("All output buffers must be address aligned");
		int rbNum = ARG_TO_INT(GET_ARR(oRBNums, i));
		if (rbNum < 0 || rbNum > 65535)
			RTN_ERROR("Invalid recovery block number specified");
		oNums[i] = rbNum;
	}
	
	#undef RTN_ERROR
	
	bool add = false;
	if (args.Length() >= 5) {
#if NODE_VERSION_AT_LEAST(8, 0, 0)
		add = args[4].As<Boolean>()->Value();
#else
		add = args[4]->ToBoolean()->Value();
#endif
	}
	
	ppgf_maybe_setup_gf();
	
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
		SET_OBJ(obj, "ondone", args[5]);
		req->obj_.Reset(ISOLATE obj);
		//if (env->in_domain())
		//	req->obj_->Set(env->domain_string(), env->domain_array()->Get(0));
		
		// keep a copy of the buffers so that they don't get GC'd whilst being written to
		req->inputBuffers.Reset(ISOLATE Local<Array>::Cast(args[0]));
		req->outputBuffers.Reset(ISOLATE Local<Array>::Cast(args[2]));
#else
		req->obj_ = Persistent<Object>::New(ISOLATE Object::New());
		req->obj_->Set(NEW_STRING("ondone"), args[5]);
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
		ppgf_multiply_mat(
			inputs, iNums, numInputs,
			len, outputs, oNums, numOutputs, add
		);
		CLEANUP_MM
	}
	RETURN_UNDEF
}

FUNC(Finish) {
	FUNC_START;
	
	if (args.Length() < 2)
		RETURN_ERROR("At least two arguments required");
	
	if (!args[0]->IsArray())
		RETURN_ERROR("First argument must be an array");
	
	unsigned int numInputs = Local<Array>::Cast(args[0])->Length();
	unsigned int allocArrSize = numInputs;
	if(numInputs < 1) RETURN_UNDEF
	
	Local<Object> oInputs = ARG_TO_OBJ(args[0]);
	bool calcMd5 = false;
	if (args.Length() >= 3 && !args[2]->IsUndefined()) {
		if (!args[2]->IsArray())
			RETURN_ERROR("MD5 contexts not an array");
		if (Local<Array>::Cast(args[2])->Length() != numInputs)
			RETURN_ERROR("Number of MD5 contexts doesn't equal number of inputs");
		calcMd5 = true;
		
		if(numInputs % MD5_SIMD_NUM)
			// if calculating MD5, allocate some more space to make parallel processing easier
			allocArrSize += MD5_SIMD_NUM - (numInputs % MD5_SIMD_NUM);
	}
	uint16_t** inputs = new uint16_t*[allocArrSize];
	
	#define RTN_ERROR(m) { \
		delete[] inputs; \
		RETURN_ERROR(m); \
	}
	
	size_t len = (size_t)ARG_TO_INT(args[1]);
	if (len % 2)
		RTN_ERROR("Length must be a multiple of 2");
	size_t bufLen = 0;
	for(unsigned int i = 0; i < numInputs; i++) {
		Local<Value> input = GET_ARR(oInputs, i);
		if (!node::Buffer::HasInstance(input))
			RTN_ERROR("All inputs must be Buffers");
		inputs[i] = (uint16_t*)node::Buffer::Data(input);
		
		size_t currentLen = node::Buffer::Length(input);
		if (currentLen < len)
			RTN_ERROR("All inputs' length must be at least specified size");
		if(i) {
			if (currentLen != bufLen)
				RTN_ERROR("All inputs' length must be equal");
		} else {
			bufLen = currentLen;
		}
		if((uintptr_t)(node::Buffer::Data(input)) & (MEM_ALIGN-1))
			RETURN_ERROR("All inputs' must be aligned");
	}
	if ((bufLen & (MEM_STRIDE-1)) != 0)
		RTN_ERROR("Length of input must be a multiple of stride");
	#undef RTN_ERROR
	
	MD5_CTX** md5 = NULL;
	MD5_CTX dummyMd5;
	if(calcMd5) {
		Local<Object> oMd5 = ARG_TO_OBJ(args[2]);
		md5 = new MD5_CTX*[allocArrSize];
		
		unsigned int i = 0;
		for(; i < numInputs; i++) {
			Local<Value> md5Ctx = GET_ARR(oMd5, i);
			if (!node::Buffer::HasInstance(md5Ctx) || node::Buffer::Length(md5Ctx) != sizeof(MD5_CTX)) {
				delete[] inputs;
				delete[] md5;
				RETURN_ERROR("Invalid MD5 contexts provided");
			}
			md5[i] = (MD5_CTX*)node::Buffer::Data(md5Ctx);
			if(md5[i]->dataLen > MD5_BLOCKSIZE) {
				delete[] inputs;
				delete[] md5;
				RETURN_ERROR("Invalid MD5 contexts provided");
			}
		}
		// for padding, fill with dummy pointers
		for(; i < allocArrSize; i++) {
			md5[i] = &dummyMd5;
			inputs[i] = inputs[0];
		}
		dummyMd5.dataLen = md5[0]->dataLen;
	}
	
	// TODO: make this stuff async
	ppgf_finish_input(numInputs, inputs, bufLen);
	if(calcMd5) {
		ppgf_omp_check_num_threads();
		int i=0;
		#pragma omp parallel for
		for(i=0; i<(int)numInputs; i+=MD5_SIMD_NUM) {
			md5_multi_update(md5 + i, (const void**)(inputs + i), len);
		}
		delete[] md5;
	}
	
	delete[] inputs;
	RETURN_UNDEF
}

FUNC(MD5Start) {
	FUNC_START;
	MD5_CTX* ctx;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Local<Object> buff = BUFFER_NEW(sizeof(MD5_CTX));
#else
	node::Buffer* buff = BUFFER_NEW(sizeof(MD5_CTX));
#endif
	
	ctx = (MD5_CTX*)node::Buffer::Data(buff);
	md5_init(ctx);
	
	// in some cases, we want to pre-populate some data
	if (args.Length() > 0 && !args[0]->IsUndefined()) {
		if (!node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("First argument must be a Buffer");
		
		size_t len = node::Buffer::Length(args[0]);
		if (len > MD5_BLOCKSIZE)
			RETURN_ERROR("Init data too long");
		
		memcpy(ctx->data, node::Buffer::Data(args[0]), len);
		ctx->dataLen = (uint8_t)len;
		ctx->length = len << 3;
	}
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	RETURN_VAL(buff);
#else
	RETURN_VAL(buff->handle_);
#endif
}

// finish single MD5
FUNC(MD5Finish) {
	FUNC_START;
	
	if (args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("First argument must be a Buffer");
	
	if(node::Buffer::Length(args[0]) != sizeof(MD5_CTX))
		RETURN_ERROR("Invalid MD5 context length");
	
	/*
	Local<Object> o = ARG_TO_OBJ(args[0]);
	// TODO: check object validity
	
	// copy to MD5 ctx
	MD5_CTX ctx;
	ctx.A = o.Get(NEW_STRING("A"))->ToUint32().Value();
	ctx.B = o.Get(NEW_STRING("B"))->ToUint32().Value();
	ctx.C = o.Get(NEW_STRING("C"))->ToUint32().Value();
	ctx.D = o.Get(NEW_STRING("D"))->ToUint32().Value();
	ctx.length = ARG_TO_INT(o.Get(NEW_STRING("length")));
	ctx.dataLen = o.Get
	*/
	
	MD5_CTX* ctx = (MD5_CTX*)node::Buffer::Data(args[0]);
	if(ctx->dataLen > MD5_BLOCKSIZE)
		RETURN_ERROR("Invalid MD5 context data");
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Local<Object> md5 = BUFFER_NEW(16);
	md5_final((unsigned char*)node::Buffer::Data(md5), ctx);
	RETURN_VAL(md5);
#else
	node::Buffer* md5 = BUFFER_NEW(16);
	md5_final((unsigned char*)node::Buffer::Data(md5), ctx);
	RETURN_VAL(md5->handle_);
#endif
}

// update two MD5 contexts with one input
FUNC(MD5Update2) {
	FUNC_START;
	
	if (args.Length() < 3)
		RETURN_ERROR("3 arguments required");
	if (!node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1]) || !node::Buffer::HasInstance(args[2]))
		RETURN_ERROR("All arguments must be Buffers");
	
	if(node::Buffer::Length(args[0]) != sizeof(MD5_CTX) || node::Buffer::Length(args[1]) != sizeof(MD5_CTX))
		RETURN_ERROR("Invalid MD5 context length");
	if(((MD5_CTX*)node::Buffer::Data(args[0]))->dataLen > MD5_BLOCKSIZE
	|| ((MD5_CTX*)node::Buffer::Data(args[1]))->dataLen > MD5_BLOCKSIZE)
		RETURN_ERROR("Invalid MD5 context data");
	
	
	// TODO: test 2 buffer update methods
	
	MD5_CTX *md5[MD5_SIMD_NUM];
	char* inputs[MD5_SIMD_NUM];
	
#if MD5_SIMD_NUM == 1
	inputs[0] = node::Buffer::Data(args[2]);
	size_t len = node::Buffer::Length(args[2]);
	md5[0] = (MD5_CTX*)node::Buffer::Data(args[0]);
	md5_multi_update(md5, (const void**)inputs, len);
	
	md5[0] = (MD5_CTX*)node::Buffer::Data(args[1]);
	md5_multi_update(md5, (const void**)inputs, len);
#else
	MD5_CTX dummyMd5;
	for(unsigned int i=0; i<MD5_SIMD_NUM; i++) {
		md5[i] = &dummyMd5;
		inputs[i] = node::Buffer::Data(args[2]);
	}
	md5[0] = (MD5_CTX*)node::Buffer::Data(args[0]);
	md5[1] = (MD5_CTX*)node::Buffer::Data(args[1]);
	dummyMd5.dataLen = md5[0]->dataLen;
	
	md5_multi_update(md5, (const void**)inputs, node::Buffer::Length(args[2]));
#endif
	
	RETURN_UNDEF
}

FUNC(MD5UpdateZeroes) {
	FUNC_START;
	
	if (args.Length() < 2)
		RETURN_ERROR("2 arguments required");
	if (!node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("First argument must be a Buffer");
	
	if(node::Buffer::Length(args[0]) != sizeof(MD5_CTX))
		RETURN_ERROR("Invalid MD5 context length");
	if(((MD5_CTX*)node::Buffer::Data(args[0]))->dataLen > MD5_BLOCKSIZE)
		RETURN_ERROR("Invalid MD5 context data");
	
	if(sizeof(size_t) < 6) {
		// 32-bit platform, may need to feed via multiple passes
#if NODE_VERSION_AT_LEAST(8, 0, 0)
		double len = args[1].As<Number>()->Value();
#else
		double len = args[1]->NumberValue();
#endif
		MD5_CTX* md5 = (MD5_CTX*)node::Buffer::Data(args[0]);
		#define MD5_MAX_LEN 0x7fffffff
		while(len > MD5_MAX_LEN) {
			md5_update_zeroes(md5, MD5_MAX_LEN);
			len -= MD5_MAX_LEN;
		}
		#undef MD5_MAX_LEN
		md5_update_zeroes(md5, (size_t)len);
	} else {
		size_t len = (size_t)ARG_TO_INT(args[1]);
		md5_update_zeroes((MD5_CTX*)node::Buffer::Data(args[0]), len);
	}
	
	RETURN_UNDEF
}

FUNC(SetMethod) {
	FUNC_START;
	
	if (mmActiveTasks)
		RETURN_ERROR("Calculation already in progress");
	
	if(ppgf_set_method(
		args.Length() >= 1 && !args[0]->IsUndefined() ? ARG_TO_INT(args[0]) : 0 /*GF16_AUTO*/,
		args.Length() >= 2 ? ARG_TO_INT(args[1]) : 0
	))
		RETURN_ERROR("Unknown method specified");
	
	// return method info
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Local<Object> ret = Object::New(isolate);
#else
	Local<Object> ret = Object::New();
#endif
	
	int rMethod;
	const char* rMethLong;
	ppgf_get_method(&rMethod, &rMethLong, &MEM_ALIGN, &MEM_STRIDE);
	
	SET_OBJ(ret, "alignment", Integer::New(ISOLATE MEM_ALIGN));
	SET_OBJ(ret, "stride", Integer::New(ISOLATE MEM_STRIDE));
	SET_OBJ(ret, "method", Integer::New(ISOLATE rMethod));
	SET_OBJ(ret, "method_desc", NEW_STRING(rMethLong));
	
	
	RETURN_VAL(ret);
}


void parpar_gf_init(
#if NODE_VERSION_AT_LEAST(4, 0, 0)
 Local<Object> target,
 Local<Value> module,
 void* priv
#else
 Handle<Object> target
#endif
) {
	ppgf_init_constants();
	ppgf_init_gf_module();
	
	int rMethod;
	const char* rMethLong;
	ppgf_get_method(&rMethod, &rMethLong, &MEM_ALIGN, &MEM_STRIDE);
	
	NODE_SET_METHOD(target, "md5_init", MD5Start);
	NODE_SET_METHOD(target, "md5_final", MD5Finish);
	NODE_SET_METHOD(target, "md5_update2", MD5Update2);
	NODE_SET_METHOD(target, "md5_update_zeroes", MD5UpdateZeroes);
	
	// generate(Buffer input, int inputBlockNum, Array<Buffer> outputs, Array<int> recoveryBlockNums [, bool add [, Function callback]])
	// ** DON'T modify buffers whilst function is running! **
	NODE_SET_METHOD(target, "generate", MultiplyMulti);
	// int alignment_offset(Buffer buffer)
	NODE_SET_METHOD(target, "alignment_offset", AlignmentOffset);
	
	// for some reason, creating our own Buffers is unreliable on node >=3, so fall back to emulation
	// TODO: see reason why
#if !NODE_VERSION_AT_LEAST(3, 0, 0)
	// Buffer AlignedBuffer(int size)
	NODE_SET_METHOD(target, "AlignedBuffer", AlignedBuffer);
#endif
	NODE_SET_METHOD(target, "copy", PrepInput);
	NODE_SET_METHOD(target, "finish", Finish);
	
#ifdef _OPENMP
	// set_max_threads(int num_threads)
	NODE_SET_METHOD(target, "set_max_threads", SetMaxThreads);
#endif
	NODE_SET_METHOD(target, "get_num_threads", GetNumThreads);
	
	NODE_SET_METHOD(target, "set_method", SetMethod);
}

NODE_MODULE(parpar_gf, parpar_gf_init);
