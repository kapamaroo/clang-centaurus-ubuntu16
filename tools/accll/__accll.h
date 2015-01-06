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

struct _geometry {
    int dimensions;
    int *global;
    int *local;
};

struct _memory_object {
    cl_mem cl_obj;
    size_t size;
    void *host_ptr;
    cl_event memory_event;
} memory_object;

void acc_create_task(int approx,
                     int num_in, struct _memory_object *inputs,
                     int num_out, struct _memory_object *outputs,
                     const char *kernel,
                     const char *kernel_accurate,
                     const char *kernel_approximate,
                     struct _geometry geometry,
                     const char *group_name,
                     size_t source_size);

void acc_wait_all();
void acc_wait_on(int varnum, ...);
void acc_wait_label(const char *label);
void acc_wait_label_ratio(const char *label, const double ratio);
void acc_wait_label_energy(const char *label, const int energy);

#endif
