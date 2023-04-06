
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <node_object_wrap.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include "../gf16/controller.h"
#include "../gf16/controller_cpu.h"
#include "../gf16/controller_ocl.h"
#include "../gf16/threadqueue.h"
#include "../hasher/hasher.h"


using namespace v8;

/*******************************************/

#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x
#define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
#define HANDLE_SCOPE HandleScope scope(isolate)
#define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HANDLE_SCOPE

# if NODE_VERSION_AT_LEAST(8, 0, 0)
#  define NEW_STRING(s) String::NewFromOneByte(isolate, (const uint8_t*)(s), NewStringType::kNormal).ToLocalChecked()
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)(e), NewStringType::kNormal).ToLocalChecked())); return; }
#  define ARG_TO_NUM(t, a) (a).As<t>()->Value()
#  define ARG_TO_OBJ(a) (a).As<Object>()
# else
#  define NEW_STRING(s) String::NewFromUtf8(isolate, s)
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, e))); return; }
#  define ARG_TO_NUM(t, a) (a)->To##t()->Value()
#  define ARG_TO_OBJ(a) (a)->ToObject()
# endif

#define RETURN_VAL(v) args.GetReturnValue().Set(v)
#define RETURN_UNDEF return;
#define RETURN_BUFFER RETURN_VAL
#define ISOLATE isolate,
#define NEW_OBJ(o) o::New(isolate)
#define BUFFER_TYPE Local<Object>
#define PERSIST_VALUE(p, v) p.Reset(isolate, v)
#define PERSIST_CLEAR(p) p.Reset()

#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define HANDLE_SCOPE HandleScope scope
#define FUNC_START HANDLE_SCOPE
#define NEW_STRING String::New
#define ARG_TO_NUM(t, a) (a)->To##t()->Value()
#define ARG_TO_OBJ(a) (a)->ToObject()
#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_BUFFER(v) RETURN_VAL(v->handle_)
#define RETURN_UNDEF RETURN_VAL( Undefined() );
#define ISOLATE
#define NEW_OBJ(o) o::New()
#define BUFFER_TYPE node::Buffer*
#define PERSIST_VALUE(p, v) p = Persistent<Value>::New(v)
#define PERSIST_CLEAR(p) p.Dispose()

#endif

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__).ToLocalChecked()
#else
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__)
#endif

#if NODE_VERSION_AT_LEAST(12, 0, 0)
# define OBJ_HAS(obj, key) (obj)->Has(isolate->GetCurrentContext(), NEW_STRING(key)).ToChecked()
# define GET_OBJ(obj, key) (obj)->Get(isolate->GetCurrentContext(), NEW_STRING(key)).ToLocalChecked()
# define SET_OBJ(obj, key, val) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), val).Check()
# define GET_ARR(obj, idx) (obj)->Get(isolate->GetCurrentContext(), idx).ToLocalChecked()
# define SET_ARR(obj, idx, val) (obj)->Set(isolate->GetCurrentContext(), idx, val).Check()
# define SET_OBJ_FUNC(obj, key, f) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), f->GetFunction(isolate->GetCurrentContext()).ToLocalChecked()).Check()
#else
# define OBJ_HAS(obj, key) (obj)->Has(NEW_STRING(key))
# define GET_OBJ(obj, key) (obj)->Get(NEW_STRING(key))
# define SET_OBJ(obj, key, val) (obj)->Set(NEW_STRING(key), val)
# define GET_ARR(obj, idx) (obj)->Get(idx)
# define SET_ARR(obj, idx, val) (obj)->Set(idx, val)
# define SET_OBJ_FUNC(obj, key, f) (obj)->Set(NEW_STRING(key), f->GetFunction())
#endif



#if NODE_VERSION_AT_LEAST(0, 11, 0)
// copied from node::GetCurrentEventLoop [https://github.com/nodejs/node/pull/17109]
static inline uv_loop_t* getCurrentLoop(Isolate* isolate, int) {
	/* -- don't have access to node::Environment :()
	Local<Context> context = isolate->GetCurrentContext();
	if(context.IsEmpty())
		return uv_default_loop();
	return node::Environment::GetCurrent(context)->event_loop();
	*/
	
# if NODE_VERSION_AT_LEAST(9, 3, 0) || (NODE_MAJOR_VERSION == 8 && NODE_MINOR_VERSION >= 10) || (NODE_MAJOR_VERSION == 6 && NODE_MINOR_VERSION >= 14)
	return node::GetCurrentEventLoop(isolate);
# endif
	return uv_default_loop();
}
#else
static inline uv_loop_t getCurrentLoop(int) {
	return uv_default_loop();
}
#endif


