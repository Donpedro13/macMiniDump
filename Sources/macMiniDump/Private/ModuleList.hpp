#ifndef MMD_MODULELIST
#define MMD_MODULELIST

#pragma once

#include <mach/port.h>
#include <uuid/uuid.h>

#include "ZoneAllocator.hpp"

namespace MMD {

class ModuleList {
public:
	struct SegmentInfo {
		char	 segmentName[16];
		uint64_t address;
		uint64_t size;
	};

	using Segments = Vector<SegmentInfo>;

	struct ModuleInfo {
		uintptr_t loadAddress;
		uuid_t	  uuid;
		String	  filePath;
		Segments  segments;
		bool	  executing;

		UniquePtr<char[]> headerAndLoadCommandBytes;

		ModuleInfo ();
		ModuleInfo (uintptr_t			loadAddress,
					const uuid_t*		pUUID,
					const String&		filePath,
					Vector<SegmentInfo> segments,
					bool				executing,
					UniquePtr<char[]>	headerAndLoadCommandBytes);
	};

	using ModuleInfos = Map<uint64_t, ModuleInfo>;

	explicit ModuleList (mach_port_t taskPort);

	bool IsValid () const;

	size_t GetSize () const;

	ModuleInfos::const_iterator begin () const { return m_moduleInfos.begin (); }
	ModuleInfos::const_iterator end () const { return m_moduleInfos.end (); }

	bool GetModuleInfoForAddress (uint64_t address, const ModuleInfo** pModuleInfoOut) const;

	bool MarkAsExecuting (uint64_t codeAddress);

private:
	ModuleInfos m_moduleInfos;

	void Invalidate ();

	bool GetModuleInfoForAddressImpl (uint64_t address, ModuleInfo** pInfoOut);
};

} // namespace MMD

#endif // MMD_MODULELIST
