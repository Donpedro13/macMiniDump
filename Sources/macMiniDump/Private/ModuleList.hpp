#ifndef MMD_MODULELIST
#define MMD_MODULELIST

#pragma once

#include <mach/port.h>
#include <uuid/uuid.h>

#include <string>
#include <vector>

namespace MMD {

class ModuleList {
public:
	struct SegmentInfo {
		char segmentName[16];
		uint64_t address;
		uint64_t size;
	};
	
	using Segments = std::vector<SegmentInfo>;
	
	struct ModuleInfo {
		uintptr_t 	loadAddress;
		uuid_t		uuid;
		std::string filePath;
		Segments	segments;
		
		std::unique_ptr<char> headerAndLoadCommandBytes;
		
		ModuleInfo (uintptr_t loadAddress,
					const uuid_t* pUUID,
					const std::string& filePath,
					std::vector<SegmentInfo> segments,
					std::unique_ptr<char[]> headerAndLoadCommandBytes);
	};
	
	explicit ModuleList (mach_port_t taskPort);
	
	bool IsValid () const;
	
	size_t GetSize () const;
	const ModuleInfo& GetModuleInfo (size_t index) const;
	
private:
	std::vector<ModuleInfo> m_moduleInfos;
	
	void Invalidate ();
};

}	// namespace MMD

#endif	// MMD_MODULELIST
