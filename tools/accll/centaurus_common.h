#ifndef _CENTAURUS_COMMON_H
#define _CENTAURUS_COMMON_H

#include <CL/cl.h>

/* Geometry struct */
typedef struct _geometry {

  int dimensions;
  size_t *global;
  size_t *local;

} geom;

enum DataDepType {
    D_UNDEF = 0,
    D_IN,
    D_OUT
};

/* Memory object containing all the important info */
typedef struct _memory_object {

  cl_mem cl_obj;
  cl_event memory_event;

  int index;
  enum DataDepType dependency;

  void *host_ptr;
  size_t size;
  int pass_by_value;

} memory_object;

/* Kernel struct */
typedef struct _kernel_struct {

  int isCompiled;

  cl_kernel bin;

  const char *src;
  const char *name;
  size_t src_size;
  size_t name_size;

} kernel_t;

typedef struct _task_executable {

  int UID;
  kernel_t *kernel_accurate;
  kernel_t *kernel_approximate;

} task_exe_t;

void acl_centaurus_init();
void acl_centaurus_finish();

/* Create a new task and then push it into the appropriate pool.
 * Inputs:
 *         approx: Task approximation
 *         inputs: actual arguments
 *         num_args: number of arguments
 *         oclexe: task code
 *         geometry: task geometry
 *         group_name: Task group name
 */
void acl_create_task( int approx, memory_object *args, int num_args, task_exe_t oclexe, geom geometry, const char *group_name );

void acl_taskwait_all();
void acl_taskwait_on(int varnum, ...);
void acl_taskwait_label(const char *label);
void acl_taskwait_label_ratio(const char *label, const double ratio);
void acl_taskwait_label_energy(const char *label, const int energy);

#endif