struct CallbackWrapper {
	CallbackWrapper() : hasCallback(false) {}
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate;
	// use BaseObject / AsyncWrap instead? meh
	explicit CallbackWrapper(Isolate* _isolate, const Local<Value>& callback) : isolate(_isolate) {
		attachCallback(_isolate, callback);
	}
#else
	CallbackWrapper(const Local<Value>& callback) {
		attachCallback(callback);
	}
#endif
	~CallbackWrapper() {
		PERSIST_CLEAR(value);
		detachCallback();
	};
	Persistent<Object> obj_;
	// persist copy of buffer for the duration of the job
	Persistent<Value> value;
	bool hasCallback;
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	void attachCallback(Isolate* _isolate, const Local<Value>& callback) {
		isolate = _isolate;
		Local<Object> obj = NEW_OBJ(Object);
		SET_OBJ(obj, "ondone", callback);
		obj_.Reset(ISOLATE obj);
		//if (env->in_domain())
		//	obj_->Set(env->domain_string(), env->domain_array()->Get(0));
		hasCallback = true;
	}
#else
	void attachCallback(const Local<Value>& callback) {
		obj_ = Persistent<Object>::New(NEW_OBJ(Object));
		obj_->Set(NEW_STRING("ondone"), callback);
		//SetActiveDomain(obj_); // never set in node_zlib.cc - perhaps domains aren't that important?
		hasCallback = true;
	}
#endif
	void detachCallback() {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		obj_.Reset();
#else
		//if (obj_.IsEmpty()) return;
		obj_.Dispose();
		obj_.Clear(); // TODO: why this line?
#endif
		hasCallback = false;
	}
	
	void attachValue(const Local<Value>& val) {
		PERSIST_VALUE(value, val);
	}
	void call(int argc, Local<Value>* argv) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		Local<Object> obj = Local<Object>::New(isolate, obj_);
# if NODE_VERSION_AT_LEAST(10, 0, 0)
		node::async_context ac;
		memset(&ac, 0, sizeof(ac));
		node::MakeCallback(isolate, obj, "ondone", argc, argv, ac);
# else
		node::MakeCallback(isolate, obj, "ondone", argc, argv);
# endif
#else
		node::MakeCallback(obj_, "ondone", argc, argv);
#endif
	}
	
	inline void call() {
		HANDLE_SCOPE;
		call(0, nullptr);
	}
	inline void call(const HandleScope&) {
		call(0, nullptr);
	}
	inline void call(std::initializer_list<Local<Value>> args) {
		std::vector<Local<Value>> argList(args);
		call(argList.size(), argList.data());
	}
};


struct GfOclSpec {
	int platformId, deviceId;
	size_t sliceOffset, sliceSize;
	
	Galois16OCLMethods method;
	unsigned inputGrouping, inputMinGrouping, targetIters, targetGrouping;
};
static bool load_ocl() {
	static bool oclLoaded = false;
	if(!oclLoaded) {
		if(PAR2ProcOCL::load_runtime()) {
			return false;
		}
		oclLoaded = true;
	}
	return true;
}

class GfProc : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1); // necessary for node::Object::Wrap
		
		NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
		NODE_SET_PROTOTYPE_METHOD(t, "freeMem", FreeMem);
		NODE_SET_PROTOTYPE_METHOD(t, "setRecoverySlices", SetRecoverySlices);
		NODE_SET_PROTOTYPE_METHOD(t, "setCurrentSliceSize", SetCurrentSliceSize);
		NODE_SET_PROTOTYPE_METHOD(t, "setNumThreads", SetNumThreads);
		NODE_SET_PROTOTYPE_METHOD(t, "setProgressCb", SetProgressCb);
		NODE_SET_PROTOTYPE_METHOD(t, "info", GetInfo);
		NODE_SET_PROTOTYPE_METHOD(t, "add", AddSlice);
		NODE_SET_PROTOTYPE_METHOD(t, "end", EndInput);
		NODE_SET_PROTOTYPE_METHOD(t, "get", GetOutputSlice);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 1)
			RETURN_ERROR("Slice size required");
		
		size_t sliceSize = (size_t)ARG_TO_NUM(Integer, args[0]);
		if(sliceSize < 2 || sliceSize & 1)
			RETURN_ERROR("Slice size is invalid");
		
		
		bool useCpu = true;
		int stagingAreas = 2;
		int cpuMethod = GF16_AUTO;
		unsigned cpuInputGrouping = 0, cpuInputMinGrouping = 0;
		size_t cpuChunkLen = 0;
		size_t cpuOffset = 0, cpuSliceSize = sliceSize;
