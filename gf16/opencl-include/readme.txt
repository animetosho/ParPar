This is a copy of the include folder from lightOCLSDK [https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases], with the following modifications:

* some unused graphics functions stripped out
* OpenGL reliance removed to ease compilation requirements
* `_mm_mfence` reference not made a hard requirement to enable compilation on non-x86 platforms
* declarations are all changed to function pointers
* load_opencl() and unload_opencl() functions added to dynamically load OpenCL library and resolve functions
* users must include cl.c for compilation, and call load_opencl() before using any OpenCL functionality
