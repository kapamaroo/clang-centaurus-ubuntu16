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

#include <sstream>
#include <iomanip>

#include "Types.hpp"
#include "Common.hpp"
#include "ocl_utils.hpp"

namespace {

std::string ToHex(const std::string src) {
    std::string out;
    //llvm::raw_string_ostream OS(out);
    std::stringstream OS;

    for (size_t i=0; i<src.size(); ++i) {
        if (i)
            OS << ",";
        OS << "0x"
           << std::setfill('0')
           << std::setw(2)
           << std::hex
           << (int)(unsigned char)src[i];
    }

    return OS.str();
}

std::vector<std::string> getProgBinary(cl_program cpProgram, cl_device_id *clDevices, cl_uint device_num)
{
    cl_int errcode;

    // Grab the number of devices associated witht the program
    cl_uint num_devices;
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, NULL);
    checkError(errcode, CL_SUCCESS);

    // Grab the device ids
    cl_device_id* devices = (cl_device_id*) malloc(num_devices * sizeof(cl_device_id));
    if (!devices)
        return std::vector<std::string>();
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_DEVICES, num_devices * sizeof(cl_device_id), devices, 0);
    checkError(errcode, CL_SUCCESS);

    //sanity checks
    assert(num_devices == device_num);
    for (cl_uint i=0; i<device_num; ++i)
        assert(devices[i] == clDevices[i]);

    // Grab the sizes of the binaries
    size_t* binary_sizes = (size_t*)malloc(num_devices * sizeof(size_t));
    if (!binary_sizes)
        return std::vector<std::string>();
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_BINARY_SIZES, num_devices * sizeof(size_t), binary_sizes, NULL);
    checkError(errcode, CL_SUCCESS);

    // Now get the binaries
    char** ptx_code = (char**) malloc(num_devices * sizeof(char*));
    if (!ptx_code)
        return std::vector<std::string>();
    for( unsigned int i=0; i<num_devices; ++i) {
        ptx_code[i]= (char*)malloc(binary_sizes[i]);
    }
    errcode = clGetProgramInfo(cpProgram, CL_PROGRAM_BINARIES, num_devices * sizeof(ptx_code), ptx_code, NULL);
    checkError(errcode, CL_SUCCESS);

    std::vector<std::string> Binary;
    for(unsigned int i=0; i<num_devices; ++i) {
        std::string bin(ptx_code[i],binary_sizes[i]);
        Binary.push_back(bin);
    }

    // Cleanup
    free( devices );
    free( binary_sizes );
    for( unsigned int i=0; i<num_devices; ++i) {
        free(ptx_code[i]);
    }
    free( ptx_code );

    return Binary;
}

////////////////////////////////////////////////////////////////////////////////
// Get the build log from ocl
////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> ocltLogBuildInfo(cl_program cpProgram, cl_device_id *cdDevice, cl_uint device_num)
{
    cl_int errcode;

    std::vector<std::string> out;

    cl_uint i;
    for (i=0; i<device_num; ++i) {
        size_t log_size;
        errcode = clGetProgramBuildInfo(cpProgram, cdDevice[i], CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        checkError(errcode, CL_SUCCESS);

        if (log_size <= 2) {
            out.push_back(std::string());
            continue;
        }

        char *buildLog = (char*)malloc((log_size+1));
        if (!buildLog) {
            out.push_back(std::string());
            continue;
        }

        errcode = clGetProgramBuildInfo(cpProgram, cdDevice[i], CL_PROGRAM_BUILD_LOG, log_size, buildLog, NULL);
        checkError(errcode, CL_SUCCESS);

        buildLog[log_size] = '\0';
        std::string log(buildLog,log_size);
        out.push_back(log);
        free(buildLog);
    }

    return out;
}

}

////////////////////////////////////////////////////////////////////////////////
// entry point
////////////////////////////////////////////////////////////////////////////////

namespace accll {
    PTXASInfo::PTXASInfo(std::string Log, std::string PlatformName) :
    Raw(Log), arch(0), registers(0), gmem(0),
    stack_frame(0), spill_stores(0), spill_loads(0), cmem(0)
{
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

    if (PlatformName.compare("NVIDIA"))
        return;

    if (!Log.size())
        return;

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
                arch = tmp_value;
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
                gmem += tmp_value;
            }
            else if (strncmp(str,"bytes cmem",10) == 0) {
                str += 10;
                cmem += tmp_value;
            }
            else if (strncmp(str,"bytes stack frame",17) == 0) {
                str += 17;
                stack_frame += tmp_value;
            }
            else if (strncmp(str,"bytes spill stores",18) == 0) {
                str += 18;
                spill_stores += tmp_value;
            }
            else if (strncmp(str,"bytes spill loads",17) == 0) {
                str += 17;
                spill_loads += tmp_value;
            }
            else if (strncmp(str,"registers",9) == 0) {
                str += 9;
                registers += tmp_value;
            }
        }
    }
}

