#include "hedley.h"

#ifdef HEDLEY_GCC_VERSION
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <js_native_api.h>
#include <node_api.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include "gf64_global.h"
#include "par3_engine.h"

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

static void Gf64EncoderWrapper_Finalize(napi_env__* env, void* data, void* hint) {
	Gf64EncoderWrapper* enc = static_cast<Gf64EncoderWrapper*>(data);
	if(enc != NULL) {
		delete enc;
	}
}

static void Gf64EncoderWrapper_Finalize_Trampoline(const napi_env__* env, void* data, void* hint) {
	Gf64EncoderWrapper_Finalize(const_cast<napi_env__*>(env), data, hint);
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

	status = napi_wrap(env, this_arg, enc, Gf64EncoderWrapper_Finalize_Trampoline, NULL, NULL);
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

// Factory function for par3gen.js compatibility
static napi_value Gf64Encoder_NAPI_create(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 2;
	napi_value args[2];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
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

	int numThreads = 0;
	if(argc >= 2) {
		status = napi_get_value_int32(env, args[1], &numThreads);
		if(status != napi_ok) {
			numThreads = 0;
		}
	}

	gf64_init_dispatch();

	Gf64EncoderWrapper* enc = new Gf64EncoderWrapper((GF64Method)method);
	(void)numThreads; // Currently unused but part of API

	napi_value result;
	// Don't set a finalizer here - Gf64Encoder_NAPI_destroy is solely responsible for cleanup
	// to avoid double-free when destroy is explicitly called
	status = napi_create_external(env, enc, NULL, NULL, &result);
	if(status != napi_ok) {
		delete enc;
		napi_throw_error(env, NULL, "Failed to create external");
		return NULL;
	}

	return result;
}

static napi_value Gf64Encoder_NAPI_destroy(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 1;
	napi_value args[1];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	void* data = NULL;
	status = napi_get_value_external(env, args[0], &data);
	if(status != napi_ok || data == NULL) {
		napi_throw_error(env, NULL, "Invalid external value");
		return NULL;
	}

	// Just delete directly - Finalize would do the same
	delete (Gf64EncoderWrapper*)data;

	return NULL;
}

extern "C" int gf64_solve(gf64_t* A, gf64_t* b, gf64_t* x, size_t n);

static napi_value gf64_solve_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 3;
	napi_value args[3];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 3) {
		napi_throw_type_error(env, NULL, "Requires A, b, and n");
		return NULL;
	}

	gf64_t* A = NULL;
	size_t ALen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&A, &ALen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Matrix A buffer required");
		return NULL;
	}

	gf64_t* b = NULL;
	size_t bLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&b, &bLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "Vector b buffer required");
		return NULL;
	}

	int32_t n = 0;
	status = napi_get_value_int32(env, args[2], &n);
	if(status != napi_ok || n <= 0) {
		napi_throw_type_error(env, NULL, "Dimension n must be positive integer");
		return NULL;
	}

	size_t nSize = (size_t)n;
	if(ALen < nSize * nSize * sizeof(gf64_t)) {
		napi_throw_error(env, NULL, "Matrix A buffer too small");
		return NULL;
	}
	if(bLen < nSize * sizeof(gf64_t)) {
		napi_throw_error(env, NULL, "Vector b buffer too small");
		return NULL;
	}

	gf64_t* x = (gf64_t*)malloc(nSize * sizeof(gf64_t));
	if(!x) {
		napi_throw_error(env, NULL, "Failed to allocate solution vector");
		return NULL;
	}

	gf64_init_dispatch();

	int result = gf64_solve(A, b, x, nSize);

	napi_value result_val;
	if(result == 0) {
		status = napi_create_buffer_copy(env, nSize * sizeof(gf64_t), x, NULL, &result_val);
	} else {
		status = napi_get_undefined(env, &result_val);
	}

	free(x);

	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create result");
		return NULL;
	}

	return result_val;
}

// Helper: extract uint64 from napi_value (accepts Number or BigInt).
// Extract uint64 from napi_value (Number or BigInt).
// Tries BigInt first (via napi_get_value_bigint_uint64), then falls back to Number.
// Returns napi_ok on success, napi_generic_failure if neither type.
static napi_status get_uint64_from_value(napi_env env, napi_value val, uint64_t* result) {
	napi_status status;
	napi_valuetype valuetype;

	status = napi_typeof(env, val, &valuetype);
	if(status != napi_ok) return status;

	if(valuetype == napi_bigint) {
		bool lossless = false;
		status = napi_get_value_bigint_uint64(env, val, result, &lossless);
		if(status == napi_ok) return status;
		// Fall through to Number attempt (rare — only if BigInt conversion fails)
	}

	int64_t tmp;
	status = napi_get_value_int64(env, val, &tmp);
	if(status == napi_ok) {
		*result = (uint64_t)tmp;
		return napi_ok;
	}

	return napi_generic_failure;
}

