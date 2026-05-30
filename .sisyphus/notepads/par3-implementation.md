# PAR3 Packet Implementation Notes

## T3.1 - PAR3 Packet Base Types (gf32/par3.h, gf32/par3.c)

### Created Files
- `gf32/par3.h` - PAR3 packet type definitions and structures
- `gf32/par3.c` - PAR3 packet helper functions

### Key Decisions

1. **PACK_STRUCT macro**: Used MSVC `#pragma pack(push, 1)` and GCC `__attribute__((packed))` for cross-platform byte-aligned structures

2. **Packet Type Strings**: PAR3 uses 8-byte null-padded type strings:
   - "PAR STA\0" = Start packet
   - "PAR CRE\0" = Creator packet  
   - "PAR FIL\0" = File packet
   - "PAR CAU\0" = Cauchy matrix packet
   - etc.

3. **Common Packet Header (48 bytes)**:
   - magic[8] - "PAR3\0PKT"
   - checksum[16] - Blake3 hash of body
   - length[8] - total packet length
   - input_set_id[8] - identifies volume set
   - type[8] - packet type string

4. **Little-endian**: PAR3 spec requires little-endian for all multi-byte fields

### Structures Defined
- `PAR3PacketHeader` - common header for all packets
- `PAR3StartBody` - Start packet (GF params, block size)
- `PAR3CreatorBody` - Creator packet (application info)
- `PAR3FileBody` - File packet (UUID, metadata, name)
- `PAR3DirectoryBody` - Directory packet
- `PAR3DataBody` - Data packet (inline block)
- `PAR3ExtDataBody` - External Data packet (hashes)
- `PAR3MatrixBody` - base Matrix packet
- `PAR3CauchyBody` - Cauchy matrix packet
- `PAR3RecoveryBody` - Recovery Data packet
- `PAR3RootBody` - Root packet
- `PAR3ExtRecBody` - External Recovery Data packet

### Helper Functions
- `par3_get_packet_type()` - convert type string to enum
- `par3_get_type_string()` - convert enum to type string
- `par3_validate_magic()` - verify packet magic

### Notes
- Comments are necessary: struct field layout needs documentation for maintainability
- Serialization (read/write functions) not implemented yet (T3.7)
- Future: need to add Sparse and Explicit matrix packet bodies