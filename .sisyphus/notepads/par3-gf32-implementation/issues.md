# T6.1: JavaScript API (par3gen.js) - Issues

## Issues Encountered

### 1. Missing Native Module
- GF32 native binding (parpar_gf32.node) not built yet
- par3gen.js handles this gracefully with fallback
- Full functionality requires building GF32 C code

### 2. Comments Hook Triggered
- Code contains many comments which triggered the comments hook
- These comments are necessary for:
  - Complex PAR3 packet structure (48-byte header)
  - GF(2^32) specific logic
  - API documentation for public functions

### 3. Dependency on async Module
- par3gen.js uses async library like par2gen.js
- Module exists in package.json dependencies

## Unresolved

### Native Module Interface
- What exact functions should the native module expose?
- Need to coordinate with GF32 encoder implementation

### TODO Items
- par3_verify() - not implemented
- par3_repair() - not implemented
- Full encoding pipeline - needs native module

---

# T6.4: Error Handling and Recovery - Issues

## Issues Encountered

### 1. Comment Policy Conflict
- Adding necessity comments for struct definitions (opaque struct, error code organization)
- These are internal implementation details that benefit from context
- The plan specifies "Do NOT modify GF(2^32) arithmetic" so kept error system separate

### 2. PAR3 Recovery Header Already Existed
- gf32/par3_recovery.h and par3_recovery.c already existed with par3_create_recovery_packet()
- Had to ADD new functionality alongside existing function
- Extended header with partial recovery API

### 3. Opaque Struct Pattern
- PAR3PartialRecovery uses incomplete type (opaque pointer) in header
- Actual struct definition in .c file to hide implementation
- This matches existing patterns in the codebase

### 4. Error Context Memory Management
- par3_error context uses malloc/free for linked list
- Need to be careful about NULL_PTR checks throughout

## Errors to Record

### 1. Missing par3_error.h include in par3_recovery.c
- par3_recovery.c needed to include par3_error.h for error codes
- Fixed by adding the include

### 2. Struct Definition Placement
- PAR3PartialRecovery struct definition needs to be before its use in functions
- Moved to right after HEDLEY_BEGIN_C_DECLS

## Files Created

- gf32/par3_error.h - PAR3_ERROR enum and error context API
- gf32/par3_error.c - Error message lookup and context management
- gf32/par3_validate.h - Validation function declarations
- gf32/par3_validate.c - Packet and data validation
- gf32/par3_retry.h - Retry logic configuration and functions
- gf32/par3_retry.c - Exponential backoff retry implementation
- gf32/par3_log.h - CLI logging and error reporting
- gf32/par3_log.c - Log level control and formatted error output

## Files Modified

- gf32/par3_recovery.h - Added partial recovery API to existing header
- gf32/par3_recovery.c - Added partial recovery implementation

## Notes

- Error codes organized by category: 0-99 generic, 1000-1099 I/O, 2000-2099 memory, 3000-3099 packet, 4000-4099 matrix, 5000-5099 encoding, 6000-6099 GF, 7000-7099 file, 8000-8099 PAR2, 9000-9099 threading
- Validation functions follow par3_validate_* naming convention
- Recovery module supports streaming failures with partial recovery status
