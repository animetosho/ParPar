// ============================================================================
// par3-native-bench.cpp
// ----------------------------------------------------------------------------
// T1 from the par3-1200mbps plan: standalone C++ PAR3 create benchmark that
// bypasses the JS layer entirely. Calls the existing gf64_region_2d_muladd_arr
// 2D-blocked kernel directly (PC3/PC4), builds the Cauchy coefficient matrix
// with gf64_inverse, and stages source data via read(2) for I/O.
//
// Purpose: produce the absolute hardware-bound throughput on this host without
// any NAPI crossing, BigInt marshaling, or WorkerThread scheduling — to
// calibrate downstream phase gates (A1, A2, B*, C*).
//
// JSON output matches the par3-create-bench.js shape so test/bench/run-all.js
// (and its parseable METRICS JSON block consumer) can ingest it unmodified.
//
// CLI:
//   --size=<bytes>           Source size (e.g. 1G, 1073741824). Default: 1G
//   --slices=<N>             Number of input slices. Default: 10000
//   --block-size=<bytes>     Block size in bytes (multiple of 8). Default: 4096
//   --recovery-slices=<N>    Recovery slices. Default: max(10, slices/10)
//   --runs=<N>               Timed runs (median/stdev if > 1). Default: 1
//   --source-path=<path>     Source file path (default: mkstemp in /tmp)
//   --help, -h               Show help
//
// Env:
//   PAR3_GF64_GROUP          Group size G for the 2D kernel (default 12)
//   PAR3_GF64_K_GROUP        K-group size for the 2D kernel (default 12)
//   PAR3_AVX512_FORCE        Force/disable AVX-512 dispatch (PD2)
//
// Build:
//   cd test/bench && mkdir -p build && cd build
//   cmake -DCMAKE_BUILD_TYPE=Release ..
//   make -j4 par3-native-bench
//
// Example:
//   taskset -c 0-3 ./test/bench/build/par3-native-bench
//       --size=1G --slices=10000 --block-size=4096
//       --recovery-slices=1000 --runs=3
// ============================================================================

#include "bench.h"
#include "gf64_global.h"
#include "gf64_invert.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>


// ============================================================================
// parseSize() -- mirrors test/bench/bench-helpers.js parseSize()
// Accepts: "1G", "2G", "1073741824", "1.5GiB", "512K", etc.
// Returns bytes. Falls back to defaultVal on parse error.
// ============================================================================
static uint64_t parseSize(const char* s, uint64_t defaultVal) {
	if (!s || !*s) return defaultVal;

	// Pure integer: ^[0-9]+$
	bool allDigits = true;
	for (const char* p = s; *p; p++) {
		if (*p < '0' || *p > '9') { allDigits = false; break; }
	}
	if (allDigits) {
		return (uint64_t)std::strtoull(s, nullptr, 10);
	}

	// Number + optional unit suffix (case-insensitive, "B" optional)
	double n = 0;
	int consumed = 0;
	if (std::sscanf(s, "%lf%n", &n, &consumed) != 1 || consumed <= 0) {
		return defaultVal;
	}
	std::string u;
	for (const char* p = s + consumed; *p; p++) {
		if (std::isalnum((unsigned char)*p)) u += (char)std::toupper((unsigned char)*p);
		else break;
	}

	uint64_t mult = 1;
	if (u == "" || u == "B")                                 mult = 1;
	else if (u == "K"  || u == "KB" || u == "KI" || u == "KIB") mult = (uint64_t)1024;
	else if (u == "M"  || u == "MB" || u == "MI" || u == "MIB") mult = (uint64_t)1024 * 1024;
	else if (u == "G"  || u == "GB" || u == "GI" || u == "GIB") mult = (uint64_t)1024 * 1024 * 1024;
	else if (u == "T"  || u == "TB" || u == "TI" || u == "TIB") mult = (uint64_t)1024 * 1024 * 1024 * 1024ULL;
	else return defaultVal;

	return (uint64_t)(n * (double)mult);
}


