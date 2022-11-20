#ifndef MMD_MODULELIST
#define MMD_MODULELIST

#pragma once

#include <mach/port.h>
#include <uuid/uuid.h>

#include <map>
#include <string>
#include <vector>

namespace MMD {

class ModuleList {
public:
	struct SegmentInfo {
		char	 segmentName[16];
		uint64_t address;
		uint64_t size;
	};

	using Segments = std::vector<SegmentInfo>;

	struct ModuleInfo {
		uintptr_t	loadAddress;
		uuid_t		uuid;
		std::string filePath;
		Segments	segments;
		bool		executing;

		std::unique_ptr<char> headerAndLoadCommandBytes;

		ModuleInfo ();
		ModuleInfo (uintptr_t				 loadAddress,
					const uuid_t*			 pUUID,
					const std::string&		 filePath,
					std::vector<SegmentInfo> segments,
					bool					 executing,
					std::unique_ptr<char[]>	 headerAndLoadCommandBytes);
	};

	using ModuleInfos = std::map<uint64_t, ModuleInfo>;

	explicit ModuleList (mach_port_t taskPort);

	bool IsValid () const;

	size_t GetSize () const;

	ModuleInfos::const_iterator begin () const { return m_moduleInfos.begin (); }
	ModuleInfos::const_iterator end () const { return m_moduleInfos.end (); }

	bool GetModuleInfoForAddress (uint64_t address, ModuleInfo** pModuleInfoOut);

	bool MarkAsExecuting (uint64_t codeAddress);

private:
	ModuleInfos m_moduleInfos;

	void Invalidate ();
};

} // namespace MMD

#endif // MMD_MODULELIST
