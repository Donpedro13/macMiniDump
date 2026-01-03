#ifndef MMD_STACKWALK
#define MMD_STACKWALK

#pragma once

#include <mach/port.h>

#include <vector>

#include "MachOCoreInternal.hpp"
#include "MemoryRegionList.hpp"
#include "ModuleList.hpp"

namespace MMD {

std::vector<uint64_t> WalkStack (mach_port_t			 taskPort,
								 const MemoryRegionList& memoryRegionList,
								 const ModuleList&		 moduleList,
								 const MachOCore::GPR&	 gpr,
								 const MachOCore::EXC&	 exc);

} // namespace MMD

#endif // #ifndef MMD_STACKWALK