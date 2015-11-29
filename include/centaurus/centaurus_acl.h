#ifndef _CENTAURUS_ACL_H_
#define _CENTAURUS_ACL_H_

enum acl_device_type {
    ACL_DEV_ALL = 0,
    ACL_DEV_CPU = 0x2,
    ACL_DEV_GPU = 0x4,
    ACL_DEV_FPGA = 0x8
};

#endif

