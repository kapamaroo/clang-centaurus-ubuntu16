#ifndef __ACL_API_TYPES__
#define __ACL_API_TYPES__

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/opencl.h>
#endif

#define CLK_LOCAL_MEM_FENCE 1
#define DCLK_GLOBAL_MEM_FENCE 2

#if 0
#define OPENCL_API_TYPE(typename, size) cl_ ## typename ## size
#define TYPEDEF_BUILTIN_VECTOR(typename, size) typedef OPENCL_API_TYPE(typename,size) typename ## size
#define TYPEDEF_BUILTIN_VECTORS_OF(typename) \
    TYPEDEF_BUILTIN_VECTOR(typename,2);      \
    TYPEDEF_BUILTIN_VECTOR(typename,4);      \
    TYPEDEF_BUILTIN_VECTOR(typename,8);      \
    TYPEDEF_BUILTIN_VECTOR(typename,16);

TYPEDEF_BUILTIN_VECTORS_OF(char)
TYPEDEF_BUILTIN_VECTORS_OF(uchar)
TYPEDEF_BUILTIN_VECTORS_OF(short)
TYPEDEF_BUILTIN_VECTORS_OF(ushort)
TYPEDEF_BUILTIN_VECTORS_OF(int)
TYPEDEF_BUILTIN_VECTORS_OF(uint)
TYPEDEF_BUILTIN_VECTORS_OF(long)
TYPEDEF_BUILTIN_VECTORS_OF(ulong)
TYPEDEF_BUILTIN_VECTORS_OF(float)

typedef int topgrapsametoxeri;

//extension
TYPEDEF_BUILTIN_VECTORS_OF(double)

#undef OPENCL_API_TYPE
#undef TYPEDEF_BUILTIN_VECTOR
#undef TYPEDEF_BUILTIN_VECTORS_OF

#else

typedef cl_char2 char2;
typedef cl_char4 char4;
typedef cl_char8 char8;
typedef cl_char16 char16;
typedef cl_uchar2 uchar2;
typedef cl_uchar4 uchar4;
typedef cl_uchar8 uchar8;
typedef cl_uchar16 uchar16;
typedef cl_short2 short2;
typedef cl_short4 short4;
typedef cl_short8 short8;
typedef cl_short16 short16;
typedef cl_ushort2 ushort2;
typedef cl_ushort4 ushort4;
typedef cl_ushort8 ushort8;
typedef cl_ushort16 ushort16;
typedef cl_int2 int2;
typedef cl_int4 int4;
typedef cl_int8 int8;
typedef cl_int16 int16;
typedef cl_uint2 uint2;
typedef cl_uint4 uint4;
typedef cl_uint8 uint8;
typedef cl_uint16 uint16;
typedef cl_long2 long2;
typedef cl_long4 long4;
typedef cl_long8 long8;
typedef cl_long16 long16;
typedef cl_ulong2 ulong2;
typedef cl_ulong4 ulong4;
typedef cl_ulong8 ulong8;
typedef cl_ulong16 ulong16;
typedef cl_float2 float2;
typedef cl_float4 float4;
typedef cl_float8 float8;
typedef cl_float16 float16;
typedef cl_double2 double2;
typedef cl_double4 double4;
typedef cl_double8 double8;
typedef cl_double16 double16;

#endif

// implement built in functions
size_t get_global_id ( uint dimindx);
size_t get_global_size ( uint dimindx);
size_t get_group_id ( uint dimindx);
size_t get_local_id ( uint dimindx);
size_t get_local_size ( uint dimindx);
size_t get_num_groups ( uint dimindx);
uint get_work_dim ();

size_t get_global_id ( uint dimindx) { (void)dimindx; return 0; }
size_t get_global_size ( uint dimindx) { (void)dimindx; return 0; }
size_t get_group_id ( uint dimindx) { (void)dimindx; return 0; }
size_t get_local_id ( uint dimindx) { (void)dimindx; return 0; }
size_t get_local_size ( uint dimindx) { (void)dimindx; return 0; }
size_t get_num_groups ( uint dimindx) { (void)dimindx; return 0; }
uint get_work_dim () { return 0; }

typedef uint cl_mem_fence_flags;

void prefetch ( const __global void *p, size_t num_elements);
void mem_fence ( cl_mem_fence_flags flags);
void read_mem_fence ( cl_mem_fence_flags flags);
void write_mem_fence ( cl_mem_fence_flags flags);
void barrier ( cl_mem_fence_flags flags);

void prefetch ( const __global void *p, size_t num_elements) { (void)p; (void)num_elements; }
void mem_fence ( cl_mem_fence_flags flags) { (void)flags; }
void read_mem_fence ( cl_mem_fence_flags flags) { (void)flags; }
void write_mem_fence ( cl_mem_fence_flags flags) { (void)flags; }
void barrier ( cl_mem_fence_flags flags) { (void)flags; }

#endif