#define ASSIGN_INT_VAL(prop, key, var, type) \
	if(OBJ_HAS(prop, key)) { \
		Local<Value> v = GET_OBJ(prop, key); \
		if(!v->IsUndefined() && !v->IsNull()) \
			var = ARG_TO_NUM(type, v); \
	}
		if(args.Length() >= 2) { // CPU processing props
			if(args[1]->IsNull()) {
				useCpu = false;
				cpuSliceSize = 0;
			} else {
				Local<Object> prop = ARG_TO_OBJ(args[1]);
				ASSIGN_INT_VAL(prop, "method", cpuMethod, Int32)
				// TODO: check validity of cpuMethod
				ASSIGN_INT_VAL(prop, "input_batchsize", cpuInputGrouping, Uint32)
				if(cpuInputGrouping > 32768)
					RETURN_ERROR("Input batchsize is invalid");
				ASSIGN_INT_VAL(prop, "input_minbatchsize", cpuInputMinGrouping, Uint32)
				ASSIGN_INT_VAL(prop, "chunk_size", cpuChunkLen, Uint32)
				ASSIGN_INT_VAL(prop, "slice_offset", cpuOffset, Integer)
				if(cpuOffset & 1 || cpuOffset > sliceSize)
					RETURN_ERROR("Invalid CPU slice offset");
				ASSIGN_INT_VAL(prop, "slice_size", cpuSliceSize, Integer)
				if(cpuSliceSize < 2 || cpuSliceSize & 1)
					RETURN_ERROR("CPU slice size must be a multiple of 2");
				if(cpuOffset+cpuSliceSize > sliceSize)
					RETURN_ERROR("CPU slice offset+size cannot exceed the slice size");
			}
		}
		std::vector<struct GfOclSpec> useOcl;
		if(args.Length() >= 3 && args[2]->IsArray()) { // OpenCL processing props
			Local<Array> props = Local<Array>::Cast(args[2]);
			for(unsigned i=0; i<props->Length(); i++) {
				Local<Object> prop = ARG_TO_OBJ(GET_ARR(props, i));
				struct GfOclSpec spec{-1, -1, 0, 0, GF16OCL_AUTO, 0, 0, 0, 0};
				// TODO: validate platform/device
				ASSIGN_INT_VAL(prop, "platform", spec.platformId, Int32)
				ASSIGN_INT_VAL(prop, "device", spec.deviceId, Int32)
				ASSIGN_INT_VAL(prop, "slice_size", spec.sliceSize, Integer)
				ASSIGN_INT_VAL(prop, "slice_offset", spec.sliceOffset, Integer)
				int method = 0;
				ASSIGN_INT_VAL(prop, "method", method, Int32)
				if(method) spec.method = (Galois16OCLMethods)method;
				// TODO: check validity of method
				ASSIGN_INT_VAL(prop, "input_batchsize", spec.inputGrouping, Int32)
				if(spec.inputGrouping > 32768)
					RETURN_ERROR("OpenCL input batchsize is invalid");
				ASSIGN_INT_VAL(prop, "input_minbatchsize", spec.inputMinGrouping, Int32)
				ASSIGN_INT_VAL(prop, "target_iters", spec.targetIters, Uint32)
				ASSIGN_INT_VAL(prop, "target_grouping", spec.targetGrouping, Uint32)
				if(spec.targetGrouping > 65535)
					RETURN_ERROR("OpenCL target grouping is invalid");
				useOcl.push_back(spec);
			}
		}
		if(args.Length() >= 4 && !args[3]->IsUndefined() && !args[3]->IsNull()) {
			stagingAreas = ARG_TO_NUM(Int32, args[3]);
			if(stagingAreas < 1 || stagingAreas > 32768)
				RETURN_ERROR("Staging area count is invalid");
		}
#undef ASSIGN_INT_VAL
		
		if(cpuInputGrouping * stagingAreas > 65536)
			RETURN_ERROR("Staging area too large");
		
		if(!useOcl.empty()) {
			if(!load_ocl()) {
				RETURN_ERROR("Could not load OpenCL runtime");
			}
		} else if(!useCpu) {
			RETURN_ERROR("At least the CPU or one OpenCL device must be enabled");
		}
		
		GfProc *self = new GfProc(sliceSize, stagingAreas, cpuOffset, cpuSliceSize, useOcl, getCurrentLoop(ISOLATE 0));
		size_t usedSliceSize = 0;
		if(useCpu && !self->init_cpu((Galois16Methods)cpuMethod, cpuInputGrouping, cpuChunkLen)) {
			delete self;
			RETURN_ERROR("Failed to allocate memory");
		}
		if(useCpu) self->par2cpu->setMinInputBatchSize(cpuInputMinGrouping);
		int oclI = 0;
		for(const auto& oclSpec : useOcl) {
			usedSliceSize += oclSpec.sliceSize;
			if(oclSpec.sliceSize == 0 || (oclSpec.sliceSize & 1)) {
				delete self;
				RETURN_ERROR("Invalid slice size allocated to OpenCL device");
			}
			if(oclSpec.sliceOffset & 1 || oclSpec.sliceOffset > sliceSize) {
				delete self;
				RETURN_ERROR("Invalid slice offset allocated to OpenCL device");
			}
			if(oclSpec.sliceOffset + oclSpec.sliceSize > sliceSize) {
				delete self;
				RETURN_ERROR("Invalid slice offset+size allocated to OpenCL device");
			}
			if(!self->init_ocl(oclI, oclSpec.method, oclSpec.inputGrouping, oclSpec.targetIters, oclSpec.targetGrouping)) {
				delete self;
				RETURN_ERROR("Failed to initialise OpenCL device"); // TODO: add device info
			}
			self->par2ocl[oclI]->setMinInputBatchSize(oclSpec.inputMinGrouping);
			oclI++;
		}
		if((useCpu && usedSliceSize >= sliceSize) || (!useCpu && usedSliceSize != sliceSize)) {
			delete self;
			RETURN_ERROR("Slice portions allocated to OpenCL devices is invalid");
		}
		
		self->Wrap(args.This());
	}
	
private:
	bool isRunning;
	bool isClosed;
	bool pendingDiscardOutput;
	bool hasOutput;
	CallbackWrapper progressCb;
	PAR2Proc par2;
	std::unique_ptr<PAR2ProcCPU> par2cpu;
	std::vector<std::unique_ptr<PAR2ProcOCL>> par2ocl;
	
	// disable copy constructor
	GfProc(const GfProc&);
	GfProc& operator=(const GfProc&);
	