static napi_value ComputeRecovery_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 8;
	napi_value args[8];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 7) {
		napi_throw_type_error(env, NULL, "Requires inputs, outputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery [, numThreads]");
		return NULL;
	}

	// Extract buffer args
	gf64_t* inputs = NULL;
	size_t inputsLen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&inputs, &inputsLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "inputs must be a Buffer");
		return NULL;
	}

	gf64_t* outputs = NULL;
	size_t outputsLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&outputs, &outputsLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "outputs must be a Buffer");
		return NULL;
	}

	// Extract integer args
	int32_t numInputs = 0;
	status = napi_get_value_int32(env, args[2], &numInputs);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "numInputs must be an integer");
		return NULL;
	}

	int32_t numRecovery = 0;
	status = napi_get_value_int32(env, args[3], &numRecovery);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "numRecovery must be an integer");
		return NULL;
	}

	int64_t blockSize = 0;
	status = napi_get_value_int64(env, args[4], &blockSize);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "blockSize must be an integer");
		return NULL;
	}

	// Parse firstInput (Number or BigInt)
	uint64_t firstInput = 0;
	status = get_uint64_from_value(env, args[5], &firstInput);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "firstInput must be a Number or BigInt");
		return NULL;
	}

	// Parse firstRecovery (Number or BigInt)
	uint64_t firstRecovery = 0;
	status = get_uint64_from_value(env, args[6], &firstRecovery);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "firstRecovery must be a Number or BigInt");
		return NULL;
	}

	// Parse numThreads (optional, defaults to 0 = auto)
	int32_t numThreads = 0;
	if(argc >= 8) {
		status = napi_get_value_int32(env, args[7], &numThreads);
		if(status != napi_ok) {
			napi_throw_type_error(env, NULL, "numThreads must be an integer");
			return NULL;
		}
	}

	// Validation
	if(numInputs <= 0) {
		napi_throw_range_error(env, NULL, "numInputs must be positive");
		return NULL;
	}

	if(numRecovery <= 0) {
		napi_throw_range_error(env, NULL, "numRecovery must be positive");
		return NULL;
	}

	if(blockSize <= 0 || blockSize % 8 != 0) {
		napi_throw_range_error(env, NULL, "blockSize must be positive and a multiple of 8");
		return NULL;
	}

	if(inputsLen < (size_t)(numInputs * blockSize)) {
		napi_throw_range_error(env, NULL, "inputs buffer too small for numInputs * blockSize");
		return NULL;
	}

	if(outputsLen < (size_t)(numRecovery * blockSize)) {
		napi_throw_range_error(env, NULL, "outputs buffer too small for numRecovery * blockSize");
		return NULL;
	}

	// Convert blockSize (bytes) to blockSize64 (64-bit words)
	size_t blockSize64 = (size_t)(blockSize / 8);

	// Call the engine
	gf64_init_dispatch();
	GF64Controller::ComputeRecoveryBlocks(
		inputs, (size_t)numInputs,
		outputs, (size_t)numRecovery,
		blockSize64,
		firstInput, firstRecovery,
		(int)numThreads
	);

	return NULL;
}

// ComputeRepairBlocks NAPI binding
// Args: availBlocks, repairedBlocks, numAvail, numMissing, blockSize, solveMatrix [, numThreads]
static napi_value ComputeRepair_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 7;
	napi_value args[7];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 6) {
		napi_throw_type_error(env, NULL, "Requires availBlocks, repairedBlocks, numAvail, numMissing, blockSize, solveMatrix [, numThreads]");
		return NULL;
	}

	gf64_t* availBlocks = NULL;
	size_t availLen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&availBlocks, &availLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "availBlocks must be a Buffer");
		return NULL;
	}

	gf64_t* repairedBlocks = NULL;
	size_t repairedLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&repairedBlocks, &repairedLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "repairedBlocks must be a Buffer");
		return NULL;
	}

	int32_t numAvail = 0;
	status = napi_get_value_int32(env, args[2], &numAvail);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "numAvail must be an integer");
		return NULL;
	}

	int32_t numMissing = 0;
	status = napi_get_value_int32(env, args[3], &numMissing);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "numMissing must be an integer");
		return NULL;
	}

	int64_t blockSize = 0;
	status = napi_get_value_int64(env, args[4], &blockSize);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "blockSize must be an integer");
		return NULL;
	}

	gf64_t* solveMatrix = NULL;
	size_t solveLen = 0;
	status = napi_get_buffer_info(env, args[5], (void**)&solveMatrix, &solveLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "solveMatrix must be a Buffer");
		return NULL;
	}

	int32_t numThreads = 0;
	if(argc >= 7) {
		status = napi_get_value_int32(env, args[6], &numThreads);
		if(status != napi_ok) {
			numThreads = 0;
		}
	}

	if(numAvail <= 0 || numMissing <= 0) {
		napi_throw_range_error(env, NULL, "numAvail and numMissing must be positive");
		return NULL;
	}

	if(blockSize <= 0 || blockSize % 8 != 0) {
		napi_throw_range_error(env, NULL, "blockSize must be positive and a multiple of 8");
		return NULL;
	}

	size_t blockSize64 = (size_t)(blockSize / 8);

	if(availLen < (size_t)(numAvail * blockSize)) {
		napi_throw_range_error(env, NULL, "availBlocks buffer too small");
		return NULL;
	}

	if(repairedLen < (size_t)(numMissing * blockSize)) {
		napi_throw_range_error(env, NULL, "repairedBlocks buffer too small");
		return NULL;
	}

	if(solveLen < (size_t)(numMissing * numAvail * sizeof(gf64_t))) {
		napi_throw_range_error(env, NULL, "solveMatrix buffer too small");
		return NULL;
	}

	gf64_init_dispatch();
	GF64Controller::ComputeRepairBlocks(
		availBlocks, (size_t)numAvail,
		repairedBlocks, (size_t)numMissing,
		solveMatrix, blockSize64,
		(int)numThreads
	);

	return NULL;
}

