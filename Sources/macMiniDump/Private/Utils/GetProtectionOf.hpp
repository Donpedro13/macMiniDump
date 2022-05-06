#ifndef MMD_GET_PROTECTION_OF
#define MMD_GET_PROTECTION_OF

#pragma once

#include <unistd.h>
#include <mach/vm_map.h>
#include <mach/mach_vm.h>

namespace MMD {
namespace Utils {

uint64_t GetProtectionOf (mach_port_t taskPort, uint64_t addr, uint64_t size);
    
} // namespace Utils
    
} // namespace MMD


#endif // MMD_GET_PROTECTION_OF