protected:
	FUNC(Close) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot close whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() >= 1 && !args[0]->IsUndefined() && !args[1]->IsNull()) {
			if(!args[0]->IsFunction())
				RETURN_ERROR("First argument must be a callback");
			
			CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[0]));
			self->par2.deinit([cb]() {
				cb->call();
				delete cb;
			});
		} else {
			self->par2.deinit();
		}
		self->isClosed = true;
	}
	
	FUNC(FreeMem) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot free memory whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		self->par2.freeProcessingMem();
		self->hasOutput = false;
	}
	
	FUNC(SetCurrentSliceSize) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1)
			RETURN_ERROR("Argument required");
		
		size_t sliceSize = (size_t)ARG_TO_NUM(Integer, args[0]);
		if(sliceSize < 2 || sliceSize & 1)
			RETURN_ERROR("Slice size is invalid");
		
		// TODO: get slice allocation from input
		
		self->hasOutput = false;
		if(!self->par2.setCurrentSliceSize(sliceSize))
			RETURN_ERROR("Failed to allocate memory");
	}
	FUNC(SetRecoverySlices) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1 || !args[0]->IsArray())
			RETURN_ERROR("List of recovery indicies required");
		
		auto argOutputs = Local<Array>::Cast(args[0]);
		int numOutputs = (int)argOutputs->Length();
		if(numOutputs > 65534)
			RETURN_ERROR("Too many recovery indicies specified");
		if(numOutputs < 1)
			RETURN_ERROR("At least one recovery index must be supplied");
		
		std::vector<uint16_t> outputs(numOutputs);
		
		for(int i=0; i<numOutputs; i++) {
			Local<Value> output = GET_ARR(argOutputs, i);
			outputs[i] = ARG_TO_NUM(Uint32, output);
			if(outputs[i] > 65534)
				RETURN_ERROR("Invalid recovery index supplied");
		}
		
		self->hasOutput = false; // probably can be retained, but we'll pretend not for consistency's sake
		if(!self->par2.setRecoverySlices(outputs))
			RETURN_ERROR("Failed to allocate memory");
	}
	
	FUNC(SetNumThreads) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->par2cpu.get())
			RETURN_ERROR("CPU processing not enabled");
		
		if(args.Length() < 1)
			RETURN_ERROR("Integer required");
		self->par2cpu->setNumThreads(ARG_TO_NUM(Int32, args[0]));
		
		RETURN_VAL(Integer::New(ISOLATE self->par2cpu->getNumThreads()));
	}
	
	FUNC(SetProgressCb) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() >= 1) {
			if(!args[0]->IsFunction())
				RETURN_ERROR("Callback required");
			self->progressCb.attachCallback(ISOLATE args[0]);
		} else {
			self->progressCb.detachCallback();
		}
	}
	
	FUNC(GetInfo) {
		// num threads, method name
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->par2.getNumRecoverySlices())
			RETURN_ERROR("setRecoverySlices not yet called");
		
		Local<Object> ret = NEW_OBJ(Object);
		if(self->par2cpu.get()) {
			SET_OBJ(ret, "threads", Integer::New(ISOLATE self->par2cpu->getNumThreads()));
			SET_OBJ(ret, "method_desc", NEW_STRING(self->par2cpu->getMethodName()));
			SET_OBJ(ret, "chunk_size", Number::New(ISOLATE self->par2cpu->getChunkLen()));
			SET_OBJ(ret, "staging_count", Integer::New(ISOLATE self->par2cpu->getStagingAreas()));
			SET_OBJ(ret, "staging_size", Integer::New(ISOLATE self->par2cpu->getInputBatchSize()));
			SET_OBJ(ret, "alignment", Integer::New(ISOLATE self->par2cpu->getAlignment()));
			SET_OBJ(ret, "stride", Integer::New(ISOLATE self->par2cpu->getStride()));
			SET_OBJ(ret, "slice_mem", Number::New(ISOLATE self->par2cpu->getAllocSliceSize()));
			SET_OBJ(ret, "num_output_slices", Integer::New(ISOLATE self->par2cpu->getNumRecoverySlices()));
		}
		if(!self->par2ocl.empty()) {
			Local<Array> oclDevInfo = Array::New(ISOLATE self->par2ocl.size());
			int i = 0;
			for(const auto& proc : self->par2ocl) {
				const auto devInfo = proc->deviceInfo();
				Local<Object> oclInfo = NEW_OBJ(Object);
				SET_OBJ(oclInfo, "device_name", NEW_STRING(devInfo.name.c_str()));
				SET_OBJ(oclInfo, "method_desc", NEW_STRING(proc->getMethodName()));
				SET_OBJ(oclInfo, "staging_count", Integer::New(ISOLATE proc->getStagingAreas()));
				SET_OBJ(oclInfo, "staging_size", Integer::New(ISOLATE proc->getInputBatchSize()));
				SET_OBJ(oclInfo, "chunk_size", Number::New(ISOLATE proc->getChunkLen()));
				SET_OBJ(oclInfo, "output_chunks", Integer::New(ISOLATE proc->getOutputGrouping()));
				SET_OBJ(oclInfo, "slice_mem", Number::New(ISOLATE proc->getAllocSliceSize()));
				SET_OBJ(oclInfo, "num_output_slices", Integer::New(ISOLATE proc->getNumRecoverySlices()));
				SET_ARR(oclDevInfo, i++, oclInfo);
			}
			SET_OBJ(ret, "opencl_devices", oclDevInfo);
		}
		
		RETURN_VAL(ret);
	}
	
	FUNC(AddSlice) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->par2.getNumRecoverySlices())
			RETURN_ERROR("setRecoverySlices not yet called");
		
		if(args.Length() < 3)
			RETURN_ERROR("Requires 3 arguments");
		
		if(!node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Input buffer required");
		if(!args[2]->IsFunction())
			RETURN_ERROR("Callback required");
		
		int idx = ARG_TO_NUM(Int32, args[0]);
		if(idx < 0 || idx > 32767)
			RETURN_ERROR("Input index not valid");
		
		if(node::Buffer::Length(args[1]) > self->par2.getCurrentSliceSize())
			RETURN_ERROR("Input buffer too large");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[2]));
		cb->attachValue(args[1]);
		
		self->isRunning = true;
		self->hasOutput = false;
		if(self->pendingDiscardOutput) {
			self->pendingDiscardOutput = false;
			self->par2.discardOutput();
		}
		
		bool added = self->par2.addInput(
			node::Buffer::Data(args[1]), node::Buffer::Length(args[1]),
			idx, false, [ISOLATE cb, idx]() {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, idx), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(idx), buffer });
