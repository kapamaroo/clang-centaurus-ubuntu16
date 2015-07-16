#ifndef __CENTAURUS_CONFIG_H__
#define __CENTAURUS_CONFIG_H__

#include <vector>
#include <string>

namespace accll {

struct CentaurusConfig {
    bool ProfileMode;
    bool CompileOnly;

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