// ============================================================================
// formatBytes() -- mirrors helpers.formatBytes() in bench-helpers.js
// ============================================================================
static std::string formatBytes(uint64_t b) {
	static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double v = (double)b;
	int i = 0;
	while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[i]);
	return std::string(buf);
}


// ============================================================================
// formatDuration() -- mirrors helpers.formatDuration() (ms/s/m)
// ============================================================================
static std::string formatDuration(double ms) {
	char buf[32];
	if (ms < 1000.0)       std::snprintf(buf, sizeof(buf), "%.0fms", ms);
	else if (ms < 60000.0) std::snprintf(buf, sizeof(buf), "%.2fs", ms / 1000.0);
	else                   std::snprintf(buf, sizeof(buf), "%.2fm",  ms / 60000.0);
	return std::string(buf);
}


// ============================================================================
// median + stdev (sample stdev, n-1 denominator)
// ============================================================================
static double medianOf(std::vector<double> v) {
	std::sort(v.begin(), v.end());
	size_t n = v.size();
	if (n == 0) return 0.0;
	if (n % 2 == 0) return (v[n / 2 - 1] + v[n / 2]) * 0.5;
	return v[n / 2];
}
static double stdevOf(const std::vector<double>& v) {
	if (v.size() < 2) return 0.0;
	double m = 0;
	for (double x : v) m += x;
	m /= (double)v.size();
	double s = 0;
	for (double x : v) s += (x - m) * (x - m);
	return std::sqrt(s / (double)(v.size() - 1));
}


// ============================================================================
// Parsed CLI options
// ============================================================================
struct BenchOpts {
	uint64_t sourceBytes;
	size_t   sliceCount;
	size_t   blockSize;
	size_t   recoverySlices;  // 0 = auto
	int      runs;
	std::string sourcePath;   // empty -> mkstemp
	bool     help;
};


// ============================================================================
// printHelp
// ============================================================================
static void printHelp() {
	std::cout <<
		"Usage: par3-native-bench [options]\n"
		"\n"
		"PAR3 Native (C++-only) Create Benchmark\n"
		"=======================================\n"
		"\n"
		"Options:\n"
		"  --size=<bytes>          Source size (e.g. 1G, 1073741824). Default: 1G\n"
		"  --slices=<N>            Number of input slices. Default: 10000\n"
		"  --block-size=<bytes>    Block size in bytes (multiple of 8). Default: 4096\n"
		"  --recovery-slices=<N>   Recovery slices. Default: max(10, slices/10)\n"
		"  --runs=<N>              Number of timed runs (median/stdev if > 1). Default: 1\n"
		"  --source-path=<path>    Write source to this path; default: mkstemp in /tmp\n"
		"  --help, -h              Show this help\n"
		"\n"
		"Environment:\n"
		"  PAR3_GF64_GROUP         Group size G for the 2D kernel (default 12, range 1..256)\n"
		"  PAR3_GF64_K_GROUP       K-group size for the 2D kernel (default 12, range 1..256)\n"
		"  PAR3_AVX512_FORCE       Force/disable AVX-512 dispatch (PD2 heuristic override)\n"
		"\n"
		"Example:\n"
		"  taskset -c 0-3 ./test/bench/build/par3-native-bench\n"
		"      --size=1G --slices=10000 --block-size=4096\n"
		"      --recovery-slices=1000 --runs=3\n";
}


