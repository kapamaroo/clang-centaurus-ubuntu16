#ifndef ACCLL_COMMON_HPP_
#define ACCLL_COMMON_HPP_

///////////////////////////////////////////////////////////////////////////////
//                        Common tools
///////////////////////////////////////////////////////////////////////////////

void ReplaceStringInPlace(std::string& subject, const std::string& search,
                          const std::string& replace);


std::string RemoveDotExtension(const std::string &filename);
std::string GetDotExtension(const std::string &filename);

#endif
