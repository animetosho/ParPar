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
} else {
  module.exports = null; // no writev support
}
