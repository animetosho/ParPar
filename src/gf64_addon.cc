#include "hedley.h"

#ifdef HEDLEY_GCC_VERSION
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#ifdef HEDLEY_GCC_VERSION
 #pragma GCC diagnostic pop
#endif
#include <v8.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <js_native_api.h>
#include <node_api.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include "../gf64/gf64_global.h"

using namespace v8;

class Gf64EncoderWrapper {
public:
	GF64Method method;
	static int dispatch_initialized;

	explicit Gf64EncoderWrapper(GF64Method m) : method(m) {
		if (!dispatch_initialized) {
			gf64_init_dispatch();
			dispatch_initialized = 1;
		}
	}

	inline void Mul(uint64_t* out, const uint64_t* in, size_t len, uint64_t constant) {
		gf64_region_mul(out, in, len, constant);
	}

	inline void MulArr(uint64_t* out, const uint64_t* in, const uint64_t* coeff, size_t len, size_t n_coeff) {
		gf64_region_mul_arr(out, in, coeff, len, n_coeff);
	}
};

int Gf64EncoderWrapper::dispatch_initialized = 0;

static void Gf64EncoderWrapper_Finalize(const napi_env env, void* data, void* hint) {
	Gf64EncoderWrapper* enc = static_cast<Gf64EncoderWrapper*>(data);
	if(enc != NULL) {
		delete enc;
	}
}

static napi_value gf64_info_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 1;
	napi_value args[1];
	napi_value this_arg;

	status = napi_get_cb_info(env, info, &argc, args, &this_arg, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	int method = 0;
	if(argc >= 1) {
		napi_valuetype valuetype;
		status = napi_typeof(env, args[0], &valuetype);
		if(status == napi_ok && valuetype != napi_undefined) {
			status = napi_get_value_int32(env, args[0], &method);
			if(status != napi_ok) {
				method = 0;
			}
		}
	}

	if(method == 0) {
		method = (int)gf64_detect_method();
	}

	if(method < 0 || method > 3)
		method = 3;

	napi_value ret;
	status = napi_create_object(env, &ret);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create object");
		return NULL;
	}

	const char* methodNames[] = {"AVX512", "AVX2", "SSSE3", "SCALAR"};

	napi_value method_val;
	status = napi_create_int32(env, method, &method_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create int32");
		return NULL;
	}
	status = napi_set_named_property(env, ret, "method", method_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set method property");
		return NULL;
	}

	napi_value name_val;
	status = napi_create_string_utf8(env, methodNames[method], NAPI_AUTO_LENGTH, &name_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create string");
		return NULL;
	}
	status = napi_set_named_property(env, ret, "name", name_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set name property");
		return NULL;
	}

	napi_value alignment_val;
	status = napi_create_int32(env, 64, &alignment_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create alignment int32");
		return NULL;
	}
	status = napi_set_named_property(env, ret, "alignment", alignment_val);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set alignment property");
		return NULL;
	}

	return ret;
}

static napi_value Gf64Encoder_NAPI_constructor(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 1;
	napi_value args[1];
	napi_value this_arg;

	status = napi_get_cb_info(env, info, &argc, args, &this_arg, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	int method = 0;
	if(argc >= 1) {
		status = napi_get_value_int32(env, args[0], &method);
		if(status != napi_ok) {
			method = 0;
		}
	}

	if(method < 0 || method > 3) {
		method = 3;
	}

	gf64_init_dispatch();

	Gf64EncoderWrapper* enc = new Gf64EncoderWrapper((GF64Method)method);

	status = napi_wrap(env, this_arg, enc, Gf64EncoderWrapper_Finalize, NULL, NULL);
	if(status != napi_ok) {
		delete enc;
		napi_throw_error(env, NULL, "Failed to wrap encoder");
		return NULL;
	}

	return this_arg;
}

static napi_value Gf64Encoder_NAPI_mul(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 4;
	napi_value args[4];
	napi_value this_arg;

	status = napi_get_cb_info(env, info, &argc, args, &this_arg, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 4) {
		napi_throw_type_error(env, NULL, "Requires out, in, len, and constant");
		return NULL;
	}

	Gf64EncoderWrapper* enc = NULL;
	status = napi_unwrap(env, this_arg, (void**)&enc);
	if(status != napi_ok || enc == NULL) {
		napi_throw_error(env, NULL, "Invalid encoder");
		return NULL;
	}

	uint64_t* out = NULL;
	size_t outLen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&out, &outLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Output buffer required");
		return NULL;
	}

	uint64_t* in = NULL;
	size_t inLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&in, &inLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Input buffer required");
		return NULL;
	}

	int64_t len = 0;
	status = napi_get_value_int64(env, args[2], &len);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Length must be an integer");
		return NULL;
	}

	uint64_t constant = 0;
	status = napi_get_value_int64(env, args[3], (int64_t*)&constant);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Constant must be a uint64");
		return NULL;
	}

	enc->Mul(out, in, (size_t)len, constant);

	return NULL;
}

static napi_value Gf64Encoder_NAPI_mul_arr(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 5;
	napi_value args[5];
	napi_value this_arg;

	status = napi_get_cb_info(env, info, &argc, args, &this_arg, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 5) {
		napi_throw_type_error(env, NULL, "Requires out, in, coeff, len, n_coeff");
		return NULL;
	}

	Gf64EncoderWrapper* enc = NULL;
	status = napi_unwrap(env, this_arg, (void**)&enc);
	if(status != napi_ok || enc == NULL) {
		napi_throw_error(env, NULL, "Invalid encoder");
		return NULL;
	}

	uint64_t* out = NULL;
	size_t outLen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&out, &outLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Output buffer required");
		return NULL;
	}

	uint64_t* in = NULL;
	size_t inLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&in, &inLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Input buffer required");
		return NULL;
	}

	uint64_t* coeff = NULL;
	size_t coeffLen = 0;
	status = napi_get_buffer_info(env, args[2], (void**)&coeff, &coeffLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Coefficients buffer required");
		return NULL;
	}
	coeffLen /= sizeof(uint64_t);

	int64_t len = 0;
	status = napi_get_value_int64(env, args[3], &len);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Length must be an integer");
		return NULL;
	}

	int64_t n_coeff = 0;
	status = napi_get_value_int64(env, args[4], &n_coeff);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Num coefficients must be an integer");
		return NULL;
	}

	enc->MulArr(out, in, coeff, (size_t)len, (size_t)n_coeff);

	return NULL;
}

napi_value parpar_gf64_init_NAPI(napi_env env, napi_value exports) {
	napi_status status;

	napi_property_descriptor properties[] = {
		{ "mul", NULL, Gf64Encoder_NAPI_mul, NULL, NULL, NULL, napi_default, NULL },
		{ "mul_arr", NULL, Gf64Encoder_NAPI_mul_arr, NULL, NULL, NULL, napi_default, NULL }
	};

	napi_value constructor;
	status = napi_define_class(env,
		"Gf64Encoder",
		NAPI_AUTO_LENGTH,
		Gf64Encoder_NAPI_constructor,
		NULL,
		2,
		properties,
		&constructor);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to define Gf64Encoder class");
		return NULL;
	}

	status = napi_set_named_property(env, exports, "Gf64Encoder", constructor);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set Gf64Encoder constructor");
		return NULL;
	}

	napi_value gf64_info_fn;
	status = napi_create_function(env, NULL, 0, gf64_info_NAPI, NULL, &gf64_info_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create gf64_info function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "gf64_info", gf64_info_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set gf64_info property");
		return NULL;
	}

	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, parpar_gf64_init_NAPI)