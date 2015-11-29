// use this file only for the transformation Stages

// on conflicts we prefer the system's headers

#ifndef __ACL_API_TYPES__
#define __ACL_API_TYPES__

#ifndef __CENTAURUS__
#error NOT IN CENTAURUS STAGE
#endif

#define __CENTAURUS_OVERLOAD__ __attribute__((overloadable))

#ifdef __CENTAURUS__
typedef int event_t;
typedef int half;
typedef int * image1d_array_t;
typedef int * image2d_array_t;
typedef int * image3d_array_t;
typedef int * image1d_buffer_t;
#endif

#ifdef __CENTAURUS__
typedef int * image1d_t;
typedef int * image2d_t;
typedef char * image3d_t;  //different type to let overloading work
typedef int * sampler_t;
#endif

// definitions of OpenCL builtin functions
#define cl_khr_fp64
#define cl_clang_storage_class_specifiers

#ifdef __cplusplus
extern "C" {
#endif

#include "clc/clc.h"

#ifdef __cplusplus
}
#endif

//#ifdef select
//#undef select
//#endif

// custom system header file
#ifdef __cplusplus
#else
#include "stdlib.h"
#include "math.h"
#endif

//#define __SPIR32__
//#include "khronos-spir/opencl_spir.h"
//#undef __SPIR32__

#undef __CENTAURUS_OVERLOAD__

// custom OpenCL types
#include <CL/centaurus_cl_platform.h>

#include "__acl_sys_impl.h"

#endif
