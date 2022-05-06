#include "GetProtectionOf.hpp"
#include "RegionInfo.hpp"

#include <mach/vm_map.h>
#include <mach/mach_vm.h>

uint64_t MMD::Utils::GetProtectionOf (mach_port_t taskPort, uint64_t addr, uint64_t size)
{
	natural_t nesting_depth;
	RegionInfo info;
	mach_msg_type_number_t infoCnt;

	uint64_t       recurseAddr = addr;
	mach_vm_size_t recurseSize = size;

	::mach_vm_region_recurse (taskPort,
							&recurseAddr,
							&recurseSize,
							&nesting_depth,
							(vm_region_recurse_info_t)&info, // scummed from llvm
							&infoCnt);

	return info.protection;
}