#endif
				delete cb;
			}
		);
		
		if(!added) {
			delete cb;
		}
		RETURN_VAL(Boolean::New(ISOLATE added));
	}
	
	FUNC(EndInput) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		// NOTE: it's possible to end without adding anything, so don't require !self->isRunning
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1 || !args[0]->IsFunction())
			RETURN_ERROR("Callback required");
		if(!self->par2.getNumRecoverySlices())
			RETURN_ERROR("setRecoverySlices not yet called");
		
		if(self->pendingDiscardOutput)
			self->par2.discardOutput();
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[0]));
		self->par2.endInput([cb, self]() {
			self->isRunning = false;
			self->hasOutput = true;
			self->pendingDiscardOutput = true;
			cb->call();
			delete cb;
		});
	}
	
	FUNC(GetOutputSlice) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot get output whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->hasOutput)
			RETURN_ERROR("No finalized output to retrieve");
		
		if(args.Length() < 3)
			RETURN_ERROR("Requires 3 arguments");
		
		if(!node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Output buffer required");
		if(!args[2]->IsFunction())
			RETURN_ERROR("Callback required");
		
		if(node::Buffer::Length(args[1]) < self->par2.getCurrentSliceSize())
			RETURN_ERROR("Output buffer too small");
		int idx = ARG_TO_NUM(Int32, args[0]);
		if(idx < 0 || idx >= self->par2.getNumRecoverySlices())
			RETURN_ERROR("Recovery index is not valid");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[2]));
		cb->attachValue(args[1]);
		
		self->par2.getOutput(
			idx,
			node::Buffer::Data(args[1]),
			[ISOLATE cb, idx](bool cksumValid) {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, idx), Boolean::New(cb->isolate, cksumValid), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(idx), Boolean::New(cksumValid), buffer });
#endif
				delete cb;
			}
		);
	}
	
	explicit GfProc(size_t sliceSize, int stagingAreas, size_t cpuOffset, size_t cpuSliceSize, std::vector<struct GfOclSpec> useOcl, uv_loop_t* loop)
	: ObjectWrap(), isRunning(false), isClosed(false), pendingDiscardOutput(true), hasOutput(false) {
		std::vector<struct PAR2ProcBackendAlloc> procs;
		for(const auto& spec : useOcl) {
			auto proc = new PAR2ProcOCL(loop, spec.platformId, spec.deviceId, stagingAreas);
			par2ocl.push_back(std::unique_ptr<PAR2ProcOCL>(proc));
			procs.push_back({static_cast<IPAR2ProcBackend*>(proc), spec.sliceOffset, spec.sliceSize});
		}
		if(cpuSliceSize) {
			par2cpu.reset(new PAR2ProcCPU(loop, stagingAreas));
			procs.push_back({static_cast<IPAR2ProcBackend*>(par2cpu.get()), cpuOffset, cpuSliceSize});
		}
		// TODO: handle init returning false (currently, it can't)
		par2.init(sliceSize, procs, [&](unsigned numInputs) {
			if(progressCb.hasCallback) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				HandleScope scope(progressCb.isolate);
				progressCb.call({ Integer::New(progressCb.isolate, numInputs) });
#else
				HandleScope scope;
				progressCb.call({ Integer::New(numInputs) });
#endif
			}
		});
	}
	
	bool init_cpu(Galois16Methods method, unsigned inputGrouping, size_t chunkLen) {
		return par2cpu->init(method, inputGrouping, chunkLen);
	}
	bool init_ocl(int idx, Galois16OCLMethods method, unsigned inputGrouping, unsigned targetIters, unsigned targetGrouping) {
		return par2ocl[idx]->init(method, inputGrouping, targetIters, targetGrouping);
	}
	
	~GfProc() {
		par2.deinit();
	}
};

FUNC(GfInfo) {
	FUNC_START;
	
	// get method
	Galois16Methods method = args.Length() >= 1 && !args[0]->IsUndefined() && !args[0]->IsNull() ? (Galois16Methods)ARG_TO_NUM(Int32, args[0]) : GF16_AUTO;
	
	if(method == GF16_AUTO) {
		// TODO: accept hints
		method = PAR2ProcCPU::default_method();
	}
	
	auto info = PAR2ProcCPU::info(method);
	Local<Object> ret = NEW_OBJ(Object);
	SET_OBJ(ret, "id", Integer::New(ISOLATE info.id));
	SET_OBJ(ret, "name", NEW_STRING(info.name));
	SET_OBJ(ret, "alignment", Integer::New(ISOLATE info.alignment));
	SET_OBJ(ret, "stride", Integer::New(ISOLATE info.stride));
	SET_OBJ(ret, "target_chunk", Integer::New(ISOLATE info.idealChunkSize));
	SET_OBJ(ret, "target_grouping", Integer::New(ISOLATE info.idealInputMultiple));
	
	RETURN_VAL(ret);
}

