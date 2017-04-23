// expose node's internal fs.writev function
"use strict";

var binding = process.binding('fs');

if(binding.writeBuffers) { // node >= 4, native writev available
  // function copied from lib/fs.js in node's sources
  module.exports = function writev(fd, chunks, position, callback) {
    function wrapper(err, written) {
      // Retain a reference to chunks so that they can't be GC'ed too soon.
      callback(err, written || 0, chunks);
    }

    var req = new binding.FSReqWrap();
    req.oncomplete = wrapper;
    binding.writeBuffers(fd, chunks, position, req);
  }
} else { // emulate writev with write calls
  var fs = require('fs');
  // TODO: consider optimisation of concatenating chunks
  
  var asyncWrite = function(fd, chunks, i, position, writeTotal, callback) {
    fs.write(fd, chunks[i], 0, chunks[i].length, position, function(err, written) {
      if(err) return callback(err);
      writeTotal += written;
      if(++i === chunks.length) {
        callback(null, writeTotal, chunks);
      } else {
        asyncWrite(fd, chunks, i, null, writeTotal, callback);
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
        position = null;
      });
    }
    
  }
}
