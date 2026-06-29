"use strict";

// TDD red for fix-gf64-recovery — Todo 2
// Captures the cross-compat CAU/MATRIX parser bug at lib/par3gen.js:1444-1455.
// On master HEAD, par3_verify() fails when parsing a par3cmdline-style 24-byte
// CAU body because the parser unconditionally reads at offset 24
// (recovery_count field) from a 24-byte buffer.
//
// After Todo 5 fixes the verify-path parser with a body.length >= 32 guard, this
// test PASSES.
//
// This test does NOT depend on any other test files. It builds a minimal PAR3
// archive in memory with a 24-byte CAU body (par3cmdline format) and asserts
// par3.verify() handles it gracefully.
//
// Bug summary:
//   In lib/par3gen.js:1433-1444 (verify path), the CAU/MATRIX packet handler
//   reads 4 uint64 fields from the body:
//     body[ 0..8 ]  = first_input
//     body[ 8..16]  = last_input
//     body[16..24]  = first_recovery
//     body[24..32]  = recovery_count     <-- reads past end of 24-byte body
//   par3cmdline emits CAU bodies without the recovery_count field (24 bytes
//   total). The unconditional readUInt64LE(body, offset + 24) throws
//   "RangeError: The value of \"offset\" is out of range." which the parser
//   pipeline catches and surfaces as the async callback error
//   "Failed to parse PAR3 file: The value of \"offset\" is out of range...".
//
// Trigger:
//   A minimal PAR3 archive containing a CRE packet, a STA packet, and a CAU
//   packet with a 24-byte body. Any data block size; the CAU body shape alone
//   is enough to trigger the read-out-of-bounds.

var fs = require('fs');
var path = require('path');
var os = require('os');
var par3 = require('../lib/par3gen.js');

function makeTempDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'parpar-unit-cross-compat-'));
}

function rmrf(dir) {
  if (!dir || dir === '/') return;
  try {
    fs.readdirSync(dir).forEach(function(name) {
      var p = path.join(dir, name);
      var stat = fs.statSync(p);
      if (stat.isDirectory()) rmrf(p);
      else fs.unlinkSync(p);
    });
    fs.rmdirSync(dir);
  } catch (e) { /* ignore */ }
}

// Build a single PAR3 packet (48-byte header + body) with zero checksum.
// The validatePacketChecksum helper treats all-zero checksum bytes as "skip".
function makePacket(typeStr, body) {
  var pkt = Buffer.alloc(48 + body.length);
  pkt.write('PAR3\0PKT', 0);                 // magic
  // checksum bytes (offset 8-24) left zero — validator returns true for zero
  pkt.writeBigUInt64LE(BigInt(48 + body.length), 24); // packet length
  pkt.write(typeStr, 40, 8, 'ascii');        // type
  body.copy(pkt, 48);
  return pkt;
}

// Build a minimal PAR3 archive with CRE + STA + CAU packets.
// CAU body is 24 bytes (par3cmdline format): first_input, last_input,
// first_recovery only — no recovery_count.
function buildMinimalArchive() {
  // CRE body (16 bytes): input set ID (uint64) + spare (uint64)
  var creBody = Buffer.alloc(16);
  creBody.writeBigUInt64LE(0x123456789abcdef0n, 0);

  // STA body (34 bytes): gf_size(1) + reserved(15) + block_size(8) + block_pow(4) + spare(6)
  var staBody = Buffer.alloc(34);
  staBody[0] = 8;                                       // gf_size
  staBody.writeBigUInt64LE(1048576n, 16);               // block_size at body offset 16
  staBody.writeUInt32LE(20, 24);                        // block_pow at body offset 24

  // CAU body (24 bytes — par3cmdline format):
  //   first_input     uint64  (body[0..8])
  //   last_input      uint64  (body[8..16])
  //   first_recovery  uint64  (body[16..24])
  // No recovery_count field — par3cmdline omits it.
  var cauBody = Buffer.alloc(24);
  cauBody.writeBigUInt64LE(0n, 0);                     // first_input = 0
  cauBody.writeBigUInt64LE(99n, 8);                    // last_input = 99
  cauBody.writeBigUInt64LE(100n, 16);                  // first_recovery = 100

  return Buffer.concat([
    makePacket('PAR CRE\0', creBody),
    makePacket('PAR STA\0', staBody),
    makePacket('PAR CAU\0', cauBody),
  ]);
}

function run() {
  var tempDir = makeTempDir();
  var archiveFile = path.join(tempDir, 'test.par3');
  var archive = buildMinimalArchive();
  fs.writeFileSync(archiveFile, archive);

  // The CAU parser bug can surface two ways depending on Node version and
  // call path:
  //   1) Synchronous RangeError thrown out of par3.verify (older Node).
  //   2) Async callback error from par3_parse_stream's try/catch wrapping
  //      the packetCallback — message starts with "Failed to parse PAR3 file:"
  //      and contains the underlying "out of range" RangeError text.
  // We detect either pattern and report it as the same bug.

  // par3.verify reads via fs.open/fs.read so the callback is async;
  // wrap in a Promise so we don't exit before it fires.
  return new Promise(function(resolve) {
    var observedError = null;
    var threwSynchronously = false;

    try {
      par3.verify(archiveFile, function(err, result) {
        if (err) observedError = err;
        resolve({ observedError: observedError, threwSynchronously: threwSynchronously });
      });
    } catch (e) {
      threwSynchronously = true;
      observedError = e;
      resolve({ observedError: observedError, threwSynchronously: threwSynchronously });
    }
  }).then(function(outcome) {
    try {
      var observedError = outcome.observedError;
      var threwSynchronously = outcome.threwSynchronously;

      if (!observedError) {
        // The test FAILS when the parser actually handles the short body
        // gracefully (i.e., the bug has been fixed).
        console.log('OK: par3_verify handled 24-byte CAU body without error');
        return 0;
      }

      var msg = observedError.message || String(observedError);
      var isRangeError = (observedError instanceof RangeError) ||
                         /RangeError|out of range/i.test(msg);

      if (!isRangeError) {
        // Different exception type — propagate so the user notices (not a
        // silent pass). This still indicates a parser issue, just one we
        // didn't predict.
        throw new Error(
          'par3_verify failed on par3cmdline 24-byte CAU body, but with an unexpected error.\n' +
          '  Error type:   ' + observedError.constructor.name + '\n' +
          '  Error message:' + msg + '\n' +
          '  Synchronous:  ' + threwSynchronously + '\n' +
          'Expected: RangeError "out of range" or async callback error wrapping it.\n' +
          'Root cause candidate: lib/par3gen.js:1439 reads at offset 24 from a 24-byte buffer.'
        );
      }

      // Bug present — surface the regression message.
      throw new Error(
        'par3_verify throws RangeError on par3cmdline 24-byte CAU body.\n' +
        '  Error message: ' + msg + '\n' +
        '  Synchronous:   ' + threwSynchronously + '\n' +
        'Root cause: lib/par3gen.js:1439 readUInt64LE(body, offset + 24) reads past end of 24-byte CAU body.\n' +
        'Fix (Todo 5): add body.length >= 32 guard before reading recovery_count; compute recoveryCount from\n' +
        '  available recovery blocks for short bodies.'
      );
    } catch (err) {
      console.error('TEST FAILED:', err.message);
      return 1;
    } finally {
      rmrf(tempDir);
    }
  });
}

run().then(function(exitCode) {
  process.exit(exitCode);
});