static void OclDeviceToJS(
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	  Isolate* isolate,
#endif
	  Local<Object>& dev, const GF16OCL_DeviceInfo& device) {
	dev = NEW_OBJ(Object);
	SET_OBJ(dev, "id", Integer::New(ISOLATE device.id));
	SET_OBJ(dev, "name", NEW_STRING(device.name.c_str()));
	SET_OBJ(dev, "vendor_id", Integer::New(ISOLATE device.vendorId));
	SET_OBJ(dev, "available", Boolean::New(ISOLATE device.available));
	SET_OBJ(dev, "supported", Boolean::New(ISOLATE device.supported));
	switch(device.type) {
		case CL_DEVICE_TYPE_DEFAULT: SET_OBJ(dev, "type", NEW_STRING("Default")); break;
		case CL_DEVICE_TYPE_CPU: SET_OBJ(dev, "type", NEW_STRING("CPU")); break;
		case CL_DEVICE_TYPE_GPU: SET_OBJ(dev, "type", NEW_STRING("GPU")); break;
		case CL_DEVICE_TYPE_ACCELERATOR: SET_OBJ(dev, "type", NEW_STRING("Accelerator")); break;
		case CL_DEVICE_TYPE_CUSTOM: SET_OBJ(dev, "type", NEW_STRING("Custom")); break;
		default: SET_OBJ(dev, "type", NEW_STRING("Unknown")); // TODO: support multi-types?
	}
	SET_OBJ(dev, "memory_global", Number::New(ISOLATE device.memory));
	SET_OBJ(dev, "cache_global", Number::New(ISOLATE device.globalCache));
	SET_OBJ(dev, "memory_unified", Boolean::New(ISOLATE device.unifiedMemory));
	SET_OBJ(dev, "memory_constant", Number::New(ISOLATE device.constantMemory));
	if(!device.localMemoryIsGlobal || device.memory != device.localMemory)
		SET_OBJ(dev, "memory_local", Number::New(ISOLATE device.localMemory));
	SET_OBJ(dev, "memory_max_alloc", Number::New(ISOLATE device.maxAllocation));
	SET_OBJ(dev, "workgroup_limit", Integer::New(ISOLATE device.maxWorkGroup));
	SET_OBJ(dev, "workgroup_multiple", Integer::New(ISOLATE device.workGroupMultiple));
	SET_OBJ(dev, "compute_units", Integer::New(ISOLATE device.computeUnits));
}

FUNC(OclDevices) {
	FUNC_START;
	
	if(!load_ocl()) {
		RETURN_ERROR("Could not load OpenCL runtime");
	}
	
	const auto platforms = PAR2ProcOCL::getPlatforms();
	Local<Array> ret = Array::New(ISOLATE platforms.size());
	for(unsigned pf=0; pf<platforms.size(); pf++) {
		const auto devices = PAR2ProcOCL::getDevices(pf);
		Local<Array> retDevs = Array::New(ISOLATE devices.size());
		for(unsigned dv=0; dv<devices.size(); dv++) {
			Local<Object> dev;
			OclDeviceToJS(ISOLATE dev, devices[dv]);
			SET_ARR(retDevs, dv, dev);
		}
		
		Local<Object> retPlat = NEW_OBJ(Object);
		SET_OBJ(retPlat, "id", Integer::New(ISOLATE pf));
		SET_OBJ(retPlat, "name", NEW_STRING(platforms[pf].c_str()));
		SET_OBJ(retPlat, "devices", retDevs);
		SET_ARR(ret, pf, retPlat);
	}
	RETURN_VAL(ret);
}

FUNC(OclDeviceInfo) {
	FUNC_START;
	
	if(args.Length() < 2)
		RETURN_ERROR("Requires 2 arguments");
	
	int platformId = ARG_TO_NUM(Int32, args[0]);
	int deviceId = ARG_TO_NUM(Int32, args[1]);
	
	if(!load_ocl()) {
		RETURN_ERROR("Could not load OpenCL runtime");
	}
	
	if(platformId == -1) platformId = PAR2ProcOCL::defaultPlatformId();
	if(platformId < 0)
		RETURN_UNDEF;
	
	const auto device = PAR2ProcOCL::getDevice(platformId, deviceId);
	if(!device.vendorId) // invalid device
		RETURN_UNDEF;
	
	Local<Object> dev;
	OclDeviceToJS(ISOLATE dev, device);
	SET_OBJ(dev, "platform_id", Integer::New(ISOLATE platformId));
	RETURN_VAL(dev);
}



class HasherInput;
static std::vector<MessageThread*> HasherInputThreadPool;
struct input_blockHash {
	uint64_t size;
	uint64_t pos;
	int count;
	char* ptr;
};
struct input_work_data {
	IHasherInput* hasher;
	const void* buffer;
	size_t len;
	struct input_blockHash* bh;
	CallbackWrapper* cb;
	HasherInput* self;
};
class HasherInput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "end", End);
		NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 2 || !node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Requires a size and buffer");

		// grab slice size + buffer to write hashes into
		double sliceSize = 0; // double ensures enough range even if int is 32-bit
