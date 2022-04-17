#ifndef MMD_MEMORYREGIONLIST
#define MMD_MEMORYREGIONLIST

#pragma once

#include <mach/vm_prot.h>

#include <cstdint>
#include <vector>

namespace MMD {

using MemoryProtection = uint8_t;
enum MemoryProtectionTypes : MemoryProtection {
	MemProtNone  = 0,
	MemProtRead  = 0b001,
	MemProtWrite = 0b010,
	MemProtExecute = 0b100
};

enum class MemoryRegionType {
	Unknown,
	Stack,
	Heap
};

struct MemoryRegionInfo {
	uint64_t vmaddr;
	uint64_t vmsize;
	
	MemoryProtection prot;
	
	MemoryRegionType type;
};

class MemoryRegionList {
public:
	using MemoryRegions = std::vector<MemoryRegionInfo>;
	
	explicit MemoryRegionList (mach_port_t taskPort);
	
	bool IsValid () const;
	
	size_t GetSize () const;
	const MemoryRegionInfo& GetMemoryRegionInfo (size_t index) const;
	
private:
	MemoryRegions m_regionInfos;
	
	void Invalidate ();
};

}	// namespace MMD

#endif	// MMD_MEMORYREGIONLIST
