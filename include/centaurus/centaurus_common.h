#ifndef _CENTAURUS_COMMON_H
#define _CENTAURUS_COMMON_H

// custom OpenCL types
#include "CL/centaurus_cl_platform.h"
#include <CL/cl.h>

#include "centaurus_acl.h"

enum acl_exec_mode {
  ACL_EXEC_RELEASE = 0,
  ACL_EXEC_PROFILE
};

struct centaurus_config {
  enum acl_exec_mode mode;
  struct {
    // override values
    enum acl_device_type bind_to_dev;
    int significant;  // if <0, ignore it
    double ratio;     // if <0, ignore it
  } profile;
};

/* Geometry struct */
typedef struct _geometry {

  int dimensions;
  size_t *acl_global;
  size_t *acl_local;

} geom;

enum DataDepType {
    D_PASS_BY_VALUE = 0,
    D_BUFFER,
    D_LOCAL_BUFFER,
    D_IN,
    D_OUT,
    D_INOUT,
    D_DEVICE_IN,
    D_DEVICE_OUT,
    D_DEVICE_INOUT,
    D_ESTIMATION
};

/* Memory object containing all the important info */
typedef struct _memory_object {

 /* OpenCL memory types and event */
  cl_mem cl_obj, cl_obj_neigh;
  cl_event memory_event;

  enum DataDepType dependency;

  /* Index used for clSetKernelArg */
  int index;

  /* Size of the memory object */
  size_t size;

  /* If the object is a buffer stored only in a device, i need a unique id */
  unsigned int unique_buffer_id;

  /* Size struct for the 2D and 3D images. 3D image takes 2 more args */
  //size_t size_xyz[5];

  /* Points to the data on the host pointer */
  void *host_ptr;

  /* The offset in bytes in the buffer object to write/read to */
  size_t start_offset;

  /* size of one element */
  size_t element_size;

  /* Flag to know if i have to do memory transfer *
   * and if yes from where ( HOST or DEVICE )     */
  int transfer;

  /* Posi1tion of memory object in memory_table. *
   * Use it to have less searches in table      */
  int mem_tbl_pos;

  unsigned long int start_time;
  unsigned long int stop_time;
  double transfer_time;

} memory_object;

struct _device_bin_static_info {
    size_t arch;

    //Number of registers
    size_t registers;

    //sizes are in bytes

    //total global size
    size_t gmem;

    size_t stack_frame;
    size_t spill_stores;
    size_t spill_loads;
    size_t cmem;
};

/*  per device low level bits  */
struct _device_bin {
    cl_kernel _kernel;
    const unsigned char *bin;
    size_t bin_size;

    struct _device_bin_static_info static_info;
};

/*  per platform low level bits  */
struct _platform_bin {
    /*  as clGetDeviceIDs() returns them for each platform  */
    /*  if a device is NULL, we need to build from source  */
    struct _device_bin *device_table;

    /*  number of devices  */
    size_t device_num;
};

/*  supported platforms  */
enum CentaurusPlatformID {
    PL_UNKNOWN = 0,
    PL_INTEL,
    PL_NVIDIA,
    PL_AMD,
    PL_FPGA
};

#define ACL_SUPPORTED_PLATFORMS_NUM (PL_FPGA + 1)

/* Kernel struct */
typedef
struct _kernel_struct {
    /*
      |_ZERO_PADDING_|_______________8-bit____________|
      |______________| enum acl_device_type| BIND_BIT |
                      \                   /
                       `---device_type---`
    */
    unsigned int device_type;

    /*  ID, source and name are the same for all platforms/devices  */

    size_t UID;

    const char *src;
    size_t src_size;

    const char *name;
    size_t name_size;

    /*  assume multiple platforms/devices  */

    /*  only installed platforms are non-NULL  */
    /*  assume we always have  at least one available platform  */

    //has size ACL_SUPPORTED_PLATFORMS_NUM
    struct _platform_bin *platform_table;

} kernel_t;

typedef struct _task_executable {
  kernel_t *kernel_accurate;
  kernel_t *kernel_approximate;

  kernel_t *kernel_evalfun;

  // as last parameter
  memory_object *estimation;

} task_exe_t;

#ifdef __cplusplus
extern "C" {
#endif

void acl_centaurus_init();
void acl_centaurus_finish();

/* Create a new task and then push it into the appropriate pool.
 * Inputs:
 *         approx           : Task approximation
 *         args             : kernel arguments
 *         num_args         : number of arguments
 *         oclexe           : task code
 *         geometry         : task geometry
 *         group_name       : Task group name  --  (the runtime must strdup() it)
 *         sourcelocation_id: identifier to the original source location  --  (the runtime must strdup() it)
 */
void acl_create_task( int approx, memory_object *args, int num_args, task_exe_t oclexe, geom geometry, const char *group_name, const char *sourcelocation_id );

void acl_taskwait_all();
void acl_taskwait_on(int varnum, ...);
void acl_taskwait_label(const char *label);
void acl_taskwait_label_ratio(const char *label, const double ratio);
void acl_taskwait_label_energy(const char *label, const unsigned int energy);
void acl_create_group_energy(const char *label, const unsigned int energy);
void acl_create_group_ratio(const char *label, const double ratio);

/* memory manager replacement for I/O buffers */
void *acl_malloc(size_t size);
void acl_free(void *ptr);
void *acl_calloc(size_t nmemb, size_t size);
void *acl_realloc(void *ptr, size_t size);
size_t acl_usable_size(void *ptr);

// if label == NULL, total quality
void acl_set_group_quality(const char *label, const double quality);

#ifdef __cplusplus
}
#endif

#endif
