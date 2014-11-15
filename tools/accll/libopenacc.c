#include "openacc.h"
#include "__accll.h"
#include "__internal_openacc.h"

#include <stdio.h>
#include <stdlib.h>

acc_device_t acc_device_type_var = acc_device_none;
int acc_device_num_var = 0;

int acc_get_num_devices( acc_device_t dev_type) {
    //http://dhruba.name/2012/10/14/opencl-cookbook-how-to-leverage-multiple-devices-in-opencl/

    cl_device_type cl_dev_type = CL_DEVICE_TYPE_ALL;

    switch (dev_type) {
    case acc_device_none:
        printf("libopenacc: Internal error: bad device type: acc_device_none - exit.\n");
        exit(EXIT_FAILURE);
        break;
    case acc_device_default:
        cl_dev_type = CL_DEVICE_TYPE_DEFAULT;
        break;
    case acc_device_host:
        cl_dev_type = CL_DEVICE_TYPE_CPU;
        break;
    case acc_device_not_host:
        cl_dev_type = CL_DEVICE_TYPE_ALL & ~CL_DEVICE_TYPE_CPU;
        break;

        //Implementation defined, must select platform
    case acc_device_nvidia:
        break;
    case acc_device_intel:
        break;
    case acc_device_amd:
        break;
    default:
        printf("libaccll: Internal error: undefined device type - exit.\n");
        exit(EXIT_FAILURE);
    }

    cl_uint num;
    error = clGetDeviceIDs(platform, cl_dev_type, 0, NULL, &num);
    clCheckError(error,"getting device ids");

    int devnum = num;
    return devnum;
}

void acc_set_device_type ( acc_device_t dev_type) {
    if (dev_type > LAST_ACC_DEVICE_TYPE) {
        printf("libopenacc: Invalid device type - exit.\n");
        exit(EXIT_FAILURE);
    }

    if (dev_type == acc_device_none)
        ;  //maybe do something here
    acc_device_type_var = dev_type;


    int max_num = acc_get_num_devices(dev_type);
    if (acc_device_num_var > max_num) {
        //FIXME: print diagnostic
        //should use acc_set_device_num() instead
        printf("libopenacc: Too big value for device number - implementation defined behavior: set device number to %d\n",max_num);
        acc_device_num_var = max_num;
    }

    __accll_init_opencl_runtime();
}

acc_device_t acc_get_device_type ( void ) {
    return acc_device_type_var;
}

void acc_set_device_num( int dev_num, acc_device_t dev_type) {
    //If the value of dev_num is zero, the runtime will revert to its
    //default behavior, which is implementation-defined.

    //1-based index
    if (dev_num < 0) {
        printf("libopenacc: Negative value for device number - exit.\n");
        exit(EXIT_FAILURE);
    }
    else if (dev_num == 0) {
        printf("libopenacc: Zero value for device number - implementation defined behavior: set device number to 1\n");
        dev_num = 1;
    }
    else {
        int max_num = acc_get_num_devices(dev_type);
        if (dev_num > max_num) {
            printf("libopenacc: Too big value for device number - implementation defined behavior: set device number to %d\n",max_num);
            dev_num = max_num;
        }
    }

    //If the value of the second argument is zero, the selected device
    //number will be used for all attached accelerator types.
    if (dev_type == acc_device_none) {
        dev_type = acc_device_default;
    }

    acc_device_num_var = dev_num;
    acc_set_device_type(dev_type);
}

int acc_get_device_num( acc_device_t dev_type) {
    return acc_device_num_var;
}

int acc_async_test( int async) {
    //this routine is translated to its respective routine from libaccll
    //named accll_async_test()

    //if there is still a call of this routine on the source code, it means
    //that there is no asynchronous activity associated with the given parameter
    //therefore it is safe to return true
    return 1;
}

int acc_async_test_all( ) {
    //this routine is translated to its respective routine from libaccll
    //named accll_async_test()

    //if there is still a call of this routine on the source code, it means
    //that there are no asynchronous activities therefore it is safe to return true
    return 1;
}

void acc_async_wait( int async) {
    //this routine is translated to an OpenACC wait Directive
}

void acc_async_wait_all( ) {
    //this routine is translated to an OpenACC wait Directive
}

void acc_init ( acc_device_t dev_type) {
    acc_shutdown(acc_device_type_var);
    acc_set_device_type(dev_type);
}

void acc_shutdown ( acc_device_t dev_type) {
    if (queue) {
        error = clReleaseCommandQueue(queue);
        clCheckError(error,"shutdown: release queue");
        queue = NULL;
    }
    if (context) {
        clReleaseContext(context);
        clCheckError(error,"shutdown: release context");
        context = NULL;
    }
    if (device) {
        clReleaseDevice(device);
        clCheckError(error,"shutdown: release device");
        device = NULL;
    }
}

int acc_on_device ( acc_device_t dev_type) {
    //FIXME

    if (dev_type == acc_device_type_var)
        return 1;

    return 0;
}

void* acc_malloc ( size_t size) {
    cl_mem *mem = (cl_mem*)malloc(sizeof(cl_mem));
    *mem = clCreateBuffer(context,CL_MEM_READ_WRITE,size,NULL,&error);
    clCheckError(error,"create buffer");
    void *ptr = (void*)mem;
    return ptr;
}

void acc_free ( void* ptr) {
    cl_mem *mem = (cl_mem*)ptr;
    error = clReleaseMemObject(*mem);
    clCheckError(error,"release buffer");
}

//PGI uses this function
void acc_set_device( acc_device_t dev_type ) {
    acc_set_device_type(dev_type);
}
