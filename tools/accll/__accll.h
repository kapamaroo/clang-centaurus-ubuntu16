#ifndef ___ACCLL_H_
#define ___ACCLL_H_

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/opencl.h>
#endif

typedef int __incomplete__;

extern cl_int error;
extern cl_platform_id platform;
extern cl_context context;
extern cl_command_queue queue;
extern cl_device_id device;

void clCheckError(cl_int error, const char *str);

void __accll_init_accll_runtime();

cl_program __accll_load_and_build(const char *filepath);

int accll_async_test(int event_num, ...);

void __accll_unreachable();

#endif

