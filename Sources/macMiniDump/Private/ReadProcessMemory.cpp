#include "ReadProcessMemory.hpp"

#include <mach/mach.h>

namespace MMD {
namespace {

bool GetMemoryRegionEndDistance (task_t task, uintptr_t address, vm_size_t* pDistOut)
{
	constexpr vm_size_t PageSize = 4096;

	vm_size_t result = 0;

	vm_address_t			 regionBase = address;
	vm_size_t				 regionSize;
	natural_t				 nestingLevel = 0;
	vm_region_submap_info_64 submapInfo;
	mach_msg_type_number_t	 infoCount = VM_REGION_SUBMAP_INFO_COUNT_64;
	vm_region_recurse_info_t regionInfo;
	regionInfo = reinterpret_cast<vm_region_recurse_info_t> (&submapInfo);

	if (vm_region_recurse_64 (task, &regionBase, &regionSize, &nestingLevel, regionInfo, &infoCount) == KERN_SUCCESS) {
		result = regionBase + regionSize - address;

		if (result < PageSize) {
			vm_address_t nextRegionBase = regionBase + regionSize;
			vm_size_t	 nextRegionSize;

			if (vm_region_recurse_64 (task, &nextRegionBase, &nextRegionSize, &nestingLevel, regionInfo, &infoCount) ==
				KERN_SUCCESS) {
				if (nextRegionBase == regionBase + regionSize && submapInfo.protection & VM_PROT_READ)
					regionSize += nextRegionSize;
			}
		}

		*pDistOut = regionBase + regionSize - address;

		return true;
	} else {
		return false;
	}
}

} // namespace

std::unique_ptr<char[]> ReadProcessMemory (mach_port_t taskPort, uintptr_t address, size_t size)
{
	std::unique_ptr<char[]> result (new char[size]);

	vm_size_t	  outSize;
	kern_return_t kr =
		vm_read_overwrite (taskPort, address, size, reinterpret_cast<vm_address_t> (result.get ()), &outSize);
	if (kr != KERN_SUCCESS)
		return nullptr;
	else
		return result;
}

bool ReadProcessMemoryString (mach_port_t taskPort, uintptr_t address, size_t maxSize, std::string* pStringOut)
{
	vm_size_t sizeToRead;
	if (!GetMemoryRegionEndDistance (taskPort, address, &sizeToRead))
		return false;

	if (sizeToRead > maxSize)
		sizeToRead = maxSize;

	std::unique_ptr<char[]> pMem = ReadProcessMemory (taskPort, address, sizeToRead);
	if (pMem != nullptr) {
		const char* pCharacters = pMem.get ();
		for (size_t i = 0; i < sizeToRead; ++i) {
			if (pCharacters[i] == '\0') {
				*pStringOut = pCharacters;

				return true;
			}
		}
	}

	return false;
}

} // namespace MMD
