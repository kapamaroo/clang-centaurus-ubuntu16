#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <string.h>

#include "__accll.h"
#include "openacc.h"
#include "__internal_openacc.h"

cl_int error = 0;
cl_platform_id platform = NULL;
cl_context context = NULL;
cl_command_queue queue = NULL;
cl_device_id device = NULL;

//https://github.com/enjalot/adventures_in_opencl/blob/master/part1/util.cpp
const char* clErrorString(cl_int error)
{
    static const char* errorString[] = {
        "CL_SUCCESS",
        "CL_DEVICE_NOT_FOUND",
        "CL_DEVICE_NOT_AVAILABLE",
        "CL_COMPILER_NOT_AVAILABLE",
        "CL_MEM_OBJECT_ALLOCATION_FAILURE",
        "CL_OUT_OF_RESOURCES",
        "CL_OUT_OF_HOST_MEMORY",
        "CL_PROFILING_INFO_NOT_AVAILABLE",
        "CL_MEM_COPY_OVERLAP",
        "CL_IMAGE_FORMAT_MISMATCH",
        "CL_IMAGE_FORMAT_NOT_SUPPORTED",
        "CL_BUILD_PROGRAM_FAILURE",
        "CL_MAP_FAILURE",
        "CL_MISALIGNED_SUB_BUFFER_OFFSET",
        "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST",
        "CL_COMPILE_PROGRAM_FAILURE",
        "CL_LINKER_NOT_AVAILABLE",
        "CL_LINK_PROGRAM_FAILURE",
        "CL_DEVICE_PARTITION_FAILED",
        "CL_KERNEL_ARG_INFO_NOT_AVAILABLE",
        "",  //20
        "",  //21
        "",  //22
        "",  //23
        "",  //24
        "",  //25
        "",  //26
        "",  //27
        "",  //28
        "",  //29
        "CL_INVALID_VALUE",
        "CL_INVALID_DEVICE_TYPE",
        "CL_INVALID_PLATFORM",
        "CL_INVALID_DEVICE",
        "CL_INVALID_CONTEXT",
        "CL_INVALID_QUEUE_PROPERTIES",
        "CL_INVALID_COMMAND_QUEUE",
        "CL_INVALID_HOST_PTR",
        "CL_INVALID_MEM_OBJECT",
        "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR",
        "CL_INVALID_IMAGE_SIZE",
        "CL_INVALID_SAMPLER",
        "CL_INVALID_BINARY",
        "CL_INVALID_BUILD_OPTIONS",
        "CL_INVALID_PROGRAM",
        "CL_INVALID_PROGRAM_EXECUTABLE",
        "CL_INVALID_KERNEL_NAME",
        "CL_INVALID_KERNEL_DEFINITION",
        "CL_INVALID_KERNEL",
        "CL_INVALID_ARG_INDEX",
        "CL_INVALID_ARG_VALUE",
        "CL_INVALID_ARG_SIZE",
        "CL_INVALID_KERNEL_ARGS",
        "CL_INVALID_WORK_DIMENSION",
        "CL_INVALID_WORK_GROUP_SIZE",
        "CL_INVALID_WORK_ITEM_SIZE",
        "CL_INVALID_GLOBAL_OFFSET",
        "CL_INVALID_EVENT_WAIT_LIST",
        "CL_INVALID_EVENT",
        "CL_INVALID_OPERATION",
        "CL_INVALID_GL_OBJECT",
        "CL_INVALID_BUFFER_SIZE",
        "CL_INVALID_MIP_LEVEL",
        "CL_INVALID_GLOBAL_WORK_SIZE",
    };

    const int errorCount = sizeof(errorString) / sizeof(errorString[0]);

    const int index = -error;

    return (index >= 0 && index < errorCount) ? errorString[index] : "";

}

void clCheckError(cl_int error, const char *str) {
    if (error != CL_SUCCESS) {
        printf("Error: %s  -  %s\n", str, clErrorString(error));
        exit(error);
    }
}

char *__accll_get_source_from_filepath(const char *filepath) {
    struct stat fileinfo;
    int error = stat(filepath,&fileinfo);
    if (error == -1) {
        perror(__FUNCTION__);
        exit(EXIT_FAILURE);
    }

    int filesize = fileinfo.st_size;

    int fd = open(filepath,O_RDONLY);
    if (fd == -1) {
        perror(__FUNCTION__);
        exit(EXIT_FAILURE);
    }

    //minimize I/O delay
    char *source =
        mmap(NULL,filesize,PROT_READ,MAP_POPULATE | MAP_PRIVATE,fd,0);
    close(fd);

    return source;
}