// ============================================================================
// parseArgs
// ============================================================================
static BenchOpts parseArgs(int argc, char** argv) {
	BenchOpts o;
	o.sourceBytes    = (uint64_t)1024 * 1024 * 1024;  // 1 GiB
	o.sliceCount     = 10000;
	o.blockSize      = 4096;
	o.recoverySlices = 0;                             // auto
	o.runs           = 1;
	o.sourcePath     = "";
	o.help           = false;

	static struct option longOpts[] = {
		{ "size",           required_argument, nullptr, 1001 },
		{ "slices",         required_argument, nullptr, 1002 },
		{ "block-size",     required_argument, nullptr, 1003 },
		{ "recovery-slices",required_argument, nullptr, 1004 },
		{ "runs",           required_argument, nullptr, 1005 },
		{ "source-path",    required_argument, nullptr, 1006 },
		{ "help",           no_argument,       nullptr, 'h' },
		{ nullptr, 0, nullptr, 0 }
	};

	int opt, idx = 0;
	while ((opt = getopt_long(argc, argv, "h", longOpts, &idx)) != -1) {
		switch (opt) {
			case 1001: o.sourceBytes    = parseSize(optarg, o.sourceBytes); break;
			case 1002: o.sliceCount     = (size_t)std::strtoull(optarg, nullptr, 10); break;
			case 1003: o.blockSize      = (size_t)std::strtoull(optarg, nullptr, 10); break;
			case 1004: o.recoverySlices = (size_t)std::strtoull(optarg, nullptr, 10); break;
			case 1005: o.runs           = (int)   std::strtol (optarg, nullptr, 10); break;
			case 1006: o.sourcePath     = optarg; break;
			case 'h':
			case '?':
			default:
				o.help = true;
				break;
		}
	}
	return o;
}


