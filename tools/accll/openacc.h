#ifndef __OPENACC_H_
#define __OPENACC_H_

#ifndef _OPENACC
#define _OPENACC 201111
#endif

#include <stddef.h>

typedef enum acc_device_t {
    //Standard defined
    acc_device_none = 0,  //uninitialized value
    acc_device_default,
    acc_device_host,
    acc_device_not_host,

    //Implementation defined
    acc_device_nvidia,
    acc_device_intel,
    acc_device_amd,
} acc_device_t;

#define LAST_ACC_DEVICE_TYPE acc_device_amd

int acc_get_num_devices( acc_device_t );

void acc_set_device_type ( acc_device_t dev_type);

acc_device_t acc_get_device_type ( void );

void acc_set_device_num( int devicenum, acc_device_t dev_type);

int acc_get_device_num( acc_device_t dev_type);

int acc_async_test( int );
int acc_async_test_all( );

void acc_async_wait( int );
void acc_async_wait_all( );

void acc_init ( acc_device_t );
void acc_shutdown ( acc_device_t );

int acc_on_device ( acc_device_t );

void* acc_malloc ( size_t );
void acc_free ( void* );

//PGI uses this function
void acc_set_device( acc_device_t dev_type );

#endif
