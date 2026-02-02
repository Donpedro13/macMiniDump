#ifndef MMD_LOGGING
#define MMD_LOGGING

#pragma once

#include <functional>
#include <string>

namespace MMD {

enum class LogSeverity {
	Debug = 1,
	Info,
	Warning,
	Error,

	MinimumDoNotUse = Debug - 1
};

using LogCallback = std::function<void (LogSeverity severity, const std::string& message)>;

void SetLogCallback (LogCallback newCallback);
void SetMinLogSeverity (LogSeverity minSeverity);

} // namespace MMD

#endif // MMD_LOGGING