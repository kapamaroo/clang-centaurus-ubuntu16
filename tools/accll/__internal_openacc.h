#ifndef ___INTERNAL_OPENACC_H_
#define ___INTERNAL_OPENACC_H_

enum acc_device_t;

extern enum acc_device_t acc_device_type_var;
extern int acc_device_num_var;

void __accll_init_opencl_runtime();

#endif
