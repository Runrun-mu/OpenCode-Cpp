#pragma once
#include <string>

namespace opencodecpp {

// Terminal color helpers for non-TUI output
namespace color {

inline std::string green(const std::string& s) { return "\033[32m" + s + "\033[0m"; }
inline std::string cyan(const std::string& s) { return "\033[36m" + s + "\033[0m"; }
inline std::string yellow(const std::string& s) { return "\033[33m" + s + "\033[0m"; }
inline std::string red(const std::string& s) { return "\033[31m" + s + "\033[0m"; }
inline std::string bold(const std::string& s) { return "\033[1m" + s + "\033[0m"; }
inline std::string dim(const std::string& s) { return "\033[2m" + s + "\033[0m"; }

} // namespace color
} // namespace opencodecpp
