#ifndef ___ACCLL_H_
#define ___ACCLL_H_

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/opencl.h>
#endif

cl_program __accll_load_and_build(const char *filepath);

#endif
