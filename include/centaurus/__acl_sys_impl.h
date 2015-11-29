#ifndef __ACL_SYS_IMPL_H__
#define __ACL_SYS_IMPL_H__

#define __CENTAURUS_OVERLOAD__ __attribute__((overloadable))
//#define __CENTAURUS_INLINE__ __attribute__((always_inline))

__CENTAURUS_OVERLOAD__ int min(int x, int y);
__CENTAURUS_OVERLOAD__ float min(float x, float y);
__CENTAURUS_OVERLOAD__ double min(double x, double y);

__CENTAURUS_OVERLOAD__ int max(int x, int y);
__CENTAURUS_OVERLOAD__ float max(float x, float y);
__CENTAURUS_OVERLOAD__ double max(double x, double y);

__CENTAURUS_OVERLOAD__ int __stdlib_abs(int x);

#undef __CENTAURUS_OVERLOAD__

//#define ACL_CAST_CL_API_VALUE_AS(type, x) x
//#define ACL_CAST_CL_API_PTR_AS(type, x) x

#ifndef __CENTAURUS__
// early include of Centaurus platform
// because we include this from the command line
#include "CL/centaurus_cl_platform.h"
#endif

#endif