void __accll_init_accll_runtime() {
    //http://dhruba.name/2012/08/13/opencl-cookbook-listing-all-platforms-and-their-attributes/

    //const char *_INTEL = "Intel(R) Corporation";

    const char *attributeNames[5] = { "Name",
                                      "Vendor",
                                      "Version",
                                      "Profile",
                                      "Extensions" };

    const cl_platform_info attributeTypes[5] = { CL_PLATFORM_NAME,
                                                 CL_PLATFORM_VENDOR,
                                                 CL_PLATFORM_VERSION,
                                                 CL_PLATFORM_PROFILE,
                                                 CL_PLATFORM_EXTENSIONS };
    const int attributeCount = sizeof(attributeNames) / sizeof(char*);

    // get platform count
    cl_uint platformCount;
    clGetPlatformIDs(5, NULL, &platformCount);
    clCheckError(error,"getting platform num");

    // get all platforms
    cl_platform_id platforms[platformCount];
    clGetPlatformIDs(platformCount, platforms, NULL);
    clCheckError(error,"getting platform ids");

    //see https://github.com/nbigaouette/oclutils/blob/master/src/OclUtils.cpp

    // for each platform print all attributes
    int i, j;
    for (i = 0; i < platformCount; i++) {
        printf("\n %d. Platform \n", i+1);
        for (j = 0; j < attributeCount; j++) {
            // get platform attribute value size
            size_t infoSize;
            clGetPlatformInfo(platforms[i], attributeTypes[j], 0, NULL, &infoSize);
            clCheckError(error,"getting platform size info");
            char* info;
            info = (char*) malloc(infoSize);
            // get platform attribute value
            clGetPlatformInfo(platforms[i], attributeTypes[j], infoSize, info, NULL);
            clCheckError(error,"getting platform info");
            printf("  %d.%d %-11s: %s\n", i+1, j+1, attributeNames[j], info);
            free(info);
        }
        printf("\n");
    }

    //choose the first platform
    //platform = memcpy(platform,platforms[0],sizeof(cl_platform_id));
    platform = platforms[0];

    acc_device_type_var = acc_device_none;
    //initialize to first device of any type (1-based index)
    acc_device_num_var = 1;

    __accll_init_opencl_runtime();
}

void __accll_init_opencl_runtime() {
    cl_device_type cl_dev_type = CL_DEVICE_TYPE_DEFAULT;
    switch (acc_device_type_var) {
    case acc_device_none:
        //printf("libaccll: Internal error: uninitialized device type - use cpu.\n");
        cl_dev_type = CL_DEVICE_TYPE_CPU;
        //exit(EXIT_FAILURE);
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

    // Device
    cl_device_id device_pool[acc_device_num_var];
    error = clGetDeviceIDs(platform, cl_dev_type, acc_device_num_var, device_pool, NULL);
    clCheckError(error,"populating device pool");

    //1-based index
    device = device_pool[acc_device_num_var - 1];

    // Context
    context = clCreateContext(0, 1, &device, NULL, NULL, &error);
    clCheckError(error,"ceating context");

    // Command-queue
    queue = clCreateCommandQueue(context, device,
                                 CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &error);
    clCheckError(error,"creating command queue");
}

cl_program __accll_load_and_build(const char *filepath) {
    const char *source = __accll_get_source_from_filepath(filepath);

    cl_program program =
        clCreateProgramWithSource(context,1,&source,/*filelength=*/NULL,&error);
    clCheckError(error,"creating program");

    //error = clBuildProgram(program,1,&device,NULL,NULL,NULL);
    const char *flags = "-g -s ";
    char *options = (char*)malloc(sizeof(char)*(strlen(filepath) + strlen(flags)) + 3);
    int size = sprintf(options,"%s\"%s\"",flags,filepath);
    options[size] = '\0';
    error = clBuildProgram(program,1,&device,options,NULL,NULL);
    clCheckError(error,"building program");

    return program;
}

int accll_async_test(int event_num, ...) {
    int finished = 0;

    va_list ap;
    va_start(ap,event_num);

    int i;
    for (i=0; i<event_num; ++i) {
        cl_int event_status;
        cl_event event = va_arg(ap,cl_event);
        error = clGetEventInfo(event,CL_EVENT_COMMAND_EXECUTION_STATUS,
                               sizeof(cl_int),&event_status,NULL);
        clCheckError(error,"check event status");
        if (event_status == CL_COMPLETE)
            finished++;
    }

    va_end(ap);

    return finished == event_num;
}

void __accll_unreachable() {
    __builtin_unreachable();
}
