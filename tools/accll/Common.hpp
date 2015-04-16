#ifndef ACCLL_COMMON_HPP_
#define ACCLL_COMMON_HPP_

#include <sstream>

///////////////////////////////////////////////////////////////////////////////
//                        Common tools
///////////////////////////////////////////////////////////////////////////////

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define DEBUG ANSI_COLOR_YELLOW "debug: " ANSI_COLOR_RESET
#define NOTE ANSI_COLOR_CYAN "note: " ANSI_COLOR_RESET
#define WARNING ANSI_COLOR_MAGENTA "warning: " ANSI_COLOR_RESET
#define ERROR ANSI_COLOR_RED "error: " ANSI_COLOR_RESET

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
