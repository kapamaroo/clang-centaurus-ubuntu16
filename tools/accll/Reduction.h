#ifndef __REDUCTION_H_
#define __REDUCTION_H_

//original code taken from
//http://developer.amd.com/resources/documentation-articles/articles-whitepapers/opencl-optimization-case-study-simple-reductions/

#if 0
//OpenCL 1.2, [6.12.2.1, Floating-point macros and pragmas]
#define FLT_MAX 0x1.fffffep127f
#define FLT_MIN 0x1.0p-126f
#define CL_FLT_MAX FLT_MAX
#define CL_FLT_MIN FLT_MIN

#define DBL_MAX 0x1.fffffffffffffp1023
#define DBL_MIN 0x1.0p-1022
#define CL_DBL_MAX DBL_MAX
#define CL_DBL_MIN DBL_MIN

#define CL_SCHAR_MAX        127
#define CL_SCHAR_MIN        (-127-1)
#define CL_CHAR_MAX         CL_SCHAR_MAX
#define CL_CHAR_MIN         CL_SCHAR_MIN
#define CL_UCHAR_MAX        255
#define CL_SHRT_MAX         32767
#define CL_SHRT_MIN         (-32767-1)
#define CL_USHRT_MAX        65535
#define CL_INT_MAX          2147483647
#define CL_INT_MIN          (-2147483647-1)
#define CL_UINT_MAX         0xffffffffU
#define CL_LONG_MAX         ((cl_long) 0x7FFFFFFFFFFFFFFFLL)
#define CL_LONG_MIN         ((cl_long) -0x7FFFFFFFFFFFFFFFLL - 1LL)
#define CL_ULONG_MAX        ((cl_ulong) 0xFFFFFFFFFFFFFFFFULL)
#endif

#define REDUCTION_UPDATE_STMT_PLUS {accumulator = (arg1) + (arg2);}
#define REDUCTION_UPDATE_STMT_MULT {accumulator = (arg1) * (arg2);}

#define REDUCTION_UPDATE_STMT_MIN_INT {accumulator = min((arg1),(arg2));}
#define REDUCTION_UPDATE_STMT_MIN_FLOAT {accumulator = fmin((arg1),(arg2));}
#define REDUCTION_UPDATE_STMT_MIN_DOUBLE {accumulator = fmin((arg1),(arg2));}
//#define REDUCTION_UPDATE_STMT_MIN_COMPLEX {accumulator = fmin((arg1),(arg2));}

#define REDUCTION_UPDATE_STMT_MAX_INT {accumulator = max((arg1),(arg2));}
#define REDUCTION_UPDATE_STMT_MAX_FLOAT {accumulator = fmax((arg1),(arg2));}
#define REDUCTION_UPDATE_STMT_MAX_DOUBLE {accumulator = fmax((arg1),(arg2));}
//#define REDUCTION_UPDATE_STMT_MAX_COMPLEX {accumulator = fmax((arg1),(arg2));}

#define REDUCTION_UPDATE_STMT_BITWISE_AND {accumulator = (arg1) & (arg2);}
#define REDUCTION_UPDATE_STMT_BITWISE_OR {accumulator = (arg1) | (arg2);}
#define REDUCTION_UPDATE_STMT_BITWISE_XOR {accumulator = (arg1) ^ (arg2);}
#define REDUCTION_UPDATE_STMT_LOGICAL_AND {accumulator = (arg1) && (arg2);}
#define REDUCTION_UPDATE_STMT_LOGICAL_OR {accumulator = (arg1) || (arg2);}

#define REDUCTION_INIT_VAL_PLUS 0
#define REDUCTION_INIT_VAL_MULT 1

#define REDUCTION_INIT_VAL_MIN_INT    CL_INT_MIN
#define REDUCTION_INIT_VAL_MIN_FLOAT  CL_FLT_MIN
#define REDUCTION_INIT_VAL_MIN_DOUBLE CL_DBL_MIN
//#define REDUCTION_INIT_VAL_MIN_COMPLEX

#define REDUCTION_INIT_VAL_MAX_INT    CL_INT_MAX
#define REDUCTION_INIT_VAL_MAX_FLOAT  CL_FLT_MAX
#define REDUCTION_INIT_VAL_MAX_DOUBLE CL_DBL_MAX
//#define REDUCTION_INIT_VAL_MAX_COMPLEX

#define REDUCTION_INIT_VAL_BITWISE_AND ~0
#define REDUCTION_INIT_VAL_BITWISE_OR   0
#define REDUCTION_INIT_VAL_BITWISE_XOR  0
#define REDUCTION_INIT_VAL_LOGICAL_AND  1
#define REDUCTION_INIT_VAL_LOGICAL_OR   0

#define EXPAND(S) S
#define DEFINE(S) EXPAND(S)
#define EXPAND_TO_STR(S) #S
#define STR(S) EXPAND_TO_STR(S)

#define CALL_REDUCTION(mode,type,buffer,scratch,length,result) \
    "reduce_" + mode + "_" + EXPAND(type)                      \
    + "(" + buffer + "," + scratch + "," + length + "," + result + ")"

