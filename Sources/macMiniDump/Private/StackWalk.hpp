#ifndef MMD_STACKWALK
#define MMD_STACKWALK

#pragma once

#include <mach/port.h>

#include <vector>

namespace MMD {

std::vector<uint64_t> WalkStack (mach_port_t taskPort, uint64_t instructionPointer, uint64_t basePointer);

} // namespace MMD

#endif // #ifndef MMD_STACKWALK