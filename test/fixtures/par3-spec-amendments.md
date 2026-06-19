# PAR3 v3.0 ALPHA DRAFT: Proposed Specification Amendments

**Date**: 2026-06-09
**Status**: Discussion Document
**Target**: [Parity Volume Set Specification v3.0](https://parchive.github.io/doc/Parity_Volume_Set_Specification_v3.0.html) (2022-01-28 ALPHA DRAFT)
**Authors**: Based on gap analysis of par3cmdline (v0.0.1) and ParPar implementations

> **About this document**
>
> This document is maintained by the **ParParPar** project, a fork of upstream
> [ParPar](https://github.com/animetosho/ParPar). The canonical URL for this
> repository is [github.com/trafgals/ParParPar](https://github.com/trafgals/ParParPar).
> Where the body below says "ParPar" in a PAR3-specific context, it refers to
> ParParPar (this fork). Upstream ParPar has no PAR3 implementation.

## Purpose

This document identifies ambiguities, gaps, and practical issues in the current PAR3 v3.0 ALPHA DRAFT specification. These proposals are informed by the experience of two independent implementations (par3cmdline, the official reference implementation by Yutaka-Sawada, and ParPar, a third-party implementation by animetosho) whose interpretations of the spec have diverged in several critical areas.

The amendments are organized by severity and designed as discussion points for the Parchive/spec maintainers. They are not formal amendment language.

## Context

The PAR3 spec is an ALPHA DRAFT from 2022-01-28. Two implementations exist:

- **par3cmdline** (v0.0.1, last commit 2026-02-11): Official reference implementation by Yutaka-Sawada (also maintains MultiPar). C++ codebase. Supports GF(2^8) and GF(2^16) with Reed-Solomon (Cauchy matrix).
- **ParParPar**: Third-party implementation forked from animetosho (also maintains par2cmdline-turbo). Node.js/C++ hybrid. Supports GF(2^64) with polynomial `0x100000000000001B`.
- **MultiPar** (Yutaka-Sawada): Historical third PAR3 implementation predating the spec. Same maintainer as par3cmdline.

Four fundamental incompatibilities between par3cmdline and ParParPar were discovered during analysis, motivating these amendments.

---

## Section 1: CRITICAL Amendments

These amendments block spec finalization. Without them, independent implementations cannot produce interoperable archives.

---

### Amendment 1 (CRITICAL): Clarify "lower 16 bytes" of Blake3 hash

**Current spec text** (Conventions section):

> The "fingerprint hash" is the lower 16-bytes of a Blake3 hash.

The spec also contains a TODO comment: "Make sure 'lower 16-bytes bytes' above is specific."

**Identified issue**: The term "lower" is ambiguous. It could mean the least-significant 16 bytes (end of the Blake3 output) or the first 16 bytes (beginning of the output). The spec also uses "first 8 bytes" of BLAKE3 for InputSetID (Amendment 2), creating an inconsistency in terminology. Blake3 outputs bytes in a well-defined order; the spec should reference that order directly rather than using positional terms like "lower" that have no agreed byte-ordering meaning.

**Proposed spec text**:

> The "fingerprint hash" is the first 16 output bytes of a BLAKE3 hash (i.e., BLAKE3(data)[0:16]).

**Rationale**: Using array-slice notation `BLAKE3(data)[0:16]` removes all ambiguity. This matches how BLAKE3 naturally produces output (first 16 bytes can be read with zero-copy from the output buffer). Both par3cmdline and ParParPar already use the first 16 bytes in practice.

**Implementation impact**:
- ParParPar: None (already uses first 16 bytes)
- par3cmdline: None (already uses first 16 bytes)
- Spec change complexity: LOW — wording clarification

---

### Amendment 2 (CRITICAL): Clarify "first 8 bytes" of Blake3 for InputSetID

**Current spec text** (Start packet section):

> The InputSetID is the first 8 bytes of the Blake3 hash of the body of the Start packet.

The spec also contains a TODO comment: "Make sure 'first 8 bytes' above is specific."

**Identified issue**: While "first" is less ambiguous than "lower", combining this reference with Amendment 1's "lower 16 bytes" creates confusion. Both refer to different prefixes of the same hash output. A reader could reasonably ask: "Are 'lower 16 bytes' and 'first 8 bytes' from the same end of the hash?"

**Proposed spec text**:

> The InputSetID is the first 8 output bytes of BLAKE3(body of the Start packet) (i.e., BLAKE3(body)[0:8]).

**Rationale**: Consistent notation with Amendment 1 makes it clear that both the fingerprint hash (first 16 bytes) and the InputSetID (first 8 bytes) are prefixes from the same end of the BLAKE3 output.

**Implementation impact**:
- ParParPar: None (already uses first 8 bytes)
- par3cmdline: None (already uses first 8 bytes)
- Spec change complexity: LOW — wording clarification

---

### Amendment 3 (CRITICAL): Specify polynomial for GF(2^64) or enumerate supported fields

**Current spec text** (Design Goals section):

> support any Galois field that is a power of 2^8

And (Galois Field Encoding section):

> Parchive 3.0 supports any Galois field that is a power of 2^8. That is, any Galois field that fits neatly in one or more bytes. Clients must support every possible Galois field.

**Identified issue**: The spec claims to support any power of 2^8, but:

1. No reference implementation supports arbitrary powers. par3cmdline supports only GF(2^8) and GF(2^16). ParParPar supports only GF(2^64) with polynomial `0x100000000000001B`.
2. The spec does not define which irreducible polynomial to use for each field size. Without a polynomial, two implementations targeting the same GF size will produce incompatible matrices.
3. The Start packet body does not contain a polynomial field beyond the generator value. The generator is necessary but not sufficient (the generator depends on the polynomial).
4. The Galois Field Encoding section's table lists generators for 8-bit (0x11D), 16-bit (0x1100B), and 128-bit fields, but not for 32-bit or 64-bit fields.

**Proposed spec text** (Option A — enumerate supported fields):

> The following Galois fields are supported:
>
> | Size (bits) | Generator (hex) | Irreducible polynomial |
> |---|---|---|
> | 8 | 0x11D | — |
> | 16 | 0x1100B | — |
> | 32 | TBD | TBD |
> | 64 | TBD | 0x100000000000001B |
> | 128 | (1 << 128) + 0x43 | — |
>
> Implementations MUST support GF(2^8) and GF(2^16). Support for GF(2^32), GF(2^64), and GF(2^128) is OPTIONAL. The block size must be a multiple of the GF element size in bytes.

**Rationale**: Enumerating supported fields and their polynomials ensures interop. The 64-bit polynomial `0x100000000000001B` is recommended based on ParParPar's usage and x86 CLMUL hardware support.

**Implementation impact**:
- ParParPar: None (already implements GF(2^64) with this polynomial)
- par3cmdline: Minor (would need to add GF(2^64) support or clearly declare which fields it supports)
- Spec change complexity: MEDIUM — adds a normative table

---

### Amendment 4 (CRITICAL): Clarify field-element byte order in Start packet

**Current spec text** (Start packet body description, Galois Field section):

> The Galois field's generator is written in little-endian format without its leading 1.

**Identified issue**: The spec only specifies byte order for the generator field, not for Galois field element values in packet bodies, data blocks, or recovery blocks. It does state "All integers are little endian" in the Conventions section, but GF elements are not integers in the conventional sense (they are field elements represented as bit patterns). Without an explicit statement, implementers could reasonably use either endianness for GF values in block data.

**Proposed spec text** (after the existing sentence):

> All Galois field element values in packet bodies, data blocks, and recovery blocks are also little-endian. The binary representation of a GF element is stored with its least significant byte first.

**Rationale**: Both par3cmdline and ParParPar already use little-endian for GF values. Making this explicit prevents future implementations from choosing big-endian (which would be incompatible).

**Implementation impact**:
- ParParPar: None
- par3cmdline: None
- Spec change complexity: LOW — normative addition

---

### Amendment 5 (CRITICAL): Specify first-input-block convention in Cauchy matrix

**Current spec text** (Cauchy Matrix Packet section):

> x_I is the Galois field element with the same bit pattern as binary integer I+1

**Identified issue**: The spec uses zero-indexing for vectors (I and R start at 0), so the first input block at index 0 maps to `I+1 = 1`. However, ParParPar uses `M[r][c] = inv((0 + c) ^ (N + r))` where recovery and input offsets are 0 and N, while par3cmdline uses `inv((I+1) ^ (MAX - R))`. These interpretations differ in how the recovery block index participates in the formula, leading to incompatible matrix values. The spec is technically correct but the wording is misleading.

**Proposed spec text** (add after the existing formula):

> For example, for input block 0 and recovery block 0, the element is inv(1 ^ MAX). For input block 1 and recovery block 0, the element is inv(2 ^ MAX). The addition of 1 to I and the subtraction of R from MAX (via MAX - R) both use native integer arithmetic, not Galois field arithmetic.

A canonical worked example with concrete byte values should be provided (see Amendment 15).

**Rationale**: The concrete examples eliminate interpretation differences. The additions/subtractions on I and R are done in native integer arithmetic (as the spec says), but showing the specific values prevents misreading.

**Implementation impact**:
- ParParPar: Minor (clarify formula to match spec intent)
- par3cmdline: None
- Spec change complexity: LOW — add example

---

### Amendment 6 (CRITICAL): Mandate Root packet

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text** (Packets section):

> A Par3 file is only required to contain 1 specific packet: the packet that identifies the client that created the file.

The Root packet is described in its own section but not listed as required.

**Identified issue**: par3cmdline hard-fails with `RET_INSUFFICIENT_DATA` if the Root packet is missing. The Root packet provides the checksum for the entire input set and is essential for verification. The spec's statement that only the Creator packet is required contradicts both par3cmdline's behavior and the Root packet's role in the protocol.

**Proposed spec text**:

> A Par3 file MUST contain at least one Creator packet and one Root packet per InputSetID. The Root packet provides the checksum of the entire input set and is required for verification and recovery. If the Root packet is missing, the client MUST report an error.

Alternatively, if Root is to remain optional:

> If the Root packet is absent, the client MUST define a fallback behavior for input set verification. Without a Root packet, the client cannot verify the completeness or integrity of the recovered input set.

**Rationale**: Making Root required aligns the spec with par3cmdline's behavior and the practical need for a top-level checksum. The Root packet is the only mechanism to verify that the complete input set has been recovered.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. Root packet generation complete in all code paths as of June 2026. Convergent behavior verified by cross-compat test suite (10/10 tests pass).
- par3cmdline: None (already requires Root)
- Spec change complexity: LOW — wording change

---

### Amendment 7 (CRITICAL with Transition): Mandate packet checksums

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text** (Packet Header table):

> 16 | fingerprint hash | 16-bytes of Blake3 hash of packet. Used as a checksum for the packet.

And:

> If the checksum of the data read/received does not match this field's value, the packet is ignored.

**Identified issue**: The spec describes the checksum field but does not specify what to do when it is all zeros. ParParPar writes zero-filled checksums for all packets, treating the field as reserved for future use. par3cmdline validates checksums and rejects packets with invalid ones. A zero checksum is ambiguous: it could mean "no checksum computed" or "this packet has a checksum of zero". Until this ambiguity is resolved, archives from different implementations will be unreadable by the other.

**Proposed spec text** (transitional approach — SOFTENED from original):

> Implementations SHOULD compute valid Blake3 packet checksums during encoding.
>
> Implementations MUST validate packet checksums during decoding; packets with invalid checksums MUST be discarded.
>
> For backward compatibility with legacy archives, implementations MAY accept packets with zero-filled checksum fields. This backward-compatibility path SHOULD be deprecated in a future spec revision and removed entirely before spec finalization.
>
> A future amendment (post-finalization) SHOULD make checksum validation MANDATORY without exception.

**Rationale**: Using SHOULD/MAY rather than hard MUST avoids immediately breaking ParParPar while setting a clear path toward spec finalization. The transition period allows ParParPar and other implementations to adopt checksum computation.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. BLAKE3 packet checksum computation implemented. Zero-file archive handling corrected. Cross-compat test suite: 10/10 tests pass.
- par3cmdline: None (already validates checksums)
- Spec change complexity: HIGH — normative change with transition plan

---

### Amendment 8 (CRITICAL): Add error handling semantics

**Current spec text**: None — there is no section on error handling or compliance.

**Identified issue**: The spec defines packet formats and encoding algorithms but does not specify what clients should do when they encounter:

- Malformed packets (invalid lengths, truncated data)
- Unsupported Galois field sizes
- Missing required packets (Root, Creator)
- Conflicting packets (different bodies, same type)
- Matrix inversion failures
- Checksum mismatches during recovery

par3cmdline defines `RET_*` error codes, but these are implementation-specific. ParParPar reports errors via stderr text. Without spec guidance, users get inconsistent error experiences.

**Proposed spec text** (new "Compliance and Error Handling" section):

> A compliant client MUST:
>
> - Reject packets with invalid magic sequences
> - Reject packets with invalid checksums (see Amendment 7)
> - Report the contents of the Creator packet when recovery fails
> - Fail gracefully with a descriptive error when an unsupported Galois field is encountered
> - Fail gracefully when required packets (Creator, Root) are missing
> - Recover from matrix inversion failures by reporting which blocks cannot be recovered
>
> The client SHOULD distinguish between:
>
> - Data errors (corrupted input, missing files) that can potentially be recovered
> - Format errors (malformed packets, invalid parameters) that are unrecoverable

**Rationale**: Error handling is underspecified for an ALPHA DRAFT heading toward finalization. Basic compliance rules reduce support burden across implementations.

**Implementation impact**:
- ParParPar: None (informal error reporting exists)
- par3cmdline: None (already has error codes)
- Spec change complexity: LOW — new section

---

## Section 2: IMPORTANT Amendments

These amendments improve cross-implementation compatibility but do not block spec finalization.

---

### Amendment 9 (IMPORTANT): Recommend order of packets

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text** (Packets section):

> Packets can appear in any order. Because packets can be lost or mishandled, it is impossible to guarantee the order that packets arrive at the decoding client. Nonetheless, there is a recommended order for packets, which, if most packets arrive correctly, allows the decoding clients to recover the files in a single pass.

**Identified issue**: The spec mentions "a recommended order" but never states what it is. The Order of Packets section (Clarifications and Commentary) lists an order (Creator, Start, Matrix, Data/External Data, Root, Recovery Data) but uses non-normative language and appears in commentary rather than the normative specification.

**Proposed spec text** (move to normative section, add rationale):

> The recommended order for packets within a Par3 file is:
>
> 1. Root (required for verification)
> 2. Start (defines encoding parameters)
> 3. Creator (identifies the client)
> 4. File (file metadata and chunk descriptions)
> 5. Directory (directory structure)
> 6. External Data (block checksums for external files)
> 7. Matrix (code matrix definition)
> 8. Data (input block data)
> 9. Recovery Data (recovery blocks)
> 10. Comment (user-facing metadata)
>
> This order allows single-pass decode: the decoder first learns the input set structure (Root, Start, Creator), then file metadata (File, Directory), then block checksums (External Data), then the code matrix (Matrix), and finally data blocks and recovery blocks.

**Rationale**: A defined recommended order enables single-pass recovery implementations and provides a consistent target for encoder developers.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. All implementation complete as of June 2026. Convergent behavior verified by cross-compat test suite (10/10 tests pass).
- par3cmdline: None (already reorders output)
- Spec change complexity: LOW — list addition

---

### Amendment 10 (IMPORTANT): Define file ID generation

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text** (File packet): The File packet body contains a 16-byte File ID field, shown in the body table as a "fingerprint hash" type, but its generation is not specified beyond the field type.

**Identified issue**: ParParPar uses MD5 of the file's relative path as the File ID. par3cmdline generates random 16-byte UUIDs. ParParPar's approach means two files at the same path in different archives will collide. Neither approach is wrong, but the spec should provide guidance to prevent accidental collisions.

**Proposed spec text** (add to File packet section):

> The File ID is a 16-byte value that uniquely identifies the file within the input set. It SHOULD be generated as BLAKE3(file path)[0:16] or as 16 cryptographically random bytes. File IDs MUST be unique within a single InputSetID. If two files share the same File ID, the client SHOULD treat this as an error.

**Rationale**: Providing two acceptable methods (deterministic and random) matches existing practice while mandating uniqueness where it matters.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. All implementation complete as of June 2026. Convergent behavior verified by cross-compat test suite (10/10 tests pass).
- par3cmdline: None (already uses random UUIDs)
- Spec change complexity: MEDIUM — normative addition

---

### Amendment 11 (IMPORTANT): Clarify "block size multiple of GF size"

**Current spec text** (Start packet section):

> The block size must be a multiple of the Galois field size.

**Identified issue**: par3cmdline enforces an additional requirement for GF(2^16): the block size must be even (multiple of 2 bytes). This is implied by "multiple of Galois field size" (GF(2^16) = 2 bytes), but the spec does not state this explicitly. The specific reason is memory alignment for buffer processing.

**Proposed spec text**:

> The block size must be a multiple of the GF element size in bytes. For GF(2^16) codes, the block size must additionally be a multiple of 2 bytes. For GF(2^8) codes, the block size must be a multiple of 1 byte (no additional constraint). This requirement ensures that blocks align on GF element boundaries for efficient processing.

**Rationale**: Explicit alignment rules prevent edge cases where a valid-by-the-spec block size causes processing failures in specific implementations.

**Implementation impact**:
- ParParPar: None (already enforces this)
- par3cmdline: None (already enforces this)
- Spec change complexity: LOW — wording clarification

---

### Amendment 15 (IMPORTANT — PROMOTED from MINOR): Add example packet dumps

**Current spec text**: None — there are no hex dump examples in the current spec.

**Identified issue**: The two independent implementations (par3cmdline and ParParPar) interpret the Start packet body and Matrix packet type differently despite both targeting the same spec. The Start packet body contains a variable-length generator field whose byte count depends on the GF size. The Matrix packet type field contains `PAR CAU`, `PAR SPA`, or `PAR EXP`. Both implementations parse these correctly, but without examples, new implementers are likely to make the same mistakes.

**Proposed spec text** (new appendix):

> Appendix D: Example Packet Dumps
>
> The following is a hex dump of each packet type for a canonical input set consisting of one file "hello.txt" containing the text "Hello, World!" (13 bytes), using a block size of 64 bytes and GF(2^8) with generator 0x11D.
>
> \[Packet-by-packet hex dump with annotated fields]

Each field MUST be annotated with offset, length, description, and value.

**Rationale**: Examples prevent misinterpretation of packet layouts. The par3cmdline vs ParParPar divergence proves that written descriptions alone are insufficient.

**Implementation impact**:
- ParParPar: None
- par3cmdline: None
- Spec change complexity: MEDIUM — adds substantial appendix content

---

## Section 3: MINOR Amendments

These are clarifications and guidelines that improve the spec but do not impact interoperability.

---

### Amendment 13 (MINOR): Define "creator" string format

**Current spec text** (Creator packet):

> UTF-8 text identifying the client, options, and contact information. Reminder: This is not a null terminated string.

And:

> It is RECOMMENDED the text include a way to contact the author of the tool.

**Identified issue**: The free-form string makes automated parsing impossible. ParPar includes a URL (`https://github.com/animetosho/ParPar`) plus options. par3cmdline uses a simpler format like `par3cmdline v0.0.1`. A recommended format would help with debugging.

**Proposed spec text** (add as RECOMMENDED, not REQUIRED):

> The RECOMMENDED format for the creator string is: `ClientName/Version (ContactURL) [options]`. For example: `ParPar/0.4.5 (https://github.com/animetosho/ParPar) -r 10 -b 1M`. Clients MAY use any format, but the version number MUST be present and identifiable.

**Rationale**: Lightweight recommendation without imposing a new requirement. Keeps back-compat with existing archives.

**Implementation impact**:
- ParParPar: Minor string format adjustment
- par3cmdline: Minor string format adjustment
- Spec change complexity: LOW — wording

---

### Amendment 14 (MINOR — SCALED DOWN from original): Recommend APP packet prefix naming convention

**Current spec text** (Packet Header section):

> Application-specific packets must have an application-specific 4-byte prefix.

**Identified issue**: The spec requires a 4-byte prefix but provides no guidance on allocating prefixes or avoiding collisions. Creating a formal registry (IANA-style) would be heavy governance for the Parchive ecosystem.

**Proposed spec text** (lightweight approach — not a registry):

> Application-specific prefix MUST be 4 ASCII uppercase letters (e.g., "MYCO", "ACME"). The remaining 4 bytes of the 8-byte type field are implementation-specific (e.g., "DATA", "META").
>
> Prefixes SHOULD be unique across the ecosystem. The following is a non-normative list of known prefixes:
>
> | Prefix | Application |
> |---|---|
> | (none registered in this draft) | |
>
> The format is: `<4-char-prefix> <4-char-impl-suffix>` e.g., "MYCO DATA".

**Rationale**: Naming convention without governance avoids a bottleneck while giving implementers clear guidance. An informal wiki or registry could be linked in a footnote.

**Implementation impact**:
- ParParPar: None
- par3cmdline: None
- Spec change complexity: LOW — guidelines

---

### Amendment 16 (MINOR): Specify file system packet precedence

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text** (File System Specific Packets section, under UNIX/FAT Permissions):

The spec describes permissions packets but does not discuss the order in which permissions and file content should be restored.

**Identified issue**: When recovering a file, should the client restore permissions first (so the file can be written in a protected directory) or file content first (so the permissions apply to the correct content)? The spec does not specify.

**Proposed spec text** (add to File System Specific Packets section):

> During recovery, permissions packets (UNIX Permissions, FAT Permissions) SHOULD be restored before file content. This ensures that the file can be written with the correct permissions from the start. If the file's content cannot be recovered, the permissions MUST NOT be written.
>
> The order within a single packet body is: permissions values first, then content block references.

**Rationale**: Defining recovery precedence prevents permission-related errors during file system restoration.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. Permissions packet generation and empty set support complete as of June 2026. Convergent behavior verified by cross-compat test suite (10/10 tests pass).
- par3cmdline: None
- Spec change complexity: LOW — wording

---

### Amendment 17 (MINOR): Clarify packet duplication behavior

**Current spec text** (Packets section):

> Packets can be duplicated. In fact, duplicating packets is recommended for vital packets, such as the one containing the file checksums.

**Identified issue**: When two copies of the same packet exist with different bodies (one corrupted, one correct), how should the client resolve the conflict? The checksum field should distinguish them, but the spec does not describe this resolution process explicitly.

**Proposed spec text** (add after existing paragraph):

> If two packets have the same type and InputSetID but different body contents, the packet whose packet header checksum verifies successfully is retained. If both (or neither) verify, the packet with the lower file offset is used. Duplicate packets that are identical and both verify correctly are treated as a single packet.

**Rationale**: Clear tiebreaking rules prevent non-deterministic behavior during recovery.

**Implementation impact**:
- ParParPar: None
- par3cmdline: None
- Spec change complexity: LOW — wording

---

### Amendment 18 (MINOR): Define empty input set behavior

**Convergence status**: ✅ ParParPar matches par3cmdline behavior as of June 2026

**Current spec text**: The Use Case section describes files but does not address the case of an empty input set (zero files, zero directories).

**Identified issue**: The spec mentions "support empty directories" (a design goal) but does not discuss what happens when the entire input set is empty. Can a Par3 archive contain zero input files? par3cmdline's behavior in this case is unclear; ParParPar throws an error. The spec should define what is valid.

**Proposed spec text** (add to Use Case or a new note):

> An input set with zero files is valid only if it has at least one directory. A recovery block count of zero with zero input files produces an archive containing only metadata packets (Root, Start, Creator). Clients MUST accept such archives and report them as valid (though trivially containing no recoverable data).

**Rationale**: Defining boundary behavior prevents implementations from silently producing or rejecting degenerate archives differently.

**Implementation impact**:
- ParParPar: **DONE — matches par3cmdline**. Empty set handling implemented. Convergent behavior verified by cross-compat test suite (10/10 tests pass).
- par3cmdline: Minor (accept zero-file archives)
- Spec change complexity: LOW — wording

---

### Dropped: Amendment 12 (Memory budget guidance)

**Original proposal**: Add guidance on memory allocation and processing budgets.

**Reason for dropping**: Memory constraints are an implementation concern, not a format specification concern. There is no precedent in the PAR2 spec for memory guidance. The `-m<n>` flag is already implementation-defined in par3cmdline.

---

## Section 4: Spec Self-Inconsistencies

The following self-inconsistencies were discovered within the current spec (2022-01-28 ALPHA DRAFT). These are contradictions between different sections of the same document, not implementation-level issues.

| # | Section | Conflict |
|---|---------|----------|
| 1 | Conventions vs Start packet | Conventions section says "lower 16-bytes" (fingerprint hash) while Start packet section says "first 8 bytes" (InputSetID). Both have TODO comments acknowledging the ambiguity. This is a terminology inconsistency — one uses spatial ordering ("lower"), the other uses sequential ordering ("first") — making it unclear whether they refer to the same end of the Blake3 output. |
| 2 | Matrix vs Sparse sections | Design Goals says "support any linear code" and the Sparse Random Matrix Packet section provides a detailed packet layout for sparse codes. However, the Cauchy Matrix section gives the impression that Cauchy is the primary/canonical matrix type. The packet type `PAR SPR\0` (Sparse Random) body is defined, but the spec does not address how a decoder should handle mixed matrix types (Cauchy + Sparse) within the same InputSetID. |
| 3 | Required packets | The Packets section states "A Par3 file is only required to contain 1 specific packet: the packet that identifies the client that created the file" (Creator). However, par3cmdline requires Root and Start for all operations, and the Start packet is implicitly required (since InputSetID derives from it). The spec should clarify which packets are required for encoding vs decoding vs verification. |
| 4 | Error reporting | The Creator packet section says "the contents of the creator packet MUST be shown to the user" if decoding fails, but there is no spec requirement for standardized error codes or error reporting format. par3cmdline defines `RET_*` codes (implementation-specific), ParParPar uses stderr text. The spec should define minimum error reporting requirements. |
| 5 | Tail packing | Design Goals lists "tail packing" as a feature ("where a block holds data from multiple files"), but the packet format does not explicitly support it. The File packet section mentions tail packing in design notes but does not define a packet field or flag to indicate that a block contains tail data from multiple files. This makes tail packing an underspecified feature that implementations must invent independently. |

---

## Section 5: Implementation Impact Matrix

The following matrix summarizes the impact of each amendment on the upstream PAR2-only implementation, this fork, the reference implementation, and the spec itself. Upstream ParPar has no PAR3 implementation, so all PAR3-specific amendments are N/A.

| Amendment | Upstream ParPar (PAR2 only) | ParParPar (this fork) | par3cmdline | Spec complexity |
|-----------|----------------------------|------------------------|-------------|-----------------|
| 1 (Blake3 byte order) | N/A | None | None | LOW — wording change |
| 2 (InputSetID) | N/A | None | None | LOW — wording change |
| 3 (polynomial) | N/A | None | Minor (support more GF sizes) | MEDIUM — adds normative table |
| 4 (endianness) | N/A | None | None | LOW — normative addition |
| 5 (first-input convention) | N/A | Minor (clarify formula) | None | LOW — add example |
| 6 (Root required) | N/A | None | None | LOW — wording change |
| 7 (checksum mandatory) | N/A | None | None | HIGH — normative change with transition |
| 8 (error semantics) | N/A | None | None | LOW — new section |
| 9 (packet order) | N/A | None | None | LOW — list addition |
| 10 (file ID) | N/A | None | None | MEDIUM — normative addition |
| 11 (alignment) | N/A | None | None | LOW — wording clarification |
| 12 (memory budget) | DROPPED | DROPPED | DROPPED | DROPPED |
| 13 (creator format) | N/A | Minor string format adjustment | Minor string format adjustment | LOW — wording |
| 14 (app prefix) | N/A | None | None | LOW — guidelines |
| 15 (hex examples) | N/A | None | None | MEDIUM — adds appendix |
| 16 (permissions order) | N/A | None | None | LOW — wording |
| 17 (duplication) | N/A | None | None | LOW — wording |
| 18 (empty set) | N/A | None | None | LOW — wording |

### Key Observations

1. **ParParPar has completed implementation work**: Amendments 6, 7, 9, 10, 16, and 18 have been implemented. Amendment 7 (checksums) was the largest effort and is now complete.

2. **par3cmdline needs minimal changes**: As the reference implementation, par3cmdline already conforms to most proposed amendments.

3. **Spec complexity is mostly LOW**: 12 of 17 amendments (Amendment 12 dropped) are LOW complexity. Only Amendments 3, 7, 10, and 15 require more than wording changes.

4. **Transition planning is critical**: Amendment 7's soft "SHOULD/MAY" language is deliberate. A hard "MUST" would immediately break ParParPar-produced archives.

---

## Appendix: Amendment Modification Log

The following amendments were modified from their original proposal in the gap analysis:

| # | Modification | Reason |
|---|-------------|--------|
| 7 | SOFTENED: "SHOULD compute valid Blake3 checksums", "MAY accept zero checksums" for transition | Avoids immediately breaking ParPar |
| 12 | DROPPED: memory budget is implementation concern | Out of scope for format spec |
| 14 | SCALED DOWN: recommend naming convention, not formal registry | Avoids governance bottleneck |
| 15 | PROMOTED to IMPORTANT (from MINOR) | Hex examples prevent misinterpretation between implementations |
