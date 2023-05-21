#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#include <CL/cl.h>

/* Platform API */
cl_int
(CL_API_CALL *clGetPlatformIDs)(cl_uint          /* num_entries */,
                 cl_platform_id * /* platforms */,
                 cl_uint *        /* num_platforms */) CL_API_SUFFIX__VERSION_1_0;
cl_int 
(CL_API_CALL *clGetPlatformInfo)(cl_platform_id   /* platform */, 
                  cl_platform_info /* param_name */,
                  size_t           /* param_value_size */, 
                  void *           /* param_value */,
                  size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Device APIs */
cl_int
(CL_API_CALL *clGetDeviceIDs)(cl_platform_id   /* platform */,
               cl_device_type   /* device_type */, 
               cl_uint          /* num_entries */, 
               cl_device_id *   /* devices */, 
               cl_uint *        /* num_devices */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetDeviceInfo)(cl_device_id    /* device */,
                cl_device_info  /* param_name */, 
                size_t          /* param_value_size */, 
                void *          /* param_value */,
                size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
    
cl_int
(CL_API_CALL *clCreateSubDevices)(cl_device_id                         /* in_device */,
                   const cl_device_partition_property * /* properties */,
                   cl_uint                              /* num_devices */,
                   cl_device_id *                       /* out_devices */,
                   cl_uint *                            /* num_devices_ret */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clRetainDevice)(cl_device_id /* device */) CL_API_SUFFIX__VERSION_1_2;
    
cl_int
(CL_API_CALL *clReleaseDevice)(cl_device_id /* device */) CL_API_SUFFIX__VERSION_1_2;
    
/* Context APIs  */
cl_context
(CL_API_CALL *clCreateContext)(const cl_context_properties * /* properties */,
                cl_uint                 /* num_devices */,
                const cl_device_id *    /* devices */,
                void (CL_CALLBACK * /* pfn_notify */)(const char *, const void *, size_t, void *),
                void *                  /* user_data */,
                cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_context
(CL_API_CALL *clCreateContextFromType)(const cl_context_properties * /* properties */,
                        cl_device_type          /* device_type */,
                        void (CL_CALLBACK *     /* pfn_notify*/ )(const char *, const void *, size_t, void *),
                        void *                  /* user_data */,
                        cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clRetainContext)(cl_context /* context */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseContext)(cl_context /* context */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetContextInfo)(cl_context         /* context */, 
                 cl_context_info    /* param_name */, 
                 size_t             /* param_value_size */, 
                 void *             /* param_value */, 
                 size_t *           /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Command Queue APIs */
cl_command_queue
(CL_API_CALL *clCreateCommandQueueWithProperties)(cl_context               /* context */,
                                   cl_device_id             /* device */,
                                   const cl_queue_properties *    /* properties */,
                                   cl_int *                 /* errcode_ret */) CL_API_SUFFIX__VERSION_2_0;

cl_int
(CL_API_CALL *clRetainCommandQueue)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseCommandQueue)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetCommandQueueInfo)(cl_command_queue      /* command_queue */,
                      cl_command_queue_info /* param_name */,
                      size_t                /* param_value_size */,
                      void *                /* param_value */,
                      size_t *              /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Memory Object APIs */
cl_mem
(CL_API_CALL *clCreateBuffer)(cl_context   /* context */,
               cl_mem_flags /* flags */,
               size_t       /* size */,
               void *       /* host_ptr */,
               cl_int *     /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_mem
(CL_API_CALL *clCreateSubBuffer)(cl_mem                   /* buffer */,
                  cl_mem_flags             /* flags */,
                  cl_buffer_create_type    /* buffer_create_type */,
                  const void *             /* buffer_create_info */,
                  cl_int *                 /* errcode_ret */) CL_API_SUFFIX__VERSION_1_1;

cl_mem
(CL_API_CALL *clCreateImage)(cl_context              /* context */,
              cl_mem_flags            /* flags */,
              const cl_image_format * /* image_format */,
              const cl_image_desc *   /* image_desc */, 
              void *                  /* host_ptr */,
              cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_2;
                        
cl_mem
(CL_API_CALL *clCreatePipe)(cl_context                 /* context */,
             cl_mem_flags               /* flags */,
             cl_uint                    /* pipe_packet_size */,
             cl_uint                    /* pipe_max_packets */,
             const cl_pipe_properties * /* properties */,
             cl_int *                   /* errcode_ret */) CL_API_SUFFIX__VERSION_2_0;
    
cl_int
(CL_API_CALL *clRetainMemObject)(cl_mem /* memobj */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseMemObject)(cl_mem /* memobj */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetSupportedImageFormats)(cl_context           /* context */,
                           cl_mem_flags         /* flags */,
                           cl_mem_object_type   /* image_type */,
                           cl_uint              /* num_entries */,
                           cl_image_format *    /* image_formats */,
                           cl_uint *            /* num_image_formats */) CL_API_SUFFIX__VERSION_1_0;
                                    
cl_int
(CL_API_CALL *clGetMemObjectInfo)(cl_mem           /* memobj */,
                   cl_mem_info      /* param_name */, 
                   size_t           /* param_value_size */,
                   void *           /* param_value */,
                   size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetImageInfo)(cl_mem           /* image */,
               cl_image_info    /* param_name */, 
               size_t           /* param_value_size */,
               void *           /* param_value */,
               size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
    
cl_int
(CL_API_CALL *clGetPipeInfo)(cl_mem           /* pipe */,
              cl_pipe_info     /* param_name */,
              size_t           /* param_value_size */,
              void *           /* param_value */,
              size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_2_0;
    

cl_int
(CL_API_CALL *clSetMemObjectDestructorCallback)(cl_mem /* memobj */,
                                 void (CL_CALLBACK * /*pfn_notify*/)( cl_mem /* memobj */, void* /*user_data*/),
                                 void * /*user_data */ )             CL_API_SUFFIX__VERSION_1_1;

/* SVM Allocation APIs */
void *
(CL_API_CALL *clSVMAlloc)(cl_context       /* context */,
           cl_svm_mem_flags /* flags */,
           size_t           /* size */,
           cl_uint          /* alignment */) CL_API_SUFFIX__VERSION_2_0;

void
(CL_API_CALL *clSVMFree)(cl_context        /* context */,
          void *            /* svm_pointer */) CL_API_SUFFIX__VERSION_2_0;
    
/* Sampler APIs */
cl_sampler
(CL_API_CALL *clCreateSamplerWithProperties)(cl_context                     /* context */,
                              const cl_sampler_properties *  /* normalized_coords */,
                              cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_2_0;

cl_int
(CL_API_CALL *clRetainSampler)(cl_sampler /* sampler */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseSampler)(cl_sampler /* sampler */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetSamplerInfo)(cl_sampler         /* sampler */,
                 cl_sampler_info    /* param_name */,
                 size_t             /* param_value_size */,
                 void *             /* param_value */,
                 size_t *           /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
                            
/* Program Object APIs  */
cl_program
(CL_API_CALL *clCreateProgramWithSource)(cl_context        /* context */,
                          cl_uint           /* count */,
                          const char **     /* strings */,
                          const size_t *    /* lengths */,
                          cl_int *          /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_program
(CL_API_CALL *clCreateProgramWithBinary)(cl_context                     /* context */,
                          cl_uint                        /* num_devices */,
                          const cl_device_id *           /* device_list */,
                          const size_t *                 /* lengths */,
                          const unsigned char **         /* binaries */,
                          cl_int *                       /* binary_status */,
                          cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_program
(CL_API_CALL *clCreateProgramWithBuiltInKernels)(cl_context            /* context */,
                                  cl_uint               /* num_devices */,
                                  const cl_device_id *  /* device_list */,
                                  const char *          /* kernel_names */,
                                  cl_int *              /* errcode_ret */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clRetainProgram)(cl_program /* program */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseProgram)(cl_program /* program */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clBuildProgram)(cl_program           /* program */,
               cl_uint              /* num_devices */,
               const cl_device_id * /* device_list */,
               const char *         /* options */, 
               void (CL_CALLBACK *  /* pfn_notify */)(cl_program /* program */, void * /* user_data */),
               void *               /* user_data */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clCompileProgram)(cl_program           /* program */,
                 cl_uint              /* num_devices */,
                 const cl_device_id * /* device_list */,
                 const char *         /* options */, 
                 cl_uint              /* num_input_headers */,
                 const cl_program *   /* input_headers */,
                 const char **        /* header_include_names */,
                 void (CL_CALLBACK *  /* pfn_notify */)(cl_program /* program */, void * /* user_data */),
                 void *               /* user_data */) CL_API_SUFFIX__VERSION_1_2;

cl_program
(CL_API_CALL *clLinkProgram)(cl_context           /* context */,
              cl_uint              /* num_devices */,
              const cl_device_id * /* device_list */,
              const char *         /* options */, 
              cl_uint              /* num_input_programs */,
              const cl_program *   /* input_programs */,
              void (CL_CALLBACK *  /* pfn_notify */)(cl_program /* program */, void * /* user_data */),
              void *               /* user_data */,
              cl_int *             /* errcode_ret */ ) CL_API_SUFFIX__VERSION_1_2;


cl_int
(CL_API_CALL *clUnloadPlatformCompiler)(cl_platform_id /* platform */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clGetProgramInfo)(cl_program         /* program */,
                 cl_program_info    /* param_name */,
                 size_t             /* param_value_size */,
                 void *             /* param_value */,
                 size_t *           /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetProgramBuildInfo)(cl_program            /* program */,
                      cl_device_id          /* device */,
                      cl_program_build_info /* param_name */,
                      size_t                /* param_value_size */,
                      void *                /* param_value */,
                      size_t *              /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
                            
/* Kernel Object APIs */
cl_kernel
(CL_API_CALL *clCreateKernel)(cl_program      /* program */,
               const char *    /* kernel_name */,
               cl_int *        /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clCreateKernelsInProgram)(cl_program     /* program */,
                         cl_uint        /* num_kernels */,
                         cl_kernel *    /* kernels */,
                         cl_uint *      /* num_kernels_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clRetainKernel)(cl_kernel    /* kernel */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseKernel)(cl_kernel   /* kernel */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clSetKernelArg)(cl_kernel    /* kernel */,
               cl_uint      /* arg_index */,
               size_t       /* arg_size */,
               const void * /* arg_value */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clSetKernelArgSVMPointer)(cl_kernel    /* kernel */,
                         cl_uint      /* arg_index */,
                         const void * /* arg_value */) CL_API_SUFFIX__VERSION_2_0;

cl_int
(CL_API_CALL *clSetKernelExecInfo)(cl_kernel            /* kernel */,
                    cl_kernel_exec_info  /* param_name */,
                    size_t               /* param_value_size */,
                    const void *         /* param_value */) CL_API_SUFFIX__VERSION_2_0;
    
cl_int
(CL_API_CALL *clGetKernelInfo)(cl_kernel       /* kernel */,
                cl_kernel_info  /* param_name */,
                size_t          /* param_value_size */,
                void *          /* param_value */,
                size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetKernelArgInfo)(cl_kernel       /* kernel */,
                   cl_uint         /* arg_indx */,
                   cl_kernel_arg_info  /* param_name */,
                   size_t          /* param_value_size */,
                   void *          /* param_value */,
                   size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clGetKernelWorkGroupInfo)(cl_kernel                  /* kernel */,
                         cl_device_id               /* device */,
                         cl_kernel_work_group_info  /* param_name */,
                         size_t                     /* param_value_size */,
                         void *                     /* param_value */,
                         size_t *                   /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Event Object APIs */
cl_int
(CL_API_CALL *clWaitForEvents)(cl_uint             /* num_events */,
                const cl_event *    /* event_list */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clGetEventInfo)(cl_event         /* event */,
               cl_event_info    /* param_name */,
               size_t           /* param_value_size */,
               void *           /* param_value */,
               size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
                            
cl_event
(CL_API_CALL *clCreateUserEvent)(cl_context    /* context */,
                  cl_int *      /* errcode_ret */) CL_API_SUFFIX__VERSION_1_1;               
                            
cl_int
(CL_API_CALL *clRetainEvent)(cl_event /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clReleaseEvent)(cl_event /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clSetUserEventStatus)(cl_event   /* event */,
                     cl_int     /* execution_status */) CL_API_SUFFIX__VERSION_1_1;
                     
cl_int
(CL_API_CALL *clSetEventCallback)( cl_event    /* event */,
                    cl_int      /* command_exec_callback_type */,
                    void (CL_CALLBACK * /* pfn_notify */)(cl_event, cl_int, void *),
                    void *      /* user_data */) CL_API_SUFFIX__VERSION_1_1;

/* Profiling APIs */
cl_int
(CL_API_CALL *clGetEventProfilingInfo)(cl_event            /* event */,
                        cl_profiling_info   /* param_name */,
                        size_t              /* param_value_size */,
                        void *              /* param_value */,
                        size_t *            /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;
                                
/* Flush and Finish APIs */
cl_int
(CL_API_CALL *clFlush)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clFinish)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

/* Enqueued Commands APIs */
cl_int
(CL_API_CALL *clEnqueueReadBuffer)(cl_command_queue    /* command_queue */,
                    cl_mem              /* buffer */,
                    cl_bool             /* blocking_read */,
                    size_t              /* offset */,
                    size_t              /* size */, 
                    void *              /* ptr */,
                    cl_uint             /* num_events_in_wait_list */,
                    const cl_event *    /* event_wait_list */,
                    cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_0;
                            
cl_int
(CL_API_CALL *clEnqueueReadBufferRect)(cl_command_queue    /* command_queue */,
                        cl_mem              /* buffer */,
                        cl_bool             /* blocking_read */,
                        const size_t *      /* buffer_offset */,
                        const size_t *      /* host_offset */, 
                        const size_t *      /* region */,
                        size_t              /* buffer_row_pitch */,
                        size_t              /* buffer_slice_pitch */,
                        size_t              /* host_row_pitch */,
                        size_t              /* host_slice_pitch */,                        
                        void *              /* ptr */,
                        cl_uint             /* num_events_in_wait_list */,
                        const cl_event *    /* event_wait_list */,
                        cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_1;
                            
cl_int
(CL_API_CALL *clEnqueueWriteBuffer)(cl_command_queue   /* command_queue */, 
                     cl_mem             /* buffer */, 
                     cl_bool            /* blocking_write */, 
                     size_t             /* offset */, 
                     size_t             /* size */, 
                     const void *       /* ptr */, 
                     cl_uint            /* num_events_in_wait_list */, 
                     const cl_event *   /* event_wait_list */, 
                     cl_event *         /* event */) CL_API_SUFFIX__VERSION_1_0;
                            
cl_int
(CL_API_CALL *clEnqueueWriteBufferRect)(cl_command_queue    /* command_queue */,
                         cl_mem              /* buffer */,
                         cl_bool             /* blocking_write */,
                         const size_t *      /* buffer_offset */,
                         const size_t *      /* host_offset */, 
                         const size_t *      /* region */,
                         size_t              /* buffer_row_pitch */,
                         size_t              /* buffer_slice_pitch */,
                         size_t              /* host_row_pitch */,
                         size_t              /* host_slice_pitch */,                        
                         const void *        /* ptr */,
                         cl_uint             /* num_events_in_wait_list */,
                         const cl_event *    /* event_wait_list */,
                         cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_1;
                            
cl_int
(CL_API_CALL *clEnqueueFillBuffer)(cl_command_queue   /* command_queue */,
                    cl_mem             /* buffer */, 
                    const void *       /* pattern */, 
                    size_t             /* pattern_size */, 
                    size_t             /* offset */, 
                    size_t             /* size */, 
                    cl_uint            /* num_events_in_wait_list */, 
                    const cl_event *   /* event_wait_list */, 
                    cl_event *         /* event */) CL_API_SUFFIX__VERSION_1_2;
                            
cl_int
(CL_API_CALL *clEnqueueCopyBuffer)(cl_command_queue    /* command_queue */, 
                    cl_mem              /* src_buffer */,
                    cl_mem              /* dst_buffer */, 
                    size_t              /* src_offset */,
                    size_t              /* dst_offset */,
                    size_t              /* size */, 
                    cl_uint             /* num_events_in_wait_list */,
                    const cl_event *    /* event_wait_list */,
                    cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_0;
                            
cl_int
(CL_API_CALL *clEnqueueCopyBufferRect)(cl_command_queue    /* command_queue */, 
                        cl_mem              /* src_buffer */,
                        cl_mem              /* dst_buffer */, 
                        const size_t *      /* src_origin */,
                        const size_t *      /* dst_origin */,
                        const size_t *      /* region */, 
                        size_t              /* src_row_pitch */,
                        size_t              /* src_slice_pitch */,
                        size_t              /* dst_row_pitch */,
                        size_t              /* dst_slice_pitch */,
                        cl_uint             /* num_events_in_wait_list */,
                        const cl_event *    /* event_wait_list */,
                        cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_1;
                            
cl_int
(CL_API_CALL *clEnqueueReadImage)(cl_command_queue     /* command_queue */,
                   cl_mem               /* image */,
                   cl_bool              /* blocking_read */, 
                   const size_t *       /* origin[3] */,
                   const size_t *       /* region[3] */,
                   size_t               /* row_pitch */,
                   size_t               /* slice_pitch */, 
                   void *               /* ptr */,
                   cl_uint              /* num_events_in_wait_list */,
                   const cl_event *     /* event_wait_list */,
                   cl_event *           /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueWriteImage)(cl_command_queue    /* command_queue */,
                    cl_mem              /* image */,
                    cl_bool             /* blocking_write */, 
                    const size_t *      /* origin[3] */,
                    const size_t *      /* region[3] */,
                    size_t              /* input_row_pitch */,
                    size_t              /* input_slice_pitch */, 
                    const void *        /* ptr */,
                    cl_uint             /* num_events_in_wait_list */,
                    const cl_event *    /* event_wait_list */,
                    cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueFillImage)(cl_command_queue   /* command_queue */,
                   cl_mem             /* image */, 
                   const void *       /* fill_color */, 
                   const size_t *     /* origin[3] */, 
                   const size_t *     /* region[3] */, 
                   cl_uint            /* num_events_in_wait_list */, 
                   const cl_event *   /* event_wait_list */, 
                   cl_event *         /* event */) CL_API_SUFFIX__VERSION_1_2;
                            
cl_int
(CL_API_CALL *clEnqueueCopyImage)(cl_command_queue     /* command_queue */,
                   cl_mem               /* src_image */,
                   cl_mem               /* dst_image */, 
                   const size_t *       /* src_origin[3] */,
                   const size_t *       /* dst_origin[3] */,
                   const size_t *       /* region[3] */, 
                   cl_uint              /* num_events_in_wait_list */,
                   const cl_event *     /* event_wait_list */,
                   cl_event *           /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueCopyImageToBuffer)(cl_command_queue /* command_queue */,
                           cl_mem           /* src_image */,
                           cl_mem           /* dst_buffer */, 
                           const size_t *   /* src_origin[3] */,
                           const size_t *   /* region[3] */, 
                           size_t           /* dst_offset */,
                           cl_uint          /* num_events_in_wait_list */,
                           const cl_event * /* event_wait_list */,
                           cl_event *       /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueCopyBufferToImage)(cl_command_queue /* command_queue */,
                           cl_mem           /* src_buffer */,
                           cl_mem           /* dst_image */, 
                           size_t           /* src_offset */,
                           const size_t *   /* dst_origin[3] */,
                           const size_t *   /* region[3] */, 
                           cl_uint          /* num_events_in_wait_list */,
                           const cl_event * /* event_wait_list */,
                           cl_event *       /* event */) CL_API_SUFFIX__VERSION_1_0;

void *
(CL_API_CALL *clEnqueueMapBuffer)(cl_command_queue /* command_queue */,
                   cl_mem           /* buffer */,
                   cl_bool          /* blocking_map */, 
                   cl_map_flags     /* map_flags */,
                   size_t           /* offset */,
                   size_t           /* size */,
                   cl_uint          /* num_events_in_wait_list */,
                   const cl_event * /* event_wait_list */,
                   cl_event *       /* event */,
                   cl_int *         /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

void *
(CL_API_CALL *clEnqueueMapImage)(cl_command_queue  /* command_queue */,
                  cl_mem            /* image */, 
                  cl_bool           /* blocking_map */, 
                  cl_map_flags      /* map_flags */, 
                  const size_t *    /* origin[3] */,
                  const size_t *    /* region[3] */,
                  size_t *          /* image_row_pitch */,
                  size_t *          /* image_slice_pitch */,
                  cl_uint           /* num_events_in_wait_list */,
                  const cl_event *  /* event_wait_list */,
                  cl_event *        /* event */,
                  cl_int *          /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueUnmapMemObject)(cl_command_queue /* command_queue */,
                        cl_mem           /* memobj */,
                        void *           /* mapped_ptr */,
                        cl_uint          /* num_events_in_wait_list */,
                        const cl_event *  /* event_wait_list */,
                        cl_event *        /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueMigrateMemObjects)(cl_command_queue       /* command_queue */,
                           cl_uint                /* num_mem_objects */,
                           const cl_mem *         /* mem_objects */,
                           cl_mem_migration_flags /* flags */,
                           cl_uint                /* num_events_in_wait_list */,
                           const cl_event *       /* event_wait_list */,
                           cl_event *             /* event */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clEnqueueNDRangeKernel)(cl_command_queue /* command_queue */,
                       cl_kernel        /* kernel */,
                       cl_uint          /* work_dim */,
                       const size_t *   /* global_work_offset */,
                       const size_t *   /* global_work_size */,
                       const size_t *   /* local_work_size */,
                       cl_uint          /* num_events_in_wait_list */,
                       const cl_event * /* event_wait_list */,
                       cl_event *       /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueNativeKernel)(cl_command_queue  /* command_queue */,
					  void (CL_CALLBACK * /*user_func*/)(void *), 
                      void *            /* args */,
                      size_t            /* cb_args */, 
                      cl_uint           /* num_mem_objects */,
                      const cl_mem *    /* mem_list */,
                      const void **     /* args_mem_loc */,
                      cl_uint           /* num_events_in_wait_list */,
                      const cl_event *  /* event_wait_list */,
                      cl_event *        /* event */) CL_API_SUFFIX__VERSION_1_0;

cl_int
(CL_API_CALL *clEnqueueMarkerWithWaitList)(cl_command_queue  /* command_queue */,
                            cl_uint           /* num_events_in_wait_list */,
                            const cl_event *  /* event_wait_list */,
                            cl_event *        /* event */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clEnqueueBarrierWithWaitList)(cl_command_queue  /* command_queue */,
                             cl_uint           /* num_events_in_wait_list */,
                             const cl_event *  /* event_wait_list */,
                             cl_event *        /* event */) CL_API_SUFFIX__VERSION_1_2;

cl_int
(CL_API_CALL *clEnqueueSVMFree)(cl_command_queue  /* command_queue */,
                 cl_uint           /* num_svm_pointers */,
                 void *[]          /* svm_pointers[] */,
                 void (CL_CALLBACK * /*pfn_free_func*/)(cl_command_queue /* queue */,
                                                        cl_uint          /* num_svm_pointers */,
                                                        void *[]         /* svm_pointers[] */,
                                                        void *           /* user_data */),
                 void *            /* user_data */,
                 cl_uint           /* num_events_in_wait_list */,
                 const cl_event *  /* event_wait_list */,
                 cl_event *        /* event */) CL_API_SUFFIX__VERSION_2_0;

cl_int
(CL_API_CALL *clEnqueueSVMMemcpy)(cl_command_queue  /* command_queue */,
                   cl_bool           /* blocking_copy */,
                   void *            /* dst_ptr */,
                   const void *      /* src_ptr */,
                   size_t            /* size */,
                   cl_uint           /* num_events_in_wait_list */,
                   const cl_event *  /* event_wait_list */,
                   cl_event *        /* event */) CL_API_SUFFIX__VERSION_2_0;

cl_int
(CL_API_CALL *clEnqueueSVMMemFill)(cl_command_queue  /* command_queue */,
                    void *            /* svm_ptr */,
                    const void *      /* pattern */,
                    size_t            /* pattern_size */,
                    size_t            /* size */,
                    cl_uint           /* num_events_in_wait_list */,
                    const cl_event *  /* event_wait_list */,
                    cl_event *        /* event */) CL_API_SUFFIX__VERSION_2_0;
    
cl_int
(CL_API_CALL *clEnqueueSVMMap)(cl_command_queue  /* command_queue */,
                cl_bool           /* blocking_map */,
                cl_map_flags      /* flags */,
                void *            /* svm_ptr */,
                size_t            /* size */,
                cl_uint           /* num_events_in_wait_list */,
                const cl_event *  /* event_wait_list */,
                cl_event *        /* event */) CL_API_SUFFIX__VERSION_2_0;
    
cl_int
(CL_API_CALL *clEnqueueSVMUnmap)(cl_command_queue  /* command_queue */,
                  void *            /* svm_ptr */,
                  cl_uint           /* num_events_in_wait_list */,
                  const cl_event *  /* event_wait_list */,
                  cl_event *        /* event */) CL_API_SUFFIX__VERSION_2_0;
    

/* Extension function access
 *
 * Returns the extension function address for the given function name,
 * or NULL if a valid function can not be found.  The client must
 * check to make sure the address is not NULL, before using or 
 * calling the returned function address.
 */
void * 
(CL_API_CALL *clGetExtensionFunctionAddressForPlatform)(cl_platform_id /* platform */,
                                         const char *   /* func_name */) CL_API_SUFFIX__VERSION_1_2;
    

/* Deprecated OpenCL 1.1 APIs */
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_mem
(CL_API_CALL *clCreateImage2D)(cl_context              /* context */,
                cl_mem_flags            /* flags */,
                const cl_image_format * /* image_format */,
                size_t                  /* image_width */,
                size_t                  /* image_height */,
                size_t                  /* image_row_pitch */, 
                void *                  /* host_ptr */,
                cl_int *                /* errcode_ret */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_mem
(CL_API_CALL *clCreateImage3D)(cl_context              /* context */,
                cl_mem_flags            /* flags */,
                const cl_image_format * /* image_format */,
                size_t                  /* image_width */, 
                size_t                  /* image_height */,
                size_t                  /* image_depth */, 
                size_t                  /* image_row_pitch */, 
                size_t                  /* image_slice_pitch */, 
                void *                  /* host_ptr */,
                cl_int *                /* errcode_ret */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_int
(CL_API_CALL *clEnqueueMarker)(cl_command_queue    /* command_queue */,
                cl_event *          /* event */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_int
(CL_API_CALL *clEnqueueWaitForEvents)(cl_command_queue /* command_queue */,
                        cl_uint          /* num_events */,
                        const cl_event * /* event_list */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_int
(CL_API_CALL *clEnqueueBarrier)(cl_command_queue /* command_queue */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;

CL_EXT_PREFIX__VERSION_1_1_DEPRECATED cl_int
(CL_API_CALL *clUnloadCompiler)(void) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_1_1_DEPRECATED void *
(CL_API_CALL *clGetExtensionFunctionAddress)(const char * /* func_name */) CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED;
    
/* Deprecated OpenCL 2.0 APIs */
CL_EXT_PREFIX__VERSION_2_0_DEPRECATED cl_command_queue
(CL_API_CALL *clCreateCommandQueue)(cl_context                     /* context */,
                     cl_device_id                   /* device */,
                     cl_command_queue_properties    /* properties */,
                     cl_int *                       /* errcode_ret */) CL_EXT_SUFFIX__VERSION_2_0_DEPRECATED;
    
    
CL_EXT_PREFIX__VERSION_2_0_DEPRECATED cl_sampler
(CL_API_CALL *clCreateSampler)(cl_context          /* context */,
                cl_bool             /* normalized_coords */,
                cl_addressing_mode  /* addressing_mode */,
                cl_filter_mode      /* filter_mode */,
                cl_int *            /* errcode_ret */) CL_EXT_SUFFIX__VERSION_2_0_DEPRECATED;
    
CL_EXT_PREFIX__VERSION_2_0_DEPRECATED cl_int
(CL_API_CALL *clEnqueueTask)(cl_command_queue  /* command_queue */,
              cl_kernel         /* kernel */,
              cl_uint           /* num_events_in_wait_list */,
              const cl_event *  /* event_wait_list */,
              cl_event *        /* event */) CL_EXT_SUFFIX__VERSION_2_0_DEPRECATED;
    

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
//#ifdef CL_USE_DEPRECATED_OPENCL_1_1_APIS
    LOAD_FN_REQ(clCreateImage2D);
    LOAD_FN_REQ(clCreateImage3D);
    LOAD_FN_REQ(clEnqueueMarker);
    LOAD_FN_REQ(clEnqueueWaitForEvents);
    LOAD_FN_REQ(clEnqueueBarrier);
    LOAD_FN_REQ(clUnloadCompiler);
    LOAD_FN_REQ(clGetExtensionFunctionAddress);
//#endif
//#ifdef CL_USE_DEPRECATED_OPENCL_2_0_APIS
    LOAD_FN_REQ(clCreateCommandQueue);
    LOAD_FN_REQ(clCreateSampler);
    LOAD_FN_REQ(clEnqueueTask);
//#endif
	
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
