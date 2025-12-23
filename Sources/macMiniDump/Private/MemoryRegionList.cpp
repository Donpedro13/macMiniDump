#include "MemoryRegionList.hpp"

#include <mach/vm_map.h>

#include <iostream>

namespace MMD {

MemoryRegionList::MemoryRegionList (mach_port_t taskPort)
{
	kern_return_t			 kr		 = KERN_SUCCESS;
	vm_address_t			 address = MACH_VM_MIN_ADDRESS;
	vm_size_t				 size	 = 0;
	natural_t				 depth	 = 0;
	vm_region_submap_info_64 info;
	mach_msg_type_number_t	 infoCount = VM_REGION_SUBMAP_INFO_COUNT_64;

	// With this simple loop we seem to be missing many mappings (compared to the output of vmmap for the same process)
	//   In all my tests thread stacks were included, but still, this should be fixed...
	while (kr == KERN_SUCCESS) {
		kr = vm_region_recurse_64 (taskPort, &address, &size, &depth, (vm_region_recurse_info_t) &info, &infoCount);

		MemoryRegionInfo regionInfo = {};
		regionInfo.vmaddr			= address;
		regionInfo.vmsize			= size;
		regionInfo.prot				= MemProtNone;

		regionInfo.prot = info.protection;

		switch (info.user_tag) {
			case VM_MEMORY_STACK:
				regionInfo.type = info.protection != VM_PROT_NONE ? MemoryRegionType::Stack : MemoryRegionType::Unknown;

				break;

			case VM_MEMORY_MALLOC_NANO:
			case VM_MEMORY_MALLOC_TINY:
			case VM_MEMORY_MALLOC_SMALL:
			case VM_MEMORY_MALLOC_LARGE:
			case VM_MEMORY_MALLOC_LARGE_REUSED:
			case VM_MEMORY_MALLOC_LARGE_REUSABLE:
			case VM_MEMORY_MALLOC_HUGE:
			case VM_MEMORY_REALLOC:
			case VM_MEMORY_SBRK:
				regionInfo.type = MemoryRegionType::Heap;

				break;

			default:
				regionInfo.type = MemoryRegionType::Unknown;
		}

		m_regionInfos.insert ({ regionInfo.vmaddr, regionInfo });

		address += size;
	}
}

bool MemoryRegionList::IsValid () const
{
	return m_regionInfos.empty ();
}

size_t MemoryRegionList::GetSize () const
{
	return m_regionInfos.size ();
}

bool MemoryRegionList::HasAddress (uint64_t address) const
{
	MemoryRegionInfo dummy;

	return GetRegionInfoForAddress (address, &dummy);
}

bool MemoryRegionList::GetRegionInfoForAddress (uint64_t address, const MemoryRegionInfo* pInfoOut) const
{
	if (m_regionInfos.empty ())
		return false;

	auto isInRegion = [] (uint64_t addr, const MemoryRegionInfo& ri) {
		return addr >= ri.vmaddr && addr <= ri.vmaddr + ri.vmsize;
	};

	// Get the element that is greater or equal to the address
	auto it = m_regionInfos.lower_bound (address);

	// Either the address is unknown, or it's "contained" in the last entry
	if (it == m_regionInfos.end ()) {
		if (isInRegion (address, m_regionInfos.rbegin ()->second)) {
			memcpy ((void*) pInfoOut, &m_regionInfos.rbegin ()->second, sizeof (MemoryRegionInfo));

			return true;
		} else {
			return false;
		}
	}

	// If we got the first element, either the address is unknown, or it's contained in the first entry
	// If the address is right at the start of a region, no need to check the previous one
	// Else, the address is either contained in the previous region, or is unknown
	if (it != m_regionInfos.begin () && address != it->first)
		--it;

	if (isInRegion (address, it->second)) {
		memcpy ((void*) pInfoOut, &it->second, sizeof (MemoryRegionInfo));

		return true;
	} else {
		return false;
	}
}

void MemoryRegionList::Invalidate ()
{
	m_regionInfos.clear ();
}

} // namespace MMD
