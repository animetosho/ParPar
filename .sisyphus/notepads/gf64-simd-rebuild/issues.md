# gf64 Node.js Addon Crash Analysis

## Issue: Node.js addon segfault on require()

**Symptom:** gf64 C code works perfectly standalone but Node.js addon (src/gf64_addon.cc) crashes with segfault during `require()`. Crash occurs BEFORE any JS code runs—during addon loading.

**Suspected crash site:** `gf64_init_dispatch()` called during `__builtin_cpu_init()`

---

## Hypothesis 1: __builtin_cpu_init() During dlopen() — HIGHEST PROBABILITY

**Why this crashes:**
- `__builtin_cpu_init()` probes CPU features via `CPUID` instruction
- During `dlopen()`/dynamic loading, process may be in "transitional" state where signal handlers, TLS, or thread-local errno aren't fully set up
- On some glibc versions, `__builtin_cpu_init()` internally calls functions that aren't async-signal-safe during early library load

**Testing:**
```bash
# Test 1: Run with Node.js debugger to get backtrace
node --inspect-brk -e "require('./build/Release/gf64)"
# In debugger: bt full

# Test 2: Preload with CPU feature override
CPU_IMPLEMENTATION=baseline node -e "require('./build/Release/gf64)"

# Test 3: LD_PRELOAD trick to intercept CPUID
LD_DEBUG=bindings node -e "require('./build/Release/gf64)"

# Test 4: Check if crash is in addon or Node's NAPI layer
node --abort-on-uncaught-exception -e "require('./build/Release/gf64)"
```

---

## Hypothesis 2: Static Initialization Order Fiasco — MEDIUM PROBABILITY

**Why this crashes:**
- If `gf64_init_dispatch()` is called from a static initializer (global constructor), it runs before `main()` and before Node.js runtime is fully initialized
- The `dispatch_initialized = 0` static is itself a static initializer
- On some platforms, calling CPUID before the runtime is ready causes segfaults

**Testing:**
```cpp
// Add this as the very first line in addon
__attribute__((constructor))
void early_check(void) {
    fprintf(stderr, "Addon loaded, stack pointer: %p\n", __builtin_frame_address(0));
    // If this prints but crash still happens, it's in subsequent code
}
```

---

## Hypothesis 3: NAPI Initialization Race — MEDIUM PROBABILITY

**Why this crashes:**
- `NAPI_MODULE` triggers `NAPI_REGISTER_MODULE` which calls your `Init()` function
- If `gf64_init_dispatch()` is called during this init AND Node.js is still spinning up threads, you can hit a deadlock or use-after-free
- The `dispatch_initialized` static is not thread-safe (non-atomic, no memory barrier)

**Testing:**
```cpp
// Check if Node.js threads are involved by adding this in Init:
void* main_thread_id = pthread_self();
fprintf(stderr, "Init on thread: %lu\n", (unsigned long)main_thread_id);

// Also check: is dispatch_initialized truly only set once?
// Race condition: two threads could both see 0 simultaneously
```

---

## Hypothesis 4: Stack Alignment for SIMD — LOWER PROBABILITY

**Why this crashes:**
- Some SIMD implementations require 16-byte or 32-byte stack alignment
- During early library load, the stack pointer may not meet this requirement
- Rare on modern Linux but can happen in containerized or exotic environments

**Testing:**
```cpp
// Add alignment check in init
void gf64_init_dispatch(void) {
    void* sp = __builtin_frame_address(0);
    fprintf(stderr, "Stack align check: %p (mod 16 = %ld)\n", sp, (long)sp % 16);
    // If mod 16 != 0, you have your culprit
}
```

---

## Recommended Diagnostic Steps

1. **Isolate the crash point** - Add fprintf debug to gf64_init_dispatch
2. **Check the disassembly** - `objdump -d build/Release/gf64.node | grep -A5 "gf64_init_dispatch"`
3. **Verify with minimal reproduction** - Create minimal addon that just calls `__builtin_cpu_init()`

---

## Architectural Fix Recommendations

| Issue | Fix | Effort |
|-------|-----|--------|
| CPU init during dlopen | Defer `gf64_init_dispatch()` to first actual use (lazy init) | Small |
| Non-thread-safe dispatch | Add `std::atomic<int>` or mutex around dispatch_initialized | Small |
| Static init order | Remove static constructors, use explicit init function called from JS | Medium |
| NAPI timing | Wrap CPU detection in `napi_env` lifecycle instead of module load | Medium |

---

## Most Likely Fix

**Move `gf64_init_dispatch()` call out of the addon constructor/loading phase** and into a lazy-initialization pattern where CPU detection happens on the **first actual encode/decode call**, not during `require()`.

**Example lazy init pattern:**
```cpp
// Instead of calling in constructor or at module load:
static std::atomic<int> dispatch_initialized{0};
static std::mutex dispatch_mutex;

void ensure_dispatch() {
    if (dispatch_initialized.load(std::memory_order_relaxed) == 0) {
        std::lock_guard<std::mutex> lock(dispatch_mutex);
        if (dispatch_initialized.load() == 0) {
            gf64_init_dispatch();
            dispatch_initialized.store(1, std::memory_order_release);
        }
    }
}
```

---

## Related Context

**From T5.3 status:**
- Created gf64 Node.js addon (src/gf64_addon.cc) and added to binding.gyp
- Build succeeds: `node-gyp rebuild` produces `build/Release/parpar_gf64.node`
- C test confirms gf64 code works: `gf64_region_mul(out, in, 4, 0x1B)` produces correct output
- **BLOCKED**: Node.js segfaults when loading the addon