// ============================================================================
// writeAll / readAll -- loop to handle EINTR + short transfers
// ============================================================================
static int writeAll(int fd, const uint8_t* buf, size_t len) {
	const uint8_t* p = buf;
	size_t rem = len;
	while (rem > 0) {
		ssize_t n = ::write(fd, p, rem);
		if (n < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		p += n; rem -= (size_t)n;
	}
	return 0;
}
static int readAll(int fd, uint8_t* buf, size_t len) {
	uint8_t* p = buf;
	size_t rem = len;
	while (rem > 0) {
		ssize_t n = ::read(fd, p, rem);
		if (n < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (n == 0) return -2;  // unexpected EOF
		p += n; rem -= (size_t)n;
	}
	return 0;
}


// ============================================================================
// Deterministic source fill -- matches the spec's recipe:
//   for(i=0; i<bytes; i+=8) *(uint64_t*)(buf+i) = i * 0x9E3779B97F4A7C15ULL
//                                          ^ 0x123456789ABCDEFULL;
// The plan specifies this exact pattern (split-RNG via Knuth's golden ratio
// constant XORed with a fixed key) so re-runs are bit-identical.
// ============================================================================
static void fillDeterministic(uint8_t* buf, uint64_t bytes) {
	const uint64_t k0 = 0x0123456789ABCDEFULL;
	const uint64_t k1 = 0x9E3779B97F4A7C15ULL;  // Knuth golden ratio (64-bit)
	uint64_t aligned = bytes & ~(uint64_t)7;
	for (uint64_t i = 0; i < aligned; i += 8) {
		*(uint64_t*)(buf + i) = i * k1 ^ k0;
	}
	if (bytes != aligned) {
		uint64_t last = aligned * k1 ^ k0;
		std::memcpy(buf + aligned, &last, (size_t)(bytes - aligned));
	}
}


// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
	BenchOpts opts = parseArgs(argc, argv);
	if (opts.help) { printHelp(); return 0; }

	// ----- validate
	if (opts.blockSize < 8 || (opts.blockSize % 8) != 0) {
		std::fprintf(stderr,
			"ERROR: --block-size must be a multiple of 8 (gf64_t size). Got %zu.\n",
			opts.blockSize);
		return 2;
	}
	if (opts.sliceCount == 0) {
		std::fprintf(stderr, "ERROR: --slices must be > 0\n");
		return 2;
	}
	if (opts.runs < 1) {
		std::fprintf(stderr, "ERROR: --runs must be >= 1\n");
		return 2;
	}

	// ----- derived sizes (matches par3-create-bench.js actualSize logic)
	size_t   blockSize64      = opts.blockSize / sizeof(gf64_t);
	size_t   sliceSize        = (size_t)((opts.sourceBytes + opts.sliceCount - 1)
	                                     / opts.sliceCount);  // ceil
	sliceSize = ((sliceSize + opts.blockSize - 1) / opts.blockSize) * opts.blockSize;
	uint64_t actualSourceBytes = (uint64_t)sliceSize * opts.sliceCount;
	size_t   numInputs        = actualSourceBytes / opts.blockSize;

	if (opts.recoverySlices == 0) {
		// mirror JS: Math.max(MIN_RECOVERY_SLICES=10, sliceCount * 10/100)
		size_t tenPct = opts.sliceCount / 10;
		opts.recoverySlices = tenPct > 10 ? tenPct : 10;
	}
	if (opts.recoverySlices > numInputs) {
		std::fprintf(stderr,
			"WARNING: recovery-slices (%zu) capped to numInputs (%zu)\n",
			opts.recoverySlices, numInputs);
		opts.recoverySlices = numInputs;
	}

	// ----- env reads (mirror engine defaults: G=K=12)
	int G = 12;
	int K = 12;
	if (const char* e = std::getenv("PAR3_GF64_GROUP")) {
		int v = std::atoi(e); if (v >= 1 && v <= 256) G = v;
	}
	if (const char* e = std::getenv("PAR3_GF64_K_GROUP")) {
		int v = std::atoi(e); if (v >= 1 && v <= 256) K = v;
	}

	// ----- dispatch
	if (gf64_init_dispatch() != 0) {
		std::fprintf(stderr, "ERROR: gf64_init_dispatch failed\n");
		return 3;
	}
	const char* methodNames[] = { "avx512", "avx2", "ssse3", "scalar" };
	const char* methodName = (gf64_current_method >= 0 && gf64_current_method <= 3)
		? methodNames[gf64_current_method] : "unknown";

	// ----- working set / downclock heuristic note (PD2)
	size_t workingSetBytes = (numInputs + opts.recoverySlices)
		* blockSize64 * sizeof(gf64_t);
	const uint64_t kDownclockThreshold = 16ULL * 1024 * 1024;
	bool downclockWouldApply = (workingSetBytes > kDownclockThreshold)
		&& (gf64_current_method == GF64_AVX512);

	// ----- print workload header
	std::cout << "PAR3 Native (C++-only) Create Benchmark\n";
	std::cout << "========================================\n";
	std::cout << "Source size:    " << formatBytes(actualSourceBytes)
	          << " (" << actualSourceBytes << " bytes)\n";
	std::cout << "Slice count:    " << opts.sliceCount << "\n";
	std::cout << "Slice size:     " << formatBytes(sliceSize)
	          << " (" << sliceSize << " bytes)\n";
	std::cout << "Block size:     " << formatBytes(opts.blockSize)
	          << " (" << opts.blockSize << " bytes)\n";
	std::cout << "Blocks total:   " << numInputs << " (input) + "
	          << opts.recoverySlices << " (recovery)\n";
	std::cout << "GF64 method:    " << methodName << "\n";
	std::cout << "Group sizes:    G=" << G << "  K=" << K << "\n";
	std::cout << "Working set:    " << formatBytes(workingSetBytes)
	          << (downclockWouldApply
	              ? "  [>16 MiB — Zen4 AVX-512 downclock heuristic would apply]"
	              : "  [within AVX-512 frequency ceiling]")
	          << "\n";
	std::cout << "Runs:           " << opts.runs << "\n";
	std::cout << "\n";

	// ----- allocate buffers
	uint8_t* source = (uint8_t*)aligned_alloc(64, actualSourceBytes);
	if (!source) {
		std::fprintf(stderr, "ERROR: source aligned_alloc failed (%llu bytes)\n",
			(unsigned long long)actualSourceBytes);
		return 4;
	}
	fillDeterministic(source, actualSourceBytes);

	// ----- write source to disk (mkstemp or user path)
	char tmpPath[256] = {0};
	bool tmpIsAuto = opts.sourcePath.empty();
	if (tmpIsAuto) {
		std::strncpy(tmpPath, "/tmp/par3-native-bench-XXXXXX", sizeof(tmpPath) - 1);
		int fd = mkstemp(tmpPath);
		if (fd < 0) {
			std::fprintf(stderr, "ERROR: mkstemp failed: %s\n", std::strerror(errno));
			free(source);
			return 5;
		}
		if (writeAll(fd, source, (size_t)actualSourceBytes) != 0) {
			std::fprintf(stderr, "ERROR: source write failed: %s\n", std::strerror(errno));
			::close(fd); ::unlink(tmpPath); free(source);
			return 5;
		}
		::close(fd);
	} else {
		std::strncpy(tmpPath, opts.sourcePath.c_str(), sizeof(tmpPath) - 1);
		int fd = ::open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			std::fprintf(stderr, "ERROR: open source for write '%s' failed: %s\n",
				tmpPath, std::strerror(errno));
			free(source);
			return 5;
		}
		if (writeAll(fd, source, (size_t)actualSourceBytes) != 0) {
			std::fprintf(stderr, "ERROR: source write failed: %s\n", std::strerror(errno));
			::close(fd); ::unlink(tmpPath); free(source);
			return 5;
		}
		::close(fd);
	}

	// ----- recovery output buffer (zero-initialized per run inside the loop)
	gf64_t* recovery = (gf64_t*)aligned_alloc(64,
		(uint64_t)opts.recoverySlices * blockSize64 * sizeof(gf64_t));
	if (!recovery) {
		std::fprintf(stderr, "ERROR: recovery aligned_alloc failed\n");
		free(source); if (tmpIsAuto) ::unlink(tmpPath);
		return 6;
	}

	// ----- Cauchy coefficient matrix: recoverySlices rows x numInputs cols
	gf64_t* coeff = (gf64_t*)aligned_alloc(64,
		(uint64_t)opts.recoverySlices * numInputs * sizeof(gf64_t));
	if (!coeff) {
		std::fprintf(stderr, "ERROR: coeff aligned_alloc failed (%zu*%zu gf64_t)\n",
			opts.recoverySlices, numInputs);
		free(source); free(recovery);
		if (tmpIsAuto) ::unlink(tmpPath);
		return 6;
	}

	// ----- I/O staging buffer (separate from `source` so read(2) cost is honest)
	uint8_t* ioBuf = (uint8_t*)aligned_alloc(64, actualSourceBytes);
	if (!ioBuf) {
		std::fprintf(stderr, "ERROR: ioBuf aligned_alloc failed\n");
		free(source); free(recovery); free(coeff);
		if (tmpIsAuto) ::unlink(tmpPath);
		return 6;
	}

	// ----- per-2D-tile pointer arrays (stack-resident, MAX_STACK matches engine)
	const size_t MAX_STACK = 256;
	gf64_t*       outsStack[MAX_STACK];
	const gf64_t* inStack  [MAX_STACK];
	std::vector<gf64_t*>       outsHeap;
	std::vector<const gf64_t*> inHeap;

	// ----- timed runs
	std::vector<double> throughputs;
	throughputs.reserve((size_t)opts.runs);

	for (int run = 1; run <= opts.runs; run++) {
		if (opts.runs > 1) {
			std::cout << "--- Run " << run << "/" << opts.runs << " ---\n";
		}

		// --- (a) read(2) source into ioBuf
		Timer tTotal;
		int rfd = ::open(tmpPath, O_RDONLY);
		if (rfd < 0) {
			std::fprintf(stderr, "ERROR: open source for read '%s' failed: %s\n",
				tmpPath, std::strerror(errno));
			free(source); free(recovery); free(coeff); free(ioBuf);
			if (tmpIsAuto) ::unlink(tmpPath);
			return 7;
		}
		int rrc = readAll(rfd, ioBuf, (size_t)actualSourceBytes);
		::close(rfd);
		if (rrc != 0) {
			std::fprintf(stderr, "ERROR: source read failed (rc=%d)\n", rrc);
			free(source); free(recovery); free(coeff); free(ioBuf);
			if (tmpIsAuto) ::unlink(tmpPath);
			return 7;
		}
		double ioMs = tTotal.elapsed() * 1000.0;

		// --- (b) zero output
		std::memset(recovery, 0,
			(uint64_t)opts.recoverySlices * blockSize64 * sizeof(gf64_t));

		// --- (c) build Cauchy matrix: M[r][c] = 1 / ((1+c) XOR (numInputs+1+r))
		//       (matches engine's firstInput=1, firstRecovery=numInputs+1 convention)
		const uint64_t firstInput    = 1;
		const uint64_t firstRecovery = (uint64_t)numInputs + 1;
		for (size_t r = 0; r < opts.recoverySlices; r++) {
			uint64_t y = firstRecovery + r;
			gf64_t* row = coeff + r * numInputs;
			for (size_t c = 0; c < numInputs; c++) {
				uint64_t x = firstInput + c;
				uint64_t denom = x ^ y;
				if (denom == 0) denom = 1;  // disjoint ranges -> never hits, but guard
				row[c] = gf64_inverse(denom);
			}
		}
		double cauchyMs = tTotal.elapsed() * 1000.0 - ioMs;

		// --- (d) 2D-blocked kernel: per K-tile, fold over all input blocks
		Timer tKern;
		for (size_t kStart = 0; kStart < opts.recoverySlices; kStart += (size_t)K) {
			size_t Kk = std::min((size_t)K, opts.recoverySlices - kStart);

			// zero the K outputs for this tile (kernel does XOR-accumulate)
			for (size_t kk = 0; kk < Kk; kk++) {
				std::memset(recovery + (kStart + kk) * blockSize64, 0,
					blockSize64 * sizeof(gf64_t));
			}

			gf64_t** outsPtr = outsStack;
			if (Kk > MAX_STACK) {
				if (outsHeap.size() < Kk) outsHeap.resize(Kk);
				outsPtr = outsHeap.data();
			}
			for (size_t kk = 0; kk < Kk; kk++) {
				outsPtr[kk] = recovery + (kStart + kk) * blockSize64;
			}

			// row-major coeff: rows of length numInputs, so K_stride = numInputs
			const gf64_t* coeffBase = coeff + kStart * numInputs;

			for (size_t j = 0; j < numInputs; j += (size_t)G) {
				size_t Gk = std::min((size_t)G, numInputs - j);
				const gf64_t** inPtr = inStack;
				if (Gk > MAX_STACK) {
					if (inHeap.size() < Gk) inHeap.resize(Gk);
					inPtr = inHeap.data();
				}
				for (size_t g = 0; g < Gk; g++) {
					// each input block is `blockSize` bytes = blockSize64 gf64_t
					inPtr[g] = (const gf64_t*)(ioBuf + (j + g) * opts.blockSize);
				}
				gf64_region_2d_muladd_arr(
					(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)outsPtr,
					Kk,
					(const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)inPtr,
					Gk,
					coeffBase + j,
					numInputs,
					blockSize64);
			}
		}
		double kernelMs = tKern.elapsed() * 1000.0;
		double totalMs  = tTotal.elapsed() * 1000.0;
		// MiB/s (matches JS bench: actualSize / 1048576 / (dt/1000))
		double mbps = ((double)actualSourceBytes / 1048576.0) / (totalMs / 1000.0);

		throughputs.push_back(mbps);

		std::cout << std::fixed << std::setprecision(2);
		std::cout << "  I/O (read source):  " << formatDuration(ioMs)     << "\n";
		std::cout << "  Cauchy matrix:      " << formatDuration(cauchyMs) << "\n";
		std::cout << "  Kernel (2D-blocked):" << formatDuration(kernelMs) << "\n";
		std::cout << "  Total:              " << formatDuration(totalMs)  << "\n";
		std::cout << "  Throughput:         " << mbps << " MB/s\n";

		// --- per-run metrics JSON block (run-all.js consumer parses this)
		std::cout << "---METRICS JSON---\n";
		std::cout << "{\n";
		std::cout << "  \"format\": \"PAR3\",\n";
		std::cout << "  \"sourceBytes\": " << actualSourceBytes << ",\n";
		std::cout << "  \"sourceBytesHuman\": \"" << formatBytes(actualSourceBytes) << "\",\n";
		std::cout << "  \"sliceCount\": " << opts.sliceCount << ",\n";
		std::cout << "  \"sliceSize\": " << sliceSize << ",\n";
		std::cout << "  \"sliceSizeHuman\": \"" << formatBytes(sliceSize) << "\",\n";
		std::cout << "  \"blockSize\": " << opts.blockSize << ",\n";
		std::cout << "  \"blockSizeHuman\": \"" << formatBytes(opts.blockSize) << "\",\n";
		std::cout << "  \"recoverySlices\": " << opts.recoverySlices << ",\n";
		std::cout << "  \"gfMethod\": \"" << methodName << "\",\n";
		std::cout << "  \"gfGroupSize\": " << G << ",\n";
		std::cout << "  \"gfKGroupSize\": " << K << ",\n";
		std::cout << "  \"run\": " << run << ",\n";
		std::cout << "  \"workingSetBytes\": " << workingSetBytes << ",\n";
		std::cout << "  \"workingSetHuman\": \"" << formatBytes(workingSetBytes) << "\",\n";
		std::cout << "  \"downclockWouldApply\": " << (downclockWouldApply ? "true" : "false") << ",\n";
		std::cout << "  \"metrics\": {\n";
		std::cout << "    \"ioMs\": "     << std::setprecision(3) << ioMs     << ",\n";
		std::cout << "    \"cauchyMs\": " << std::setprecision(3) << cauchyMs << ",\n";
		std::cout << "    \"kernelMs\": " << std::setprecision(3) << kernelMs << ",\n";
		std::cout << "    \"createMs\": " << std::setprecision(3) << totalMs  << ",\n";
		std::cout << "    \"totalMs\": "  << std::setprecision(3) << totalMs  << ",\n";
		std::cout << "    \"createMBps\": " << std::setprecision(2) << mbps    << ",\n";
		std::cout << "    \"peakRssBytes\": 0,\n";
		std::cout << "    \"peakRssHuman\": \"0 B\"\n";
		std::cout << "  }\n";
		std::cout << "}\n";
		std::cout << "---END METRICS---\n";

		if (opts.runs > 1 && run < opts.runs) std::cout << "\n";
	}

	// ----- aggregate summary (only when runs > 1)
	if (opts.runs > 1) {
		double med = medianOf(throughputs);
		double sd  = stdevOf (throughputs);
		std::cout << std::fixed << std::setprecision(2);
		std::cout << "\n=== Summary (" << opts.runs << " runs) ===\n";
		std::cout << "  Per-run MBps:    ";
		for (size_t i = 0; i < throughputs.size(); i++) {
			if (i) std::cout << ", ";
			std::cout << throughputs[i];
		}
		std::cout << "\n";
		std::cout << "  Median MBps:     " << med << "\n";
		std::cout << "  Stdev  MBps:     " << sd  << "\n";
	}

	// ----- cleanup
	free(source);
	free(recovery);
	free(coeff);
	free(ioBuf);
	if (tmpIsAuto) ::unlink(tmpPath);

	return 0;
}