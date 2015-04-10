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
    cl_int errcode;

    // Grab the number of devices associated witht the program
    cl_uint num_devices;
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, NULL);
    checkError(errcode, CL_SUCCESS);

    // Grab the device ids
    cl_device_id* devices = (cl_device_id*) malloc(num_devices * sizeof(cl_device_id));
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_DEVICES, num_devices * sizeof(cl_device_id), devices, 0);
    checkError(errcode, CL_SUCCESS);

    // Grab the sizes of the binaries
    size_t* binary_sizes = (size_t*)malloc(num_devices * sizeof(size_t));
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_BINARY_SIZES, num_devices * sizeof(size_t), binary_sizes, NULL);
    checkError(errcode, CL_SUCCESS);

    // Now get the binaries
    char** ptx_code = (char**) malloc(num_devices * sizeof(char*));
    for( unsigned int i=0; i<num_devices; ++i) {
        ptx_code[i]= (char*)malloc(binary_sizes[i]);
    }
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_BINARIES, 0, ptx_code, NULL);
    checkError(errcode, CL_SUCCESS);

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
struct accll::PTXASInfo ParsePTXLog(const std::string &Log) {
    /*
      ptxas info    : 0 bytes gmem
      ptxas info    : Compiling entry function 'kernel_pbpi1' for 'sm_30'
      ptxas info    : Function properties for kernel_pbpi1
      ptxas         .     0 bytes stack frame, 0 bytes spill stores, 0 bytes spill loads
      ptxas info    : Used 28 registers, 348 bytes cmem[0]
      ptxas info    : Compiling entry function 'kernel_pbpi2' for 'sm_30'
      ptxas info    : Function properties for kernel_pbpi2
      ptxas         .     0 bytes stack frame, 0 bytes spill stores, 0 bytes spill loads
      ptxas info    : Used 37 registers, 360 bytes cmem[0]
    */

    struct accll::PTXASInfo info;
    memset(&info,0,sizeof(struct accll::PTXASInfo));

    const char *str = Log.c_str();
    const char *end = str + Log.size();
    while (*str != '\0') {
        while (str != end && !isdigit(*str))
            str++;
        if (str == end)
            break;

        char *lastpos = NULL;
        size_t tmp_value = std::strtol(str,&lastpos,0);
        assert(str != lastpos);
        if (*lastpos == '\0')
            break;
        str = lastpos;
        if (*str == '\'') {
            if (str[1] == '\n')
                //handle arch
                info.arch = tmp_value;
        }
        else if (*str == ']') {
            //ignore
            continue;
        }
        else if (isspace(*str)) {
            //eat space
            str++;
            if (strncmp(str,"bytes gmem",10) == 0) {
                str += 10;
                info.gmem += tmp_value;
            }
            else if (strncmp(str,"bytes cmem",10) == 0) {
                str += 10;
                info.cmem += tmp_value;
            }
            else if (strncmp(str,"bytes stack frame",17) == 0) {
                str += 17;
                info.stack_frame += tmp_value;
            }
            else if (strncmp(str,"bytes spill stores",18) == 0) {
                str += 18;
                info.spill_stores += tmp_value;
            }
            else if (strncmp(str,"bytes spill loads",17) == 0) {
                str += 17;
                info.spill_loads += tmp_value;
            }
            else if (strncmp(str,"registers",9) == 0) {
                str += 9;
                info.registers += tmp_value;
            }
        }
    }

#if 0
    std::cout
        << info.arch
        << "\n"
        << info.registers
        << "\n"
        << info.gmem
        << "\n"
        << info.stack_frame
        << "\n"
        << info.spill_stores
        << "\n"
        << info.spill_loads
        << "\n"
        << info.cmem
        << "\n"
        << "\n"
        ;
#endif

    return info;
}

std::string ocltLogBuildInfo(cl_program cpProgram, cl_device_id cdDevice)
{
    cl_int errcode;
    size_t log_size;
    errcode = clGetProgramBuildInfo(cpProgram, cdDevice, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
    checkError(errcode, CL_SUCCESS);

    if (log_size <= 2)
        return std::string();

    char *buildLog = (char*)malloc((log_size+1));
    if (!buildLog)
        return std::string();

    errcode = clGetProgramBuildInfo(cpProgram, cdDevice, CL_PROGRAM_BUILD_LOG, log_size, buildLog, NULL);
    checkError(errcode, CL_SUCCESS);

    buildLog[log_size] = '\0';
    std::string out(buildLog,log_size);
    free(buildLog);
    return out;
}

}

////////////////////////////////////////////////////////////////////////////////
// entry point
////////////////////////////////////////////////////////////////////////////////

namespace accll {

std::string
KernelRefDef::compile(std::string src, const std::string &platform, const std::vector<std::string> &options) {
    cl_context       clGPUContext;
    cl_program       clProgram;

    size_t           dataBytes;
    size_t           srcLength = src.size();
    cl_int           errcode;

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

    std::string BuildOptions;

    for (std::vector<std::string>::const_iterator
             II = options.begin(), EE = options.end(); II != EE; ++II)
        BuildOptions += *II + " ";

    std::string includesStr;
    std::string definesStr;

    BuildOptions += definesStr;
    BuildOptions += includesStr;

    cl_platform_id   cpPlatform;
    cl_device_id     cdDevice;

    ocltGetPlatformID(&cpPlatform, platform.c_str());

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
        return std::string();
    }

    if (BuildLog.size())
        ParsedBuildLog = ParsePTXLog(BuildLog);

    // Store the binary in the file system
    char* binary;
    size_t binaryLength;
    getProgBinary(clProgram, cdDevices[0], &binary, &binaryLength);
    checkError(errcode, CL_SUCCESS);

    errcode |= clReleaseProgram(clProgram);
    errcode |= clReleaseContext(clGPUContext);
    checkError(errcode, CL_SUCCESS);

    std::string Binary = std::string(binary,binaryLength);

    free(cdDevices);
    free(binary);
    return Binary;
}

}
