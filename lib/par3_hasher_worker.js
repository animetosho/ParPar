"use strict";

// PAR3 per-block packet checksum worker (B3, par3-1200mbps plan)
// Computes the BLAKE3 packet checksum that finalizePacketHeader()
// computes inline in lib/par3gen.js. Hash function and input layout
// MUST stay bit-exact compatible with the inline path:
//
//   blake3.createHash().update(afterChecksum)
//                        .update(blockIndex_LE)
//                        .update(blockData)
//                        .digest().slice(0, 16)
//
// Contract:
//   In:  { blockIndex, blockData, afterChecksum } via postMessage.
//        blockIndex   Number (<= 2^31 per PAR3 input-block cap).
//        blockData    Buffer (blockSize bytes), transferred via transferList.
//        afterChecksum Buffer (24 bytes) = length[8] + inputSetId[8] + type[8].
//   Out: { blockIndex, hash } via postMessage (16-byte truncated BLAKE3).
//   On error: { error, blockIndex } via postMessage.
//
// Pool contract (managed by lib/par3gen.js):
//   One worker per pool slot; long-lived, reused across creates.
//   Pool handles dispatch (round-robin or queue), ordering, crash recovery.
//   This worker is a single-purpose postMessage loop: no buffering, no
//   batching, no dependency on lib/par3gen.js state.

var blake3 = require('blake3');
var { parentPort } = require('worker_threads');

if(!parentPort) {
	// Defensive guard — must only be loaded via new worker_threads.Worker().
	throw new Error('par3_hasher_worker must be loaded via worker_threads.Worker');
}

parentPort.on('message', function(msg) {
	try {
		var blockIndex = msg.blockIndex;
		var blockData = msg.blockData;
		var afterChecksum = msg.afterChecksum;

		// Hash input MUST match the inline path byte-for-byte. The inline
		// body's layout is packet[48..] = blockIndex_LE(8) + blockData(blockSize);
		// we reconstruct the same bytes here. PAR3 caps totalBlocks at
		// 2^31 - 1 (see lib/par3gen.js), so blockIndex always fits in a
		// Number's safe-integer range; writeBigUInt64LE produces identical
		// bytes to the inline writeUInt64LE in both halves.
		var blockIndexBuf = Buffer.alloc(8);
		blockIndexBuf.writeBigUInt64LE(BigInt(blockIndex), 0);

		var hash = blake3.createHash()
			.update(afterChecksum)
			.update(blockIndexBuf)
			.update(blockData)
			.digest()
			.slice(0, 16);

		parentPort.postMessage({ blockIndex: blockIndex, hash: hash });
	} catch(e) {
		try {
			parentPort.postMessage({
				error: (e && e.message) ? e.message : String(e),
				blockIndex: msg && msg.blockIndex
			});
		} catch(_) {
			// best-effort error delivery
		}
	}
});