static napi_value SolveAndReconstruct_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 4;
	napi_value args[4];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 4) {
		napi_throw_type_error(env, NULL, "Requires A, rhsBlocks, n, blockSize");
		return NULL;
	}

	gf64_t* A = NULL;
	size_t ALen = 0;
	status = napi_get_buffer_info(env, args[0], (void**)&A, &ALen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "A must be a Buffer");
		return NULL;
	}

	gf64_t* rhsBlocks = NULL;
	size_t rhsLen = 0;
	status = napi_get_buffer_info(env, args[1], (void**)&rhsBlocks, &rhsLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "rhsBlocks must be a Buffer");
		return NULL;
	}

	int32_t n = 0;
	status = napi_get_value_int32(env, args[2], &n);
	if(status != napi_ok || n <= 0) {
		napi_throw_type_error(env, NULL, "n must be a positive integer");
		return NULL;
	}

	int64_t blockSize = 0;
	status = napi_get_value_int64(env, args[3], &blockSize);
	if(status != napi_ok || blockSize <= 0 || blockSize % 8 != 0) {
		napi_throw_type_error(env, NULL, "blockSize must be positive and a multiple of 8");
		return NULL;
	}

	size_t nSize = (size_t)n;
	size_t blockSize64 = (size_t)(blockSize / 8);

	if(ALen < nSize * nSize * sizeof(gf64_t)) {
		napi_throw_range_error(env, NULL, "A buffer too small for n×n matrix");
		return NULL;
	}

	if(rhsLen < nSize * (size_t)blockSize) {
		napi_throw_range_error(env, NULL, "rhsBlocks buffer too small for n blocks");
		return NULL;
	}

	gf64_init_dispatch();
	int result = GF64Controller::SolveAndReconstruct(A, rhsBlocks, nSize, blockSize64, 0);

	napi_value ret;
	if(result == 0) {
		status = napi_get_boolean(env, true, &ret);
	} else {
		status = napi_get_boolean(env, false, &ret);
	}
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create return value");
		return NULL;
	}

	return ret;
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

napi_value create_fn;
	status = napi_create_function(env, NULL, 0, Gf64Encoder_NAPI_create, NULL, &create_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create Gf64Encoder_create function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "Gf64Encoder_create", create_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set Gf64Encoder_create property");
		return NULL;
	}

	napi_value destroy_fn;
	status = napi_create_function(env, NULL, 0, Gf64Encoder_NAPI_destroy, NULL, &destroy_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create Gf64Encoder_destroy function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "Gf64Encoder_destroy", destroy_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set Gf64Encoder_destroy property");
		return NULL;
	}

	napi_value solve_fn;
	status = napi_create_function(env, NULL, 0, gf64_solve_NAPI, NULL, &solve_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create gf64_solve function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "gf64_solve", solve_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set gf64_solve property");
		return NULL;
	}

	napi_value compute_recovery_fn;
	status = napi_create_function(env, NULL, 0, ComputeRecovery_NAPI, NULL, &compute_recovery_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create compute_recovery function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "compute_recovery", compute_recovery_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set compute_recovery property");
		return NULL;
	}

	napi_value compute_repair_fn;
	status = napi_create_function(env, NULL, 0, ComputeRepair_NAPI, NULL, &compute_repair_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create compute_repair function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "compute_repair", compute_repair_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set compute_repair property");
		return NULL;
	}

	napi_value solve_reconstruct_fn;
	status = napi_create_function(env, NULL, 0, SolveAndReconstruct_NAPI, NULL, &solve_reconstruct_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create solve_and_reconstruct function");
		return NULL;
	}
	status = napi_set_named_property(env, exports, "solve_and_reconstruct", solve_reconstruct_fn);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to set solve_and_reconstruct property");
		return NULL;
	}

	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, parpar_gf64_init_NAPI)