#if NODE_VERSION_AT_LEAST(8, 0, 0)
		sliceSize = args[0].As<Number>()->Value();
#else
		sliceSize = args[0]->NumberValue();
#endif
		
		HasherInput *self = new HasherInput(getCurrentLoop(ISOLATE 0));
		
		self->bh.size = (uint64_t)sliceSize;
		self->bh.pos = 0;
		self->bh.count = node::Buffer::Length(args[1]) / 20;
		self->bh.ptr = node::Buffer::Data(args[1]);
		PERSIST_VALUE(self->ifscData, args[1]);
		
		self->Wrap(args.This());
	}
	
private:
	IHasherInput* hasher;
	uv_loop_t* loop;
	int queueCount;
	
	std::unique_ptr<MessageThread> thread;
	uv_async_t threadSignal;
	ThreadMessageQueue<struct input_work_data*> hashesDone;
	
	struct input_blockHash bh;
	Persistent<Value> ifscData;
	
	// disable copy constructor
	HasherInput(const HasherInput&);
	HasherInput& operator=(const HasherInput&);
	
protected:
	FUNC(Reset) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->queueCount)
			RETURN_ERROR("Cannot reset whilst running");
		
		self->hasher->reset();
	}
	
	static void thread_func(ThreadMessageQueue<void*>& q) {
		struct input_work_data* data;
		while((data = static_cast<struct input_work_data*>(q.pop())) != NULL) {
			char* src_ = (char*)data->buffer;
			size_t len = data->len;
			// feed initial part
			uint64_t blockLeft = data->bh->size - data->bh->pos;
			while(len >= blockLeft) {
				data->hasher->update(src_, blockLeft);
				src_ += blockLeft;
				len -= blockLeft;
				blockLeft = data->bh->size;
				data->bh->pos = 0;
				
				if(data->bh->count) {
					data->hasher->getBlock(data->bh->ptr, 0);
					data->bh->ptr += 20;
					data->bh->count--;
				} // else there's an overflow
			}
			if(len) data->hasher->update(src_, len);
			data->bh->pos += len;
			
			
			// signal main thread that hashing has completed
			data->self->hashesDone.push(data);
			uv_async_send(&(data->self->threadSignal));
		}
	}
	void after_process() {
		struct input_work_data* data;
		while(hashesDone.trypop(&data)) {
			static_cast<HasherInput*>(data->self)->queueCount--;
			data->cb->call();
			delete data->cb;
			delete data;
		}
	}
	
	inline void thread_send(struct input_work_data* data) {
		if(thread == nullptr) {
			if(HasherInputThreadPool.empty()) {
				thread.reset(new MessageThread(thread_func));
				thread->name = "par2_hash_input";
			} else {
				thread.reset(HasherInputThreadPool.back());
				HasherInputThreadPool.pop_back();
			}
		}
		thread->send(data);
	}
	
	FUNC(Update) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		// TODO: consider queueing mechanism; for now, require JS to do the queueing
		if(!self->hasher)
			RETURN_ERROR("Process already ended");
		
		if(args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !args[1]->IsFunction())
			RETURN_ERROR("Requires a buffer and callback");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[1]));
		cb->attachValue(args[0]);
		
		self->queueCount++;
		
		struct input_work_data* data = new struct input_work_data;
		data->cb = cb;
		data->hasher = self->hasher;
		data->buffer = node::Buffer::Data(args[0]);
		data->len = node::Buffer::Length(args[0]);
		data->self = self;
		data->bh = &self->bh;
		self->thread_send(data);
	}
	
	void deinit() {
		if(!hasher) return;
		hasher->destroy();
		if(thread != nullptr)
			HasherInputThreadPool.push_back(thread.release());
		uv_close(reinterpret_cast<uv_handle_t*>(&threadSignal), nullptr);
		hasher = nullptr;
		
		PERSIST_CLEAR(ifscData);
	}
	
	FUNC(End) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->queueCount)
			RETURN_ERROR("Process currently active");
		if(!self->hasher)
			RETURN_ERROR("Process already ended");
		
		if(args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("Requires a buffer");
		if(node::Buffer::Length(args[0]) < 16)
			RETURN_ERROR("Buffer must be at least 16 bytes long");
		
		// finish block hashes
		if(self->bh.count)
			// TODO: as zero padding can be slow, consider way of doing it in separate thread to not lock this one
			self->hasher->getBlock(self->bh.ptr, self->bh.size - self->bh.pos);
		
		char* result = (char*)node::Buffer::Data(args[0]);
		self->hasher->end(result);
		
		// clean up everything
		self->deinit();
	}
	
	explicit HasherInput(uv_loop_t* _loop) : ObjectWrap(), loop(_loop), queueCount(0), thread(nullptr) {
		hasher = HasherInput_Create();
		uv_async_init(loop, &threadSignal, [](uv_async_t *handle) {
			static_cast<HasherInput*>(handle->data)->after_process();
		});
		threadSignal.data = static_cast<void*>(this);
	}
	
	~HasherInput() {
		// TODO: if active, cancel thread?
		deinit();
	}
};

FUNC(HasherInputClear) {
	FUNC_START;
	for(auto thread : HasherInputThreadPool)
		delete thread;
	HasherInputThreadPool.clear();
}

