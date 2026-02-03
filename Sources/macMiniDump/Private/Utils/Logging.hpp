#ifndef MMD_LOGGING
#define MMD_LOGGING

#pragma once

#include <iostream>

namespace MMD {

#ifndef NDEBUG
class DebugLogLine {
public:
    DebugLogLine(const char* file, int line) { 
        std::cerr << "[DEBUG] " << file << ":" << line << " "; 
    }
    ~DebugLogLine() { std::cerr << std::endl; }
    
    template<typename T>
    DebugLogLine& operator<<(const T& value) {
        std::cerr << value;
        return *this;
    }
    
    DebugLogLine& operator<<(std::ostream& (*manip)(std::ostream&)) {
        manip(std::cerr);
        return *this;
    }
};
#endif // NDEBUG

} // namespace MMD

#ifdef NDEBUG
    // Expressions are not evaluated in release builds
    #define MMD_DEBUGLOG if (true) {} else std::cerr
    #define MMD_DEBUGLOG_LINE if (true) {} else std::cerr
#else
    #define MMD_DEBUGLOG std::cerr << "[DEBUG] "
    #define MMD_DEBUGLOG_LINE MMD::DebugLogLine(__FILE_NAME__, __LINE__)
#endif

#endif // MMD_LOGGING