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
