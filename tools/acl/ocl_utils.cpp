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
#include <stdio.h>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <map>
#include <CL/cl.h>

extern "C" {
   extern char __ocl_code_start __attribute__((weak));
   extern char __ocl_code_end   __attribute__((weak));
}

void           ocltExtractKernels();
char*          ocltLoadKernelSrc(const char* filename, size_t* length);
unsigned char* ocltLoadKernelBin(const char* filename, char** compilerFlags, size_t* length);
unsigned char* ocltGetEmbeddedKernelBin(char* kernelName, char** compilerFlags, size_t* length);
unsigned char* ocltGetEmbeddedKernelSrc(char* kernelName, size_t* length);

std::map<std::string, std::string> __kernel_map__;
std::map<std::string, std::string> __flag_map__;

////////////////////////////////////////////////////////////////////////////////
// Extracts embedded kernels from application binary
////////////////////////////////////////////////////////////////////////////////
void ocltExtractKernels()
{
   size_t size = &__ocl_code_end - &__ocl_code_start;
   char *start = &__ocl_code_start;

   if(size < 5)
   {
      std::cout << "OCLTools[ERROR] In call to ocltExtractKernels" << std::endl;
      std::cout << "                Can't extract kernel from binary" << std::endl;
      std::cout << "                Did you forget to link in your kernel binary?" << std::endl;
      exit(1);
   }

   unsigned char* buffer = (unsigned char*) malloc (size);

   size_t length = size;
   memcpy(buffer, start, size);

   std::string blob((const char*)buffer, length);
   std::string::size_type start_flag   = 0;
   std::string::size_type kernel_start = 0;
   while((start_flag = blob.find("!@#~", start_flag)) != std::string::npos)
    {
      std::string::size_type end_flag = blob.find("!@#~", start_flag + 1);

      std::string compilerFlags;
      if((end_flag - start_flag) > 5)
      {
         compilerFlags = blob.substr(start_flag + 4, end_flag - start_flag - 4);
      }

      std::string::size_type end_name = blob.find("!@#~", end_flag + 1);

      std::string name;
      if((end_name - end_flag) > 5)
      {
         name = blob.substr(end_flag + 4, end_name - end_flag - 4);
      }

      std::string kernel = blob.substr(kernel_start, start_flag - kernel_start - 2);

      __kernel_map__[name] = kernel;
      __flag_map__[name]   = compilerFlags;

//std::cout << "NAME "   << name          << std::endl;
//std::cout << "FLAGS "  << compilerFlags << std::endl;
//std::cout << "KERNEL " << kernel        << std::endl;

      kernel_start = end_name + 4;
      start_flag   = end_name + 1;
   }

}

////////////////////////////////////////////////////////////////////////////////
// Gets embedded kernel binary from MAP
////////////////////////////////////////////////////////////////////////////////
unsigned char* ocltGetEmbeddedKernelBin(char* kernelName, char** compilerFlags, size_t* length)
{

   std::string kernel = __kernel_map__[kernelName];
   *length = kernel.length();

   if(*length < 5)
   {
      std::cerr << "OCLTools[ERROR] " << std::endl;
      std::cerr << "In call to ocltGetEmbeddedKernelBin" << std::endl;
      std::cerr << "The kernel name you are looking for (" << kernelName << ") is not embedded in this binary" << std::endl;
      std::cerr << "Either you forgot to call ocltExtractKernels or you have a typo in your kernel name" << std::endl;
      *compilerFlags = 0;
      *length = 0;
      return(NULL);
   }

   char* kernelCStr = (char *)malloc(*length + 1);
   kernel.copy(kernelCStr, *length);
   kernelCStr[*length] = '\0';

   std::string flags = __flag_map__[kernelName];
   size_t flagLength = flags.length();

   char* flagCStr = (char *)malloc(flagLength + 1);
   flags.copy(flagCStr, flagLength);
   flagCStr[flagLength] = '\0';
   *compilerFlags = flagCStr;

   return (unsigned char*)kernelCStr;
}

////////////////////////////////////////////////////////////////////////////////
// Gets Embedded Kernel Source from MAP
////////////////////////////////////////////////////////////////////////////////
unsigned char* ocltGetEmbeddedKernelSrc(char* kernelName, size_t* length)
{
   std::string kernel = __kernel_map__[kernelName];
   *length = kernel.length();

   if(*length < 5)
   {
      std::cerr << "OCLTools[ERROR] " << std::endl;
      std::cerr << "In call to ocltGetEmbeddedKernelSrc" << std::endl;
      std::cerr << "The kernel name you are looking for (" << kernelName << ") is not embedded in this binary" << std::endl;
      std::cerr << "Either you forgot to call ocltExtractKernels or you have a typo in your kernel name" << std::endl;
      *length = 0;
      return(NULL);
   }

   char* kernelCStr = (char *)malloc(*length + 1);
   kernel.copy(kernelCStr, *length);
   kernelCStr[*length + 1] = '\0';

   return (unsigned char*)kernelCStr;
}

////////////////////////////////////////////////////////////////////////////////
// Loads a kernel binary from file system
////////////////////////////////////////////////////////////////////////////////
unsigned char* ocltLoadKernelBin(const char* filename, char** compilerFlags, size_t* length)
{
   FILE* fp = fopen(filename, "r");
   if(fp == 0)
        return NULL;

   fseek (fp , 0 , SEEK_END);
   *length = ftell(fp);
   rewind(fp);
   unsigned char* buffer;
   buffer = (unsigned char*) malloc (*length);
   fread(buffer, 1, *length, fp);
   fclose(fp);

   std::string blob((const char*)buffer, *length);
   size_t start_cookie = blob.find("!@#~", 0);

   // remove the '//' from the compiler flags line
   *length = start_cookie - 2;
   size_t end_cookie   = blob.find("!@#~", start_cookie + 1);

   std::string flags = blob.substr(start_cookie+4, end_cookie) ;

   if(compilerFlags != NULL)
   {
      size_t flagLength = flags.length() + 1;
      char* flagBuffer = (char *)malloc(flagLength);
      flags.copy(flagBuffer, flagLength - 1);
      flagBuffer[flagLength] = '\0';

      *compilerFlags = flagBuffer;
   }

   return buffer;
}
