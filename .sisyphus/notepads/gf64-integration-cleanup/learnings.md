# GF64 Integration Learnings

## Completed Successfully
- T1.1-T1.3: gf64 addon fix and verification
- T2.1: gf64 binding loader integrated into par3gen.js
- T2.2: _encodeBlocks updated to use gf64.mul_arr() API
- T3.1-T3.3: gf32 code removed from project

## Issues Discovered

### binding.gyp Has Two Targets Arrays (BLOCKER)
**File**: `binding.gyp`
**Problem**: The file has TWO `targets` arrays:
- Line 65: First `targets` array containing `parpar_gf` (GF16 for PAR2) and many other targets
- Line 1157: Second `targets` array containing only `parpar_gf64`

**Impact**: Only `parpar_gf64` is being built. `parpar_gf` (GF16) is ignored due to malformed gyp structure.

**Workaround**: Fix binding.gyp by merging the two targets arrays into one.

### mul_arr() Has Node.js v18 Buffer Bug
**File**: src/gf64_addon.cc
**Problem**: `napi_get_buffer_info` assertion fails on Node.js v18 when called from mul_arr()
**Impact**: gf64.mul_arr() cannot be tested on Node.js v18. However, par3gen.js integration is complete and correct per API.

## Key Decisions
- PAR64 = new format using GF(64), separate from PAR2 which uses GF(16)
- par3gen.js uses gf64 for PAR64 creation (separate from parpar CLI)
- GF32 removed entirely per user request

## Test Commands
```bash
# Verify gf64 loads
/usr/bin/node -e "const par = require('./lib/par3gen.js'); console.log('gf64:', !!par.gf64Binding)"

# Verify gf64 info
/usr/bin/node -e "const gf64 = require('./build/Release/parpar_gf64.node'); console.log(gf64.gf64_info())"
```