PlatformBin _compile(std::string src, std::string SymbolName, std::string PrefixDef,
                   const std::vector<std::string> &options,
                   cl_platform_id cpPlatform) {
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

    char buffer[2048];
    std::string PlatformName;
    errcode = clGetPlatformInfo(cpPlatform, CL_PLATFORM_NAME, 2048, &buffer, NULL);
    if(errcode == CL_SUCCESS)
        PlatformName = std::string(buffer,strlen(buffer));

    std::string BuildOptions;

    for (std::vector<std::string>::const_iterator
             II = options.begin(), EE = options.end(); II != EE; ++II)
        BuildOptions += *II + " ";

    if (PlatformName.find("NVIDIA") != std::string::npos) {
        BuildOptions += "-cl-nv-verbose";
        PlatformName = "NVIDIA";
    }
    else if (PlatformName.find("Intel") != std::string::npos) {
        PlatformName = "INTEL";
    }
    else if (PlatformName.find("AMD") != std::string::npos) {
        PlatformName = "AMD";
    }
    else {
        PlatformName = "UNKNOWN";
    }

    std::string includesStr;
    std::string definesStr;

    BuildOptions += definesStr;
    BuildOptions += includesStr;

    cl_uint device_num;
    errcode = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_ALL, 0, NULL, &device_num);
    checkError(errcode, CL_SUCCESS);

    cl_device_id *cdDevice = (cl_device_id *)malloc(sizeof(cl_device_id)*device_num);

    // Get a GPU device
    errcode = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_ALL, device_num, cdDevice, NULL);
    checkError(errcode, CL_SUCCESS);

    clGPUContext = clCreateContext(0, device_num, cdDevice, NULL, NULL, &errcode);
    checkError(errcode, CL_SUCCESS);

    // get the list of devices associated with context
    errcode = clGetContextInfo(clGPUContext, CL_CONTEXT_DEVICES, 0, NULL, &dataBytes);
    cl_device_id *cdDevices = (cl_device_id *)malloc(dataBytes);
    errcode |= clGetContextInfo(clGPUContext, CL_CONTEXT_DEVICES, dataBytes, cdDevices, NULL);
    checkError(errcode, CL_SUCCESS);

    //sanity check
    for (unsigned int i=0; i<device_num; ++i)
        assert(cdDevice[i] == cdDevices[i]);

    free(cdDevice);

    const char *c_str = src.c_str();
    clProgram = clCreateProgramWithSource(clGPUContext, 1, (const char **)&c_str, &srcLength, &errcode);
    checkError(errcode, CL_SUCCESS);

    errcode = clBuildProgram(clProgram, 0, NULL, BuildOptions.c_str(), NULL, NULL);

    // Get the build log... Without doing this it is next to impossible to
    // debug a failed .cl build
    std::vector<std::string> RawBuildLogs = ocltLogBuildInfo(clProgram, cdDevices, device_num);
    if(errcode != CL_SUCCESS) {
        return PlatformBin();
    }

    // Store the binary in the file system
    std::vector<std::string> BinArray = getProgBinary(clProgram, cdDevices, device_num);

    errcode |= clReleaseProgram(clProgram);
    errcode |= clReleaseContext(clGPUContext);
    checkError(errcode, CL_SUCCESS);

    free(cdDevices);

    //sanity check
    assert(RawBuildLogs.size() == BinArray.size());

    if (!BinArray.size()) {
        //ObjRefDef Empty("NULL",std::string());
        //return KernelBin(std::string(),Empty,std::string());
        return PlatformBin();
    }

    PlatformBin PlatformBinary(PlatformName);
    PlatformBinary.NameRef = PrefixDef + PlatformName;

    std::string DevTableName = PrefixDef + PlatformName + "_DEV_TABLE";
    std::string DevTable = "struct _device_bin *" + DevTableName + "[" + toString(BinArray.size()) + "] = {";

    for (std::vector<std::string>::size_type i=0; i<BinArray.size(); ++i) {
        std::string APINameRef = PrefixDef + PlatformName + "__device" + toString(i);
        if (i)
            DevTable += ",";
        DevTable += "&" + APINameRef;

        std::string NameRef = "__bin__" + APINameRef + "__" + SymbolName;
#if 1
        std::string HexBinArray = ToHex(BinArray[i]);
#else
        std::string HexBinArray;
#endif
        std::string HeaderDecl = "extern const unsigned char " + NameRef
            + "[" + toString(BinArray[i].size()) + "];";
        std::string Definition = "const unsigned char " + NameRef
            + "[" + toString(BinArray[i].size()) + "]"
            +" = "
            + "{" + HexBinArray + "};";

        std::string APIDefinition = "struct _device_bin " + APINameRef + " = {"
            + ".bin = " + NameRef
            + ",.bin_size = " + toString(BinArray[i].size())
            + "};";

        DeviceBin DeviceBinary(APINameRef,APIDefinition,
                               PlatformName,ObjRefDef(NameRef,Definition,HeaderDecl),
                               RawBuildLogs[i]);
        PlatformBinary.push_back(DeviceBinary);
    }

    DevTable += "};";
    PlatformBinary.Definition = DevTable
        + "struct _platform_bin " + PlatformBinary.NameRef + " = {"
        + ".device_table = " + DevTableName
        + ",.device_num = " + toString(BinArray.size())
        + "};";

    return PlatformBinary;
}

int
KernelRefDef::compile(std::string src, std::string SymbolName, std::string PrefixDef,
                      const std::vector<std::string> &options)
{
    cl_uint         num_platforms;
    cl_platform_id* clPlatformIDs;
    cl_int          status;

    status = clGetPlatformIDs(0, NULL, &num_platforms);
    if (status != CL_SUCCESS)
    {
        std::cerr << "Error " << status << "in clGetPlatformIDs" << std::endl;
        return -1;
    }
    if(num_platforms == 0)
    {
        std::cerr << "No OpenCL platform found!" << std::cout;
        return -2;
    }
    else
    {
        clPlatformIDs = (cl_platform_id*)malloc(num_platforms * sizeof(cl_platform_id));

        status = clGetPlatformIDs(num_platforms, clPlatformIDs, NULL);
        for(uint i = 0; i < num_platforms; ++i)
        {
            PlatformBin PlatformBinary = _compile(src,SymbolName,PrefixDef,options,clPlatformIDs[i]);
            Binary.push_back(PlatformBinary);
        }

        free(clPlatformIDs);
    }

    return 0;
}

}
