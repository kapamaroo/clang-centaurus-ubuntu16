#include "llvm/ADT/SmallVector.h"
#include "clang/Basic/OpenACC.h"

#include "Common.hpp"

using namespace clang;
using namespace clang::openacc;

///////////////////////////////////////////////////////////////////////////////
//                        Common tools
///////////////////////////////////////////////////////////////////////////////

void ReplaceStringInPlace(std::string& subject, const std::string& search,
                          const std::string& replace) {
    size_t pos = 0;
    while((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

std::string
RemoveDotExtension(const std::string &filename) {
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return filename;
    return filename.substr(0, lastdot);
}

std::string
GetDotExtension(const std::string &filename) {
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return "";
    return filename.substr(lastdot,std::string::npos-1);
}
