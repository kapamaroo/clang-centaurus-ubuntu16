/*
  OCLTools is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Dr. Zaius
  ClusterChimps.org
*/

// standard utilities and systems includes
#include <vector>
#include <string>
#include <iostream>
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include "Types.hpp"
#include "ocl_utils.hpp"

namespace {

void getProgBinary(cl_program cpProgram, cl_device_id cdDevice, char** binary, size_t* length)
{
    // Grab the number of devices associated witht the program
    cl_uint num_devices;
    clGetProgramInfo(cpProgram, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, NULL);

    // Grab the device ids
    cl_device_id* devices = (cl_device_id*) malloc(num_devices * sizeof(cl_device_id));
    clGetProgramInfo(cpProgram, CL_PROGRAM_DEVICES, num_devices * sizeof(cl_device_id), devices, 0);

    // Grab the sizes of the binaries
    size_t* binary_sizes = (size_t*)malloc(num_devices * sizeof(size_t));
    clGetProgramInfo(cpProgram, CL_PROGRAM_BINARY_SIZES, num_devices * sizeof(size_t), binary_sizes, NULL);

    // Now get the binaries
    char** ptx_code = (char**) malloc(num_devices * sizeof(char*));
    for( unsigned int i=0; i<num_devices; ++i) {
        ptx_code[i]= (char*)malloc(binary_sizes[i]);
    }
    clGetProgramInfo(cpProgram, CL_PROGRAM_BINARIES, 0, ptx_code, NULL);

    // Find the index of the device of interest
    unsigned int idx = 0;
    while( idx<num_devices && devices[idx] != cdDevice ) ++idx;

    // If it is associated prepare the result
    if( idx < num_devices )
        {
            *binary = ptx_code[idx];
            *length = binary_sizes[idx];
        }

    // Cleanup
    free( devices );
    free( binary_sizes );
    for( unsigned int i=0; i<num_devices; ++i) {
        if( i != idx ) free(ptx_code[i]);
    }
    free( ptx_code );
}

////////////////////////////////////////////////////////////////////////////////
// Get the build log from ocl
////////////////////////////////////////////////////////////////////////////////
std::string ocltLogBuildInfo(cl_program cpProgram, cl_device_id cdDevice)
{
    char buildLog[10240];
    clGetProgramBuildInfo(cpProgram, cdDevice, CL_PROGRAM_BUILD_LOG, sizeof(buildLog), buildLog, NULL);
    return std::string(buildLog,10240);
}

}

////////////////////////////////////////////////////////////////////////////////
// entry point
////////////////////////////////////////////////////////////////////////////////

namespace accll {

bool
KernelRefDef::compile(std::string src, const std::vector<std::string> &options) {
    cl_context       clGPUContext;
    cl_program       clProgram;

    size_t           dataBytes;
    size_t           srcLength = src.size();
    cl_int           errcode;

    Binary = "NULL";

    std::string platform;
    // Declare the supported options.
    /*
        ("includes,I",po::value<std::vector<std::string > >( ), "Directories to search for headers.\n")
        ("define,D",po::value<std::vector<std::string > >( ), "Defines a macro.\n")
        ("platform",po::value<std::string >(&platform)->default_value("NVIDIA"), "Platform to compile for.\n")
        ("cl-single-precision-constant",     "Treat floating-point constant as single \nprecision constant "
         "instead of implicitly converting it to double precision "
         "constant. This is valid only when the double precision extension "
         "is supported. This is the default if double precision "
         "floating-point is not supported.\n")
        ("cl-denorms-are-zero",              "This option controls how single precision and double precision denormalized "
         "numbers are handled.\n")
        ("cl-opt-disable",                   "This option disables all optimizations. The default is optimizations are "
         "enabled.\n")
        ("cl-strict-aliasing",               "This option allows the compiler to assume the strictest aliasing rules.\n")
        ("cl-mad-enable",                    "Allows a * b + c to be replaced by a mad. This will result in reduced accuracy.\n")
        ("cl-no-signed-zeros",               "Allow optimizations for floating-point \narithmetic that ignore the signedness of zero.\n")
        ("cl-unsafe-math-optimizations",     "Allow optimizations for floating-point \narithmetic that (a) assume that arguments and "
         "results are valid, (b) may violate IEEE 754 standard and (c) may violate the OpenCL "
         "\nnumerical compliance requirements.\n")
        ("cl-finite-math-only",              "Allow optimizations for floating-point arithmetic that assume that arguments and results "
         "are not NaNs or +/- Inf.\n")
        ("cl-fast-relaxed-math",             "Sets the optimization options \n--cl-finite-math-only and \n--cl-unsafe-math-optimizations.\n")
        ("cl-nv-maxrregcount",po::value<std::string >( ), "Nvidia specific: The max number of registers a GPU function can use.\n")
        ("cl-nv-opt-level",po::value<std::string >( ), "Nvidia specific: optimization level (0 - 3).\n")
        ("cl-nv-verbose",                    "Nvidia specific: turns on verbose build\noutput.\n")
        ("warnings-off,w",                   "Turn off warnings.\n")
        ("Werror",                           "Turn warnings into errors.\n");
    */

    std::string includesStr;
    std::string definesStr;
    std::string BuildOptions;

    BuildOptions = definesStr;
    BuildOptions += includesStr;

    cl_platform_id   cpPlatform;
    cl_device_id     cdDevice;

    errcode = ocltGetPlatformID(&cpPlatform, platform.c_str());
    checkError(errcode, CL_SUCCESS);

    // Get a GPU device
    errcode = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_GPU, 1, &cdDevice, NULL);
    checkError(errcode, CL_SUCCESS);

    clGPUContext = clCreateContext(0, 1, &cdDevice, NULL, NULL, &errcode);

    checkError(errcode, CL_SUCCESS);

    // get the list of GPU devices associated with context
    errcode = clGetContextInfo(clGPUContext, CL_CONTEXT_DEVICES, 0, NULL, &dataBytes);
    cl_device_id *cdDevices = (cl_device_id *)malloc(dataBytes);
    errcode |= clGetContextInfo(clGPUContext, CL_CONTEXT_DEVICES, dataBytes, cdDevices, NULL);
    checkError(errcode, CL_SUCCESS);

    const char *c_str = src.c_str();
    clProgram = clCreateProgramWithSource(clGPUContext, 1, (const char **)&c_str, &srcLength, &errcode);
    checkError(errcode, CL_SUCCESS);

    errcode = clBuildProgram(clProgram, 0, NULL, BuildOptions.c_str(), NULL, NULL);

    // Get the build log... Without doing this it is next to impossible to
    // debug a failed .cl build
    BuildLog = ocltLogBuildInfo(clProgram, cdDevices[0]);
    if(errcode != CL_SUCCESS) {
        return false;
    }

    // Store the binary in the file system
    char* binary;
    size_t binaryLength;
    getProgBinary(clProgram, cdDevices[0], &binary, &binaryLength);
    checkError(errcode, CL_SUCCESS);

    errcode |= clReleaseProgram(clProgram);
    errcode |= clReleaseContext(clGPUContext);
    checkError(errcode, CL_SUCCESS);

    free(cdDevices);

    Binary = std::string(binary,binaryLength);

    return true;
}

}