#define REDUCTION_BASE(mode,type,init_value,update_stmt)                \
    void reduce_##mode##_##type(__global type* buffer,                  \
                                __local type* scratch,                  \
                                __const int length,                     \
                                __global type* result) {                \
                                                                        \
      /* reduce buffer elements from global_size to local_size */       \
                                                                        \
      int global_index = get_global_id(0);                              \
      type accumulator = init_value;                                    \
      /* Loop sequentially over chunks of input vector */               \
      while (global_index < length)                                     \
          {                                                             \
              type arg1 = buffer[global_index];                         \
              type arg2 = accumulator;                                  \
              update_stmt;                                              \
              global_index += get_global_size(0);                       \
          }                                                             \
                                                                        \
      /* Perform parallel reduction */                                  \
      const int local_index = get_local_id(0);                          \
      scratch[local_index] = accumulator;                               \
      barrier(CLK_LOCAL_MEM_FENCE);                                     \
      for(int offset = get_local_size(0) / 2;                           \
          offset > 0;                                                   \
          offset = offset / 2)                                          \
          {                                                             \
              if (local_index < offset) {                               \
                  type arg1 = scratch[local_index + offset];            \
                  type arg2 = scratch[local_index];                     \
                  update_stmt;                                          \
                  scratch[local_index] = accumulator;                   \
              }                                                         \
              barrier(CLK_LOCAL_MEM_FENCE);                             \
          }                                                             \
                                                                        \
      if (local_index == 0)                                             \
          {                                                             \
              /* reuse buffer for second phase */                       \
              buffer[get_group_id(0)] = scratch[0];                     \
          }                                                             \
                                                                        \
      /* ////////////////////////////////////////////////// */          \
      barrier(CLK_GLOBAL_MEM_FENCE);                                    \
                                                                        \
      /* reduce buffer to result */                                     \
                                                                        \
      /* let all the group do the calculation */                        \
      int index = get_local_id(0);                                       \
      const int group_num = get_num_groups(0);                          \
      accumulator = init_value;                                         \
      while (index < group_num)                                         \
          {                                                             \
              type arg1 = buffer[index];                                \
              type arg2 = accumulator;                                  \
              update_stmt;                                              \
              index += get_local_size(0);                               \
          }                                                             \
      scratch[local_index] = accumulator;                               \
      barrier(CLK_LOCAL_MEM_FENCE);                                     \
      for(int offset = get_local_size(0) / 2;                           \
          offset > 0;                                                   \
          offset = offset / 2)                                          \
          {                                                             \
              if (local_index < offset) {                               \
                  type arg1 = scratch[local_index + offset];            \
                  type arg2 = scratch[local_index];                     \
                  update_stmt;                                          \
                  scratch[local_index] = accumulator;                   \
              }                                                         \
              barrier(CLK_LOCAL_MEM_FENCE);                             \
          }                                                             \
                                                                        \
      /* only one thread sets the result */                             \
      if (get_global_id(0) == 0)                                        \
          {                                                             \
              (*result) = scratch[0];                                   \
          }                                                             \
  }

#define REDUCTION_PLUS(type) REDUCTION_BASE(plus,type,REDUCTION_INIT_VAL_PLUS,REDUCTION_UPDATE_STMT_PLUS)
#define REDUCTION_MULT(type) REDUCTION_BASE(mult,type,REDUCTION_INIT_VAL_MULT,REDUCTION_UPDATE_STMT_MULT)

#define REDUCTION_MAX_INT REDUCTION_BASE(max,int,REDUCTION_INIT_VAL_MAX_INT,REDUCTION_UPDATE_STMT_MAX_INT)
#define REDUCTION_MAX_FLOAT REDUCTION_BASE(max,float,REDUCTION_INIT_VAL_MAX_FLOAT,REDUCTION_UPDATE_STMT_MAX_FLOAT)
#define REDUCTION_MAX_DOUBLE REDUCTION_BASE(max,double,REDUCTION_INIT_VAL_MAX_DOUBLE,REDUCTION_UPDATE_STMT_MAX_DOUBLE)

#define REDUCTION_MIN_INT REDUCTION_BASE(min,int,REDUCTION_INIT_VAL_MIN_INT,REDUCTION_UPDATE_STMT_MIN_INT)
#define REDUCTION_MIN_FLOAT REDUCTION_BASE(min,float,REDUCTION_INIT_VAL_MIN_FLOAT,REDUCTION_UPDATE_STMT_MIN_FLOAT)
#define REDUCTION_MIN_DOUBLE REDUCTION_BASE(min,double,REDUCTION_INIT_VAL_MIN_DOUBLE,REDUCTION_UPDATE_STMT_MIN_DOUBLE)

#define REDUCTION_BITWISE_AND(type) REDUCTION_BASE(bitwise_and,type,REDUCTION_INIT_VAL_BITWISE_AND,REDUCTION_UPDATE_STMT_BITWISE_AND)
#define REDUCTION_BITWISE_OR(type) REDUCTION_BASE(bitwise_or,type,REDUCTION_INIT_VAL_BITWISE_OR,REDUCTION_UPDATE_STMT_BITWISE_OR)
#define REDUCTION_BITWISE_XOR(type) REDUCTION_BASE(bitwise_xor,type,REDUCTION_INIT_VAL_BITWISE_XOR,REDUCTION_UPDATE_STMT_BITWISE_XOR)

#define REDUCTION_LOGICAL_AND(type) REDUCTION_BASE(logical_and,type,REDUCTION_INIT_VAL_LOGICAL_AND,REDUCTION_UPDATE_STMT_LOGICAL_AND)
#define REDUCTION_LOGICAL_OR(type) REDUCTION_BASE(logical_or,type,REDUCTION_INIT_VAL_LOGICAL_OR,REDUCTION_UPDATE_STMT_LOGICAL_OR)

#endif
