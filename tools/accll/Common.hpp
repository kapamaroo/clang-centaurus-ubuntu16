#ifndef ACCLL_COMMON_HPP_
#define ACCLL_COMMON_HPP_

#include <sstream>

///////////////////////////////////////////////////////////////////////////////
//                        Common tools
///////////////////////////////////////////////////////////////////////////////

void ReplaceStringInPlace(std::string& subject, const std::string& search,
                          const std::string& replace);


std::string RemoveDotExtension(const std::string &filename);
std::string GetDotExtension(const std::string &filename);
std::string GetBasename(const std::string &filename);

template <typename T>
std::string toString(const T &x) {
    std::stringstream OS;
    OS << x;
    return std::string(OS.str());
}

#endif
