#include "LoggingPrivate.hpp"

#include <cassert>
#include <iostream>

namespace MMD {
namespace {

std::string SeverityToString (LogSeverity severity)
{
	switch (severity) {
		case LogSeverity::Debug:
			return "DEBUG";

		case LogSeverity::Info:
			return "INFO";

		case LogSeverity::Warning:
			return "WARNING";

		case LogSeverity::Error:
			return "ERROR";

		default:
			assert (false);

			return "";
	}
}

void DefaultLogCallback (LogSeverity severity, const std::string& message)
{
	std::string linePrefix = SeverityToString (severity) + ": ";

	std::cout << linePrefix << message << std::endl;
}

LogCallback g_logCallback = DefaultLogCallback;

} // namespace

namespace Impl {

LogSeverity g_minSeverity =
#ifdef _DEBUG
	LogSeverity::Debug;
#else
	LogSeverity::MinimumDoNotUse;
#endif

void LogLine (LogSeverity severity, const std::string& logLine)
{
	g_logCallback (severity, logLine);
}

} // namespace Impl

void SetLogCallback (LogCallback newCallback)
{
	g_logCallback = newCallback;
}

void SetMinLogSeverity (LogSeverity minSeverity)
{
	Impl::g_minSeverity = minSeverity;
}

} // namespace MMD
