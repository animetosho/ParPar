# PAR3 Implementation Learnings

## T8.3: Packet Round-Trip Tests

### Issue Encountered
The gf32 codebase has a recursive include chain that prevents standalone compilation:
- par3.h → platform.h → stdint.h (custom) → stdint.h (system) 
- But the custom stdint.h checks MSVC version which causes re-inclusion

### Root Cause
src/stdint.h line 32: `!defined(_MSC_VER) || defined(_STDINT) || _MSC_VER >= 1900 || defined(__clang__)`
When this condition fails, it includes itself creating infinite recursion.

### Workaround
The issue is pre-existing in the codebase. Tests must be compiled via node-gyp or with the build system.

### Test File Created
- Location: `gf32/test/test_packet_roundtrip.c`
- Tests: header structure, packet types, checksums, round-trip for Start/File/Matrix/Recovery/ExtData packets

### Note
The existing test binaries (benchmark, test_gf32_simple) were compiled with the full build system.
Direct gcc compilation of individual .c files with the gf32 headers fails due to include chain issues.
