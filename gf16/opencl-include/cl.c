#define CL_API_EXTERN
#define CL_TARGET_OPENCL_VERSION 120
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#include "cl.h"

static void* handle = NULL;
#define LOAD_FN_REQ(n) if(!(LOAD_FN(n))) goto sym_load_failed
#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
# define LOAD_FN(n) *(void**)&n = GetProcAddress(handle, #n)
#elif defined(PARPAR_LIBDL_SUPPORT)
# include <dlfcn.h>
# define LOAD_FN(n) *(void**)&n = dlsym(handle, #n)
#else
# define LOAD_FN(n)
# undef LOAD_FN_REQ
# define LOAD_FN_REQ(n) goto sym_load_failed
#endif
int load_opencl() {
	if(handle) return 0; // already loaded
	
#ifdef _WIN32
	handle = LoadLibrary(TEXT("OpenCL.dll"));
#elif defined(PARPAR_LIBDL_SUPPORT)
	handle = dlopen("libOpenCL.so", RTLD_NOW);
	if(!handle)
		handle = dlopen("libOpenCL.so.1", RTLD_NOW); // another common name seen in a number of Linux/BSD distros (DEB, RPM, APK, Arch etc)
	if(!handle)
		handle = dlopen("libOpenCL.so.1.0.0", RTLD_NOW); // final fallback
#endif
	if(!handle) return 1;
	
	// load functions
    LOAD_FN_REQ(clGetPlatformIDs);
    LOAD_FN_REQ(clGetPlatformInfo);
    LOAD_FN_REQ(clGetDeviceIDs);
    LOAD_FN_REQ(clGetDeviceInfo);
    LOAD_FN(clCreateSubDevices); // 1.2
    LOAD_FN(clRetainDevice); // 1.2
    LOAD_FN(clReleaseDevice); // 1.2
    LOAD_FN_REQ(clCreateContext);
    LOAD_FN_REQ(clCreateContextFromType);
    LOAD_FN_REQ(clRetainContext);
    LOAD_FN_REQ(clReleaseContext);
    LOAD_FN_REQ(clGetContextInfo);
	//LOAD_FN(clCreateCommandQueueWithProperties); // 2.0
    LOAD_FN_REQ(clRetainCommandQueue);
    LOAD_FN_REQ(clReleaseCommandQueue);
    LOAD_FN_REQ(clGetCommandQueueInfo);
    LOAD_FN_REQ(clCreateBuffer);
    LOAD_FN_REQ(clCreateSubBuffer); // 1.1
	LOAD_FN(clCreateImage); // 1.2
	//LOAD_FN(clCreatePipe); // 2.0
    LOAD_FN_REQ(clRetainMemObject);
    LOAD_FN_REQ(clReleaseMemObject);
    LOAD_FN_REQ(clGetSupportedImageFormats);
    LOAD_FN_REQ(clGetMemObjectInfo);
    LOAD_FN_REQ(clGetImageInfo);
	//LOAD_FN(clGetPipeInfo); // 2.0
    LOAD_FN_REQ(clSetMemObjectDestructorCallback); // 1.1
	//LOAD_FN(clSVMAlloc); // 2.0
	//LOAD_FN(clSVMFree); // 2.0
	//LOAD_FN(clCreateSamplerWithProperties); // 2.0
    LOAD_FN_REQ(clRetainSampler);
    LOAD_FN_REQ(clReleaseSampler);
    LOAD_FN_REQ(clGetSamplerInfo);
    LOAD_FN_REQ(clCreateProgramWithSource);
    LOAD_FN_REQ(clCreateProgramWithBinary);
	LOAD_FN(clCreateProgramWithBuiltInKernels); // 1.2
    LOAD_FN_REQ(clRetainProgram);
    LOAD_FN_REQ(clReleaseProgram);
    LOAD_FN_REQ(clBuildProgram);
	LOAD_FN(clCompileProgram); // 1.2
	LOAD_FN(clLinkProgram); // 1.2
	LOAD_FN(clUnloadPlatformCompiler); // 1.2
    LOAD_FN_REQ(clGetProgramInfo);
    LOAD_FN_REQ(clGetProgramBuildInfo);
    LOAD_FN_REQ(clCreateKernel);
    LOAD_FN_REQ(clCreateKernelsInProgram);
    LOAD_FN_REQ(clRetainKernel);
    LOAD_FN_REQ(clReleaseKernel);
    LOAD_FN_REQ(clSetKernelArg);
	//LOAD_FN(clSetKernelArgSVMPointer); // 2.0
	//LOAD_FN(clSetKernelExecInfo); // 2.0
    LOAD_FN_REQ(clGetKernelInfo);
	LOAD_FN(clGetKernelArgInfo); // 1.2
    LOAD_FN_REQ(clGetKernelWorkGroupInfo);
    LOAD_FN_REQ(clWaitForEvents);
    LOAD_FN_REQ(clGetEventInfo);
    LOAD_FN_REQ(clCreateUserEvent); // 1.1
    LOAD_FN_REQ(clRetainEvent);
    LOAD_FN_REQ(clReleaseEvent);
    LOAD_FN_REQ(clSetUserEventStatus); // 1.1
    LOAD_FN_REQ(clSetEventCallback); // 1.1
    LOAD_FN_REQ(clGetEventProfilingInfo);
    LOAD_FN_REQ(clFlush);
    LOAD_FN_REQ(clFinish);
    LOAD_FN_REQ(clEnqueueReadBuffer);
    LOAD_FN_REQ(clEnqueueReadBufferRect); // 1.1
    LOAD_FN_REQ(clEnqueueWriteBuffer);
    LOAD_FN_REQ(clEnqueueWriteBufferRect); // 1.1
	LOAD_FN(clEnqueueFillBuffer); // 1.2
    LOAD_FN_REQ(clEnqueueCopyBuffer);
    LOAD_FN_REQ(clEnqueueCopyBufferRect); // 1.1
    LOAD_FN_REQ(clEnqueueReadImage);
    LOAD_FN_REQ(clEnqueueWriteImage);
	LOAD_FN(clEnqueueFillImage); // 1.2
    LOAD_FN_REQ(clEnqueueCopyImage);
    LOAD_FN_REQ(clEnqueueCopyImageToBuffer);
    LOAD_FN_REQ(clEnqueueCopyBufferToImage);
    LOAD_FN_REQ(clEnqueueMapBuffer);
    LOAD_FN_REQ(clEnqueueMapImage);
    LOAD_FN_REQ(clEnqueueUnmapMemObject);
	LOAD_FN(clEnqueueMigrateMemObjects); // 1.2
    LOAD_FN_REQ(clEnqueueNDRangeKernel);
    LOAD_FN_REQ(clEnqueueNativeKernel);
	LOAD_FN(clEnqueueMarkerWithWaitList); // 1.2
	LOAD_FN(clEnqueueBarrierWithWaitList); // 1.2
	//LOAD_FN(clEnqueueSVMFree); // 2.0
	//LOAD_FN(clEnqueueSVMMemcpy); // 2.0
	//LOAD_FN(clEnqueueSVMMemFill); // 2.0
	//LOAD_FN(clEnqueueSVMMap); // 2.0
	//LOAD_FN(clEnqueueSVMUnmap); // 2.0
	LOAD_FN(clGetExtensionFunctionAddressForPlatform); // 1.2
#ifdef CL_USE_DEPRECATED_OPENCL_1_1_APIS
    LOAD_FN_REQ(clCreateImage2D);
    LOAD_FN_REQ(clCreateImage3D);
    LOAD_FN_REQ(clEnqueueMarker);
    LOAD_FN_REQ(clEnqueueWaitForEvents);
    LOAD_FN_REQ(clEnqueueBarrier);
    LOAD_FN_REQ(clUnloadCompiler);
    LOAD_FN_REQ(clGetExtensionFunctionAddress);
#endif
#ifdef CL_USE_DEPRECATED_OPENCL_2_0_APIS
    LOAD_FN_REQ(clCreateCommandQueue);
    LOAD_FN_REQ(clCreateSampler);
    LOAD_FN_REQ(clEnqueueTask);
#endif
	
	return 0;
	
sym_load_failed:
	unload_opencl();
	return 2;
}

int unload_opencl() {
#ifdef _WIN32
	BOOL ret = FreeLibrary((HMODULE)handle);
	handle = NULL;
	return !ret;
#elif defined(PARPAR_LIBDL_SUPPORT)
	int ret = dlclose(handle);
	handle = NULL;
	return ret;
#else
	return 1;
#endif
}
