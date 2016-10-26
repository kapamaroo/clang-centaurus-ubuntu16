#ifndef __CENTAURUS_CONFIG_H__
#define __CENTAURUS_CONFIG_H__

#include <vector>
#include <string>

namespace acl {

struct CentaurusConfig {
    bool ProfileMode;
    bool CompileOnly;
    bool isCXX;
    bool NoArgs;

    int NvidiaDriverVersion;

    std::string InstallPath;
    std::string IncludePath;
    std::string CustomSystemHeaders;
    std::string LibPath;
    std::string LinkerPath;
    std::string ClangPath;
    std::string SPIRToolPath;

    std::string UserDefinedOutputFile;

    std::vector<std::string> ExtraCompilerFlags;
    std::vector<std::string> ExtraLinkerFlags;
    std::vector<std::string> InputFiles;

    std::vector<std::string> OutputFiles;
    std::vector<std::string> LibOCLFiles;
    std::vector<std::string> KernelFiles;
    std::vector<std::string> RegularFiles;

    CentaurusConfig(int argc, const char *argv[]);
    CentaurusConfig() {}

    void print() const;
};

}

#endif
