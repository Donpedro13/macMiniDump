#ifndef MMD_STACKWALK
#define MMD_STACKWALK

#pragma once

#include <mach/port.h>

#include "MachOCoreInternal.hpp"
#include "MemoryRegionList.hpp"
#include "ModuleList.hpp"
#include "ZoneAllocator.hpp"

namespace MMD {

Vector<uint64_t> WalkStack (mach_port_t				taskPort,
							const MemoryRegionList& memoryRegionList,
							const ModuleList&		moduleList,
							const MachOCore::GPR&	gpr,
							const MachOCore::EXC&	exc);

} // namespace MMD

#endif // #ifndef MMD_STACKWALK