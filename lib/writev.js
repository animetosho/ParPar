// expose node's internal fs.writev function
"use strict";

var fs = require('fs');
var binding = process.binding('fs');

if(fs.writev) { // properly exposed in API in node v12.9.0
  module.exports = fs.writev;
}
else if(binding && binding.writeBuffers) { // node >= 4, native writev available
  // function copied from lib/fs.js in node's sources
  module.exports = function writev(fd, chunks, position, callback) {
    function wrapper(err, written) {
      // Retain a reference to chunks so that they can't be GC'ed too soon.
      callback(err, written || 0, chunks);
    }

    var req = binding.FSReqCallback ? new binding.FSReqCallback() : new binding.FSReqWrap();
    req.oncomplete = wrapper;
    binding.writeBuffers(fd, chunks, position, req);
  }
} else { // emulate writev with write calls
  // TODO: consider optimisation of concatenating chunks
  
  var asyncWrite = function(fd, chunks, i, position, writeTotal, callback) {
    var len = chunks[i].length;
    fs.write(fd, chunks[i], 0, len, position, function(err, written) {
      if(err) return callback(err);
      writeTotal += written;
      if(++i === chunks.length) {
        callback(null, writeTotal, chunks);
      } else {
        asyncWrite(fd, chunks, i, position === null ? null : position + len, writeTotal, callback);
      }
    });
  }
  module.exports = function writev(fd, chunks, position, callback) {
    // TODO: check that all chunks are Buffers?
    
    if(typeof callback === 'function') {
      if(chunks.length)
        asyncWrite(fd, chunks, 0, position, 0, callback);
      else
        process.nextTick(callback.bind(null, null, 0, chunks));
    } else {
      chunks.forEach(function(chunk) {
        fs.writeSync(fd, chunk, 0, chunk.length, position);
        if(position !== null) position += chunk.length;
      });
    }
    
  }
}
