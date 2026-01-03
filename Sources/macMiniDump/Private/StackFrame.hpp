#ifndef MMD_STACK_FRAME
#define MMD_STACK_FRAME

#pragma once

#include <cstdint>
#include <mach/port.h>

#include "ModuleList.hpp"

namespace MMD {

enum class StackFrameLookupResult { HasFrame, Frameless, Unknown };

StackFrameLookupResult LookupStackFrameForPC (mach_port_t taskPort, const ModuleList& moduleList, uintptr_t pc);

} // namespace MMD

#endif // MMD_STACK_FRAME