class HasherOutput;
struct output_work_data {
	MD5Multi* hasher;
	const void* const* buffer;
	size_t len;
	CallbackWrapper* cb;
	HasherOutput* self;
};
class HasherOutput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "get", Get);
		NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 1)
			RETURN_ERROR("Number of regions required");
		unsigned regions = ARG_TO_NUM(Int32, args[0]);
		if(regions < 1 || regions > 65534)
			RETURN_ERROR("Invalid number of regions specified");
		
		HasherOutput *self = new HasherOutput(regions, getCurrentLoop(ISOLATE 0));
		self->Wrap(args.This());
	}
	
private:
	MD5Multi hasher;
	uv_loop_t* loop;
	int numRegions;
	bool isRunning;
	std::vector<const void*> buffers;
	
	// disable copy constructor
	HasherOutput(const HasherOutput&);
	HasherOutput& operator=(const HasherOutput&);
	
protected:
	FUNC(Reset) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot reset whilst running");
		
		self->hasher.reset();
	}
	
	static void do_update(uv_work_t *req) {
		struct output_work_data* data = static_cast<struct output_work_data*>(req->data);
		data->hasher->update(data->buffer, data->len);
	}
	static void after_update(uv_work_t *req, int status) {
		assert(status == 0);
		
		struct output_work_data* data = static_cast<struct output_work_data*>(req->data);
		data->self->isRunning = false;
		data->cb->call();
		delete data->cb;
		delete data;
		delete req;
	}
	
	FUNC(Update) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process already active");
		
		if(args.Length() < 1 || !args[0]->IsArray())
			RETURN_ERROR("Requires an array of buffers");
		
		
		// check array of buffers
		int numBufs = Local<Array>::Cast(args[0])->Length();
		if(numBufs != self->numRegions)
			RETURN_ERROR("Invalid number of array items given");
		
		Local<Object> oBufs = ARG_TO_OBJ(args[0]);
		size_t bufLen = 0;
		for(int i = 0; i < numBufs; i++) {
			Local<Value> buffer = GET_ARR(oBufs, i);
			if (!node::Buffer::HasInstance(buffer))
				RETURN_ERROR("All inputs must be Buffers");
			self->buffers[i] = static_cast<const void*>(node::Buffer::Data(buffer));
			
			size_t currentLen = node::Buffer::Length(buffer);
			if(i) {
				if (currentLen != bufLen)
					RETURN_ERROR("All inputs' length must be equal");
			} else {
				bufLen = currentLen;
			}
		}
		
		if(args.Length() > 1) {
			if(!args[1]->IsFunction())
				RETURN_ERROR("Second argument must be a callback");
			
			CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[1]));
			cb->attachValue(args[0]);
			
			self->isRunning = true;
			
			uv_work_t* req = new uv_work_t;
			struct output_work_data* data = new struct output_work_data;
			data->cb = cb;
			data->hasher = &(self->hasher);
			data->buffer = self->buffers.data();
			data->len = bufLen;
			data->self = self;
			req->data = data;
			uv_queue_work(self->loop, req, do_update, after_update);
		} else {
			self->hasher.update(self->buffers.data(), bufLen);
		}
	}
	
	FUNC(Get) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		if(args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("Requires a buffer");
		
		if(node::Buffer::Length(args[0]) < (unsigned)self->numRegions*16)
			RETURN_ERROR("Buffer must be large enough to hold all hashes");
		
		char* result = (char*)node::Buffer::Data(args[0]);
		self->hasher.end();
		self->hasher.get(result);
	}
	
	explicit HasherOutput(unsigned regions, uv_loop_t* _loop) : ObjectWrap(), hasher(regions), loop(_loop), numRegions(regions), isRunning(false), buffers(regions) {
		// TODO: consider multi-threaded hashing
	}
	
	~HasherOutput() {
		// TODO: if isRunning, cancel
	}
};

FUNC(SetHasherInput) {
	FUNC_START;
	
	if(args.Length() < 1)
		RETURN_ERROR("Method required");

	HasherInputMethods method = (HasherInputMethods)ARG_TO_NUM(Int32, args[0]);
	RETURN_VAL(Boolean::New(ISOLATE set_hasherInput(method)));
}
FUNC(SetHasherOutput) {
	FUNC_START;
	
	if(args.Length() < 1)
		RETURN_ERROR("Method required");

	MD5MultiLevels level = (MD5MultiLevels)ARG_TO_NUM(Int32, args[0]);
	set_hasherMD5MultiLevel(level);
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
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate = target->GetIsolate();
#endif
	HANDLE_SCOPE;
	Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE GfProc::New);
	GfProc::AttachMethods(t);
	SET_OBJ_FUNC(target, "GfProc", t);
	
	NODE_SET_METHOD(target, "gf_info", GfInfo);
	NODE_SET_METHOD(target, "opencl_devices", OclDevices);
	NODE_SET_METHOD(target, "opencl_device_info", OclDeviceInfo);
	
	t = FunctionTemplate::New(ISOLATE HasherInput::New);
	HasherInput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherInput", t);
	
	NODE_SET_METHOD(target, "hasher_clear", HasherInputClear);
	
	t = FunctionTemplate::New(ISOLATE HasherOutput::New);
	HasherOutput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherOutput", t);
	
	NODE_SET_METHOD(target, "set_HasherInput", SetHasherInput);
	NODE_SET_METHOD(target, "set_HasherOutput", SetHasherOutput);
	
	setup_hasher();
}

NODE_MODULE(parpar_gf, parpar_gf_init);
