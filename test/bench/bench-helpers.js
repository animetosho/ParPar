"use strict";

var path = require('path');
var fs = require('fs');
var os = require('os');
var crypto = require('crypto');

var DEFAULT_SIZE = 1 * 1024 * 1024 * 1024; // 1 GiB

function parseSize(s) {
  if (s == null) return DEFAULT_SIZE;
  if (/^\d+$/.test(String(s))) return parseInt(s, 10);
  var m = String(s).match(/^(\d+(?:\.\d+)?)\s*([KMGT]?I?B?)$/i);
  if (!m) return DEFAULT_SIZE;
  var n = parseFloat(m[1]);
  var unit = (m[2] || '').toUpperCase();
  var mult = {
    '': 1, B: 1,
    K: 1024, KB: 1024, KI: 1024, KIB: 1024,
    M: 1024 * 1024, MB: 1024 * 1024, MI: 1024 * 1024, MIB: 1024 * 1024,
    G: 1024 * 1024 * 1024, GB: 1024 * 1024 * 1024, GI: 1024 * 1024 * 1024, GIB: 1024 * 1024 * 1024,
    T: 1024 * 1024 * 1024 * 1024, TB: 1024 * 1024 * 1024 * 1024, TI: 1024 * 1024 * 1024 * 1024, TIB: 1024 * 1024 * 1024 * 1024
  };
  return Math.floor(n * mult[unit]);
}

var SOURCE_KEY = crypto.createHash('sha256').update('parpar-bench-source-v1').digest();
var SOURCE_IV = Buffer.alloc(16, 0);

function createBenchSource(size, filePath) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  var fd = fs.openSync(filePath, 'w');
  var cipher = crypto.createCipheriv('aes-256-ctr', SOURCE_KEY, SOURCE_IV);
  var chunkSize = 64 * 1024 * 1024;
  var remaining = size;
  while (remaining > 0) {
    var toWrite = Math.min(chunkSize, remaining);
    var zeroBuf = Buffer.alloc(toWrite);
    var keystream = cipher.update(zeroBuf);
    fs.writeSync(fd, keystream, 0, toWrite, size - remaining);
    remaining -= toWrite;
  }
  cipher.final();
  fs.fsyncSync(fd);
  fs.closeSync(fd);
}

function formatBytes(b) {
  var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
  for (var i = 0; i < units.length; i++) {
    if (b < 1024 || i === units.length - 1) break;
    b /= 1024;
  }
  return b.toFixed(2) + ' ' + units[i];
}

function formatDuration(ms) {
  if (ms < 1000) return ms.toFixed(0) + 'ms';
  if (ms < 60000) return (ms / 1000).toFixed(2) + 's';
  return (ms / 60000).toFixed(2) + 'm';
}

function getTempDir(prefix) {
  return fs.mkdtempSync(path.join(os.tmpdir(), (prefix || 'parpar-bench-') + '-'));
}

function cleanup(dir) {
  if (!dir || dir === '/') return;
  try {
    var files = fs.readdirSync(dir);
    for (var i = 0; i < files.length; i++) {
      var fp = path.join(dir, files[i]);
      try { fs.unlinkSync(fp); } catch (e) {}
    }
    fs.rmdirSync(dir);
  } catch (e) {}
}

function ensureGfMethod() {
  var gf16 = 'unavailable';
  var gf64 = 'unavailable';
  try {
    var gfAddon = require('../../build/Release/parpar_gf.node');
    var info16 = gfAddon.gf_info(0);
    if (info16 && info16.name) gf16 = info16.name;
  } catch (e) {}
  try {
    var gf64Addon = require('../../build/Release/parpar_gf64.node');
    var info64 = gf64Addon.gf64_info(0);
    if (info64 && info64.name) gf64 = info64.name;
  } catch (e) {}
  return { gf16: gf16, gf64: gf64 };
}

module.exports = {
  parseSize: parseSize,
  createBenchSource: createBenchSource,
  formatBytes: formatBytes,
  formatDuration: formatDuration,
  getTempDir: getTempDir,
  cleanup: cleanup,
  ensureGfMethod: ensureGfMethod
};
