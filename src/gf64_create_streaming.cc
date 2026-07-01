// par3_create_streaming NAPI binding — fd/path-based zero-copy PAR3 create.
//
// Exposed as `gf64.par3_create_streaming(sourcePath, options, callback)`.
//
// Simplified signature (vs the plan's fd/offset/length variant):
//   - The JS layer in B1 can map an fs.open handle onto this path-based
//     signature; the full fd/offset/length API is deferred to a follow-up.
//   - Internally: opens the source via open(2) + fstat(2), mmaps when
//     PAR3_GF64_USE_MMAP=1, otherwise reads the file into a 64-byte-aligned
//     buffer, then runs GF64Controller::ComputeRecoveryBlocksFull on the
//     mapped/buffered pointer.
//   - Returns { recoveryBytes, throughputMBps, durationMs } via callback.

#include <node_api.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <chrono>

#include "gf64_global.h"
#include "par3_engine.h"

static napi_status par3cs_get_uint64(napi_env env, napi_value val, uint64_t* result);

napi_value par3_create_streaming_NAPI(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 3;
	napi_value args[3];

	status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to get callback info");
		return NULL;
	}

	if(argc < 3) {
		napi_throw_type_error(env, NULL, "par3_create_streaming requires (sourcePath, options, callback)");
		return NULL;
	}

	char sourcePath[4096];
	size_t pathLen = 0;
	status = napi_get_value_string_utf8(env, args[0], sourcePath, sizeof(sourcePath), &pathLen);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "sourcePath must be a string");
		return NULL;
	}
	if(pathLen == 0) {
		napi_throw_type_error(env, NULL, "sourcePath must be a non-empty string");
		return NULL;
	}
	sourcePath[sizeof(sourcePath) - 1] = '\0';

	napi_valuetype cbType;
	status = napi_typeof(env, args[2], &cbType);
	if(status != napi_ok || cbType != napi_function) {
		napi_throw_type_error(env, NULL, "callback must be a function");
		return NULL;
	}

	napi_valuetype optsType;
	status = napi_typeof(env, args[1], &optsType);
	if(status != napi_ok || optsType != napi_object) {
		napi_throw_type_error(env, NULL, "options must be an object");
		return NULL;
	}

	napi_value recoverySlicesVal;
	int32_t numRecovery = 0;
	status = napi_get_named_property(env, args[1], "recoverySlices", &recoverySlicesVal);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "options.recoverySlices is required");
		return NULL;
	}
	status = napi_get_value_int32(env, recoverySlicesVal, &numRecovery);
	if(status != napi_ok || numRecovery <= 0) {
		napi_throw_range_error(env, NULL, "recoverySlices must be a positive integer");
		return NULL;
	}

	napi_value blockSizeVal;
	int64_t blockSize = 0;
	status = napi_get_named_property(env, args[1], "blockSize", &blockSizeVal);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "options.blockSize is required");
		return NULL;
	}
	status = napi_get_value_int64(env, blockSizeVal, &blockSize);
	if(status != napi_ok) {
		napi_throw_type_error(env, NULL, "blockSize must be an integer");
		return NULL;
	}
	if(blockSize <= 0 || blockSize % 8 != 0) {
		napi_throw_range_error(env, NULL, "blockSize must be positive and a multiple of 8");
		return NULL;
	}
	size_t blockSize64 = (size_t)(blockSize / 8);

	uint64_t firstInput = 0;
	napi_value firstInputVal;
	if(napi_get_named_property(env, args[1], "firstInput", &firstInputVal) == napi_ok) {
		napi_valuetype ft;
		if(napi_typeof(env, firstInputVal, &ft) == napi_ok && ft != napi_undefined && ft != napi_null) {
			if(par3cs_get_uint64(env, firstInputVal, &firstInput) != napi_ok) {
				napi_throw_type_error(env, NULL, "firstInput must be a Number or BigInt");
				return NULL;
			}
		}
	}

	uint64_t firstRecovery = 0;
	napi_value firstRecoveryVal;
	if(napi_get_named_property(env, args[1], "firstRecovery", &firstRecoveryVal) == napi_ok) {
		napi_valuetype ft;
		if(napi_typeof(env, firstRecoveryVal, &ft) == napi_ok && ft != napi_undefined && ft != napi_null) {
			if(par3cs_get_uint64(env, firstRecoveryVal, &firstRecovery) != napi_ok) {
				napi_throw_type_error(env, NULL, "firstRecovery must be a Number or BigInt");
				return NULL;
			}
		}
	}

	int32_t numThreads = 0;
	napi_value numThreadsVal;
	if(napi_get_named_property(env, args[1], "numThreads", &numThreadsVal) == napi_ok) {
		napi_valuetype nt;
		if(napi_typeof(env, numThreadsVal, &nt) == napi_ok && nt != napi_undefined && nt != napi_null) {
			if(napi_get_value_int32(env, numThreadsVal, &numThreads) != napi_ok) {
				napi_throw_type_error(env, NULL, "numThreads must be an integer");
				return NULL;
			}
		}
	}

	int fd = ::open(sourcePath, O_RDONLY);
	if(fd < 0) {
		int err = errno;
		char msg[256];
		std::snprintf(msg, sizeof(msg), "open failed for sourcePath: errno=%d (%s)", err, std::strerror(err));
		const char* nodeCode = "EIO";
		switch(err) {
			case ENOENT:  nodeCode = "ENOENT";  break;
			case EACCES:  nodeCode = "EACCES";  break;
			case EISDIR:  nodeCode = "EISDIR";  break;
			case EFBIG:   nodeCode = "EFBIG";   break;
			case ENOMEM:  nodeCode = "ENOMEM";  break;
			case ENAMETOOLONG: nodeCode = "ENAMETOOLONG"; break;
			default: break;
		}
		napi_throw_error(env, nodeCode, msg);
		return NULL;
	}

	struct stat st;
	if(::fstat(fd, &st) != 0) {
		int err = errno;
		::close(fd);
		char msg[256];
		std::snprintf(msg, sizeof(msg), "fstat failed: errno=%d (%s)", err, std::strerror(err));
		napi_throw_error(env, NULL, msg);
		return NULL;
	}

	if(st.st_size <= 0) {
		::close(fd);
		napi_throw_range_error(env, NULL, "sourcePath is empty");
		return NULL;
	}

	if(((int64_t)st.st_size) % blockSize != 0) {
		::close(fd);
		char msg[256];
		std::snprintf(msg, sizeof(msg),
			"sourcePath size (%lld) is not a multiple of blockSize (%lld)",
			(long long)st.st_size, (long long)blockSize);
		napi_throw_range_error(env, NULL, msg);
		return NULL;
	}
	size_t numInputs = (size_t)(st.st_size / blockSize);

	const char* useMmapEnv = std::getenv("PAR3_GF64_USE_MMAP");
	bool useMmap = (useMmapEnv != NULL && useMmapEnv[0] != '\0' && useMmapEnv[0] != '0');

	const uint8_t* mappedPtr = NULL;
	uint8_t* readBuffer = NULL;
	bool mmapActive = false;

	if(useMmap) {
		void* p = ::mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if(p == MAP_FAILED) {
			int err = errno;
			::close(fd);
			char msg[256];
			std::snprintf(msg, sizeof(msg), "mmap failed: errno=%d (%s)", err, std::strerror(err));
			napi_throw_error(env, NULL, msg);
			return NULL;
		}
		mappedPtr = (const uint8_t*)p;
		mmapActive = true;
	} else {
		size_t totalBytes = (size_t)st.st_size;
		readBuffer = (uint8_t*)std::aligned_alloc(64, (totalBytes + 63) & ~((size_t)63));
		if(readBuffer == NULL) {
			::close(fd);
			napi_throw_error(env, NULL, "aligned_alloc failed for source buffer");
			return NULL;
		}
		size_t off = 0;
		while(off < totalBytes) {
			ssize_t n = ::read(fd, readBuffer + off, totalBytes - off);
			if(n < 0) {
				if(errno == EINTR) continue;
				int err = errno;
				std::free(readBuffer);
				::close(fd);
				char msg[256];
				std::snprintf(msg, sizeof(msg), "read failed: errno=%d (%s)", err, std::strerror(err));
				napi_throw_error(env, NULL, msg);
				return NULL;
			}
			if(n == 0) {
				std::free(readBuffer);
				::close(fd);
				napi_throw_error(env, NULL, "unexpected EOF while reading sourcePath");
				return NULL;
			}
			off += (size_t)n;
		}
		mappedPtr = readBuffer;
	}

	const uint8_t* inputsBytes = mappedPtr;
	size_t recoveryBytes = (size_t)numRecovery * (size_t)blockSize;
	uint8_t* recovery = (uint8_t*)std::aligned_alloc(64, (recoveryBytes + 63) & ~((size_t)63));
	if(recovery == NULL) {
		if(mmapActive) ::munmap((void*)mappedPtr, (size_t)st.st_size);
		if(readBuffer) std::free(readBuffer);
		::close(fd);
		napi_throw_error(env, NULL, "aligned_alloc failed for recovery buffer");
		return NULL;
	}
	std::memset(recovery, 0, recoveryBytes);

	gf64_init_dispatch();
	auto t0 = std::chrono::steady_clock::now();
	GF64Controller::ComputeRecoveryBlocksFull(
		(const gf64_t*)inputsBytes, numInputs,
		(gf64_t*)recovery, (size_t)numRecovery,
		blockSize64,
		firstInput, firstRecovery,
		(int)numThreads
	);
	auto t1 = std::chrono::steady_clock::now();

	double durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
	double throughputMBps = 0.0;
	if(durationMs > 0.0) {
		throughputMBps = ((double)recoveryBytes / 1048576.0) / (durationMs / 1000.0);
	}

	if(mmapActive) {
		::munmap((void*)mappedPtr, (size_t)st.st_size);
	}
	if(readBuffer) {
		std::free(readBuffer);
	}
	std::free(recovery);
	::close(fd);

	napi_value resultObj;
	status = napi_create_object(env, &resultObj);
	if(status != napi_ok) {
		napi_throw_error(env, NULL, "Failed to create result object");
		return NULL;
	}

	napi_value recoveryBytesVal;
	napi_create_uint32(env, (uint32_t)(recoveryBytes & 0xFFFFFFFF), &recoveryBytesVal);
	napi_set_named_property(env, resultObj, "recoveryBytes", recoveryBytesVal);

	napi_value throughputVal;
	napi_create_double(env, throughputMBps, &throughputVal);
	napi_set_named_property(env, resultObj, "throughputMBps", throughputVal);

	napi_value durationVal;
	napi_create_double(env, durationMs, &durationVal);
	napi_set_named_property(env, resultObj, "durationMs", durationVal);

	napi_value cbArgs[2];
	napi_get_null(env, &cbArgs[0]);
	cbArgs[1] = resultObj;
	napi_value cbReturn;
	status = napi_call_function(env, args[2], args[2], 2, cbArgs, &cbReturn);
	if(status != napi_ok) {
		return NULL;
	}

	return NULL;
}

static napi_status par3cs_get_uint64(napi_env env, napi_value val, uint64_t* result) {
	napi_status status;
	napi_valuetype valuetype;

	status = napi_typeof(env, val, &valuetype);
	if(status != napi_ok) return status;

	if(valuetype == napi_bigint) {
		bool lossless = false;
		status = napi_get_value_bigint_uint64(env, val, result, &lossless);
		if(status == napi_ok) return status;
	}

	int64_t tmp;
	status = napi_get_value_int64(env, val, &tmp);
	if(status == napi_ok) {
		*result = (uint64_t)tmp;
		return napi_ok;
	}

	return napi_generic_failure;
}