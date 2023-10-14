The files in the CL directory are taken from the following:
* https://github.com/KhronosGroup/OpenCL-Headers/releases/tag/v2023.04.17
* https://github.com/KhronosGroup/OpenCL-CLHPP/releases/tag/v2023.04.17

The OpenCL C++ header has been modified to remove dependency on cl_ext/cl_gl headers, and handle the function pointers introduced in the wrapper below.

The cl.c/cl.h files here are a wrapper to enable runtime linking of OpenCL.
cl.h will include the OpenCL C headers, and must be included before the C++ header.
* users must include cl.c for compilation, and call load_opencl() before using any OpenCL functionality
