#ifndef MMD_ADD_SEGMENT_COMMAND_FROM_PROCESS_MEMORY
#define MMD_ADD_SEGMENT_COMMAND_FROM_PROCESS_MEMORY

#include "../MachOCoreDumpBuilder.hpp"
#include "../MemoryRegionList.hpp"

namespace MMD {
namespace Utils {

bool AddSegmentCommandFromProcessMemory (MachOCoreDumpBuilder* pCoreBuilder,
												mach_port_t taskPort,
												MMD::MemoryProtection prot,
											    uint64_t startAddress,
												size_t lengthInBytes);

/// @brief Determines protection automatically.
bool AddSegmentCommandFromProcessMemory (MachOCoreDumpBuilder* pCoreBuilder,
												mach_port_t taskPort,
											    uint64_t startAddress,
												size_t lengthInBytes);

} // namespace MMD

} // namespace Util

#endif // MMD_ADD_SEGMENT_COMMAND_FROM_PROCESSOR_MEMORY