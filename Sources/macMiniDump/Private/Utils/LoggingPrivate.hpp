#ifndef MMD_LOGGING_PRIVATE
#define MMD_LOGGING_PRIVATE

#pragma once

#include "MMD/Logging.hpp"

namespace MMD {
namespace Impl {

extern LogSeverity g_minSeverity;

void LogLine (LogSeverity severity, const std::string& logLine);

} // namespace Impl

inline void LogLine (LogSeverity severity, const std::string& logLine)
{
	if (severity < Impl::g_minSeverity)
		return;
	else
		Impl::LogLine (severity, logLine);
}

} // namespace MMD

#endif // MMD_LOGGING_PRIVATE