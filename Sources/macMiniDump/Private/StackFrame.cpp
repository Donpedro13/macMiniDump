#include "StackFrame.hpp"

#include <mach-o/compact_unwind_encoding.h>
#include <mach-o/loader.h>

#include <cassert>
#include <cstring>

#include "ReadProcessMemory.hpp"

namespace MMD {
namespace {

#ifdef __x86_64__
// Not supported, and probably never will be
StackFrameLookupResult LookupStackFrameForPCImplIntel (mach_port_t, const ModuleList&, uintptr_t)
{
	return StackFrameLookupResult::Unknown;
}
#elif defined __arm64__
StackFrameLookupResult LookupStackFrameForPCImplArm64 (mach_port_t taskPort, const ModuleList& moduleList, uintptr_t pc)
{
	// Try to use compact unwind info (if present) to see if there is a stack frame
	const ModuleList::ModuleInfo* pModuleInfo = nullptr;

	if (!moduleList.GetModuleInfoForAddress (pc, &pModuleInfo))
		return StackFrameLookupResult::Unknown;

	const mach_header_64* header =
		reinterpret_cast<const mach_header_64*> (pModuleInfo->headerAndLoadCommandBytes.get ());
	const load_command* lc = reinterpret_cast<const load_command*> (header + 1);

	uintptr_t unwindInfoAddr = 0;
	uintptr_t textVmAddr	 = 0;
	bool	  foundText		 = false;

	for (uint32_t i = 0; i < header->ncmds; ++i) {
		if (lc->cmd == LC_SEGMENT_64) {
			const segment_command_64* sc = reinterpret_cast<const segment_command_64*> (lc);
			if (strncmp (sc->segname, "__TEXT", 16) == 0) {
				textVmAddr = sc->vmaddr;
				foundText  = true;
			}

			const section_64* sect = reinterpret_cast<const section_64*> (sc + 1);
			for (uint32_t j = 0; j < sc->nsects; ++j) {
				if (strncmp (sect[j].sectname, "__unwind_info", 16) == 0) {
					unwindInfoAddr = sect[j].addr;
				}
			}
		}
		lc = reinterpret_cast<const load_command*> (reinterpret_cast<const char*> (lc) + lc->cmdsize);
	}

	if (unwindInfoAddr == 0 || !foundText)
		return StackFrameLookupResult::Unknown;

	uintptr_t slide				 = pModuleInfo->loadAddress - textVmAddr;
	uintptr_t unwindInfoLoadAddr = unwindInfoAddr + slide;

	// Reference: https://faultlore.com/blah/compact-unwinding/
	// The compact unwind info format uses a two-level page table. The first level is an index
	// mapping function start addresses to second-level pages. Each second-level page then contains concrete unwind
	// information. A second level page is either a so-called regular page or a compressed page.

	unwind_info_section_header unwindHeader;
	if (!ReadProcessMemoryInto (taskPort, unwindInfoLoadAddr, &unwindHeader))
		return StackFrameLookupResult::Unknown;

	// Calculate offset of index section
	uintptr_t indexSectionAddr = unwindInfoLoadAddr + unwindHeader.indexSectionOffset;
	uint32_t  indexCount	   = unwindHeader.indexCount;

	uint32_t pcOffset = pc - pModuleInfo->loadAddress;

	uint32_t low  = 0;
	uint32_t high = indexCount;

	while (low < high) {
		uint32_t  mid		= low + (high - low) / 2;
		uintptr_t entryAddr = indexSectionAddr + mid * sizeof (unwind_info_section_header_index_entry);

		unwind_info_section_header_index_entry midEntry;
		if (!ReadProcessMemoryInto (taskPort, entryAddr, &midEntry))
			return StackFrameLookupResult::Unknown;

		if (midEntry.functionOffset <= pcOffset) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}

	if (low == 0)
		return StackFrameLookupResult::Unknown;

	uint32_t  targetIndex = low - 1;
	uintptr_t entryAddr	  = indexSectionAddr + targetIndex * sizeof (unwind_info_section_header_index_entry);
	unwind_info_section_header_index_entry entry;
	if (!ReadProcessMemoryInto (taskPort, entryAddr, &entry))
		return StackFrameLookupResult::Unknown;

	if (entry.secondLevelPagesSectionOffset == 0)
		return StackFrameLookupResult::Unknown;

	uintptr_t secondLevelAddr = unwindInfoLoadAddr + entry.secondLevelPagesSectionOffset;

	uint32_t kind;
	if (!ReadProcessMemoryInto (taskPort, secondLevelAddr, &kind))
		return StackFrameLookupResult::Unknown;

	compact_unwind_encoding_t encoding = 0;

	if (kind == UNWIND_SECOND_LEVEL_REGULAR) {
		unwind_info_regular_second_level_page_header pageHeader;
		if (!ReadProcessMemoryInto (taskPort, secondLevelAddr, &pageHeader))
			return StackFrameLookupResult::Unknown;

		uintptr_t entriesAddr	 = secondLevelAddr + sizeof (unwind_info_regular_second_level_page_header);
		uint32_t  pageEntryCount = pageHeader.entryCount;

		uint32_t pLow  = 0;
		uint32_t pHigh = pageEntryCount;

		// See comment above about zero-length entries
		while (pLow < pHigh) {
			uint32_t  pMid		 = pLow + (pHigh - pLow) / 2;
			uintptr_t pEntryAddr = entriesAddr + pMid * sizeof (unwind_info_regular_second_level_entry);

			unwind_info_regular_second_level_entry pEntry;
			if (!ReadProcessMemoryInto (taskPort, pEntryAddr, &pEntry))
				return StackFrameLookupResult::Unknown;

			if (pEntry.functionOffset <= pcOffset) {
				pLow = pMid + 1;
			} else {
				pHigh = pMid;
			}
		}

		if (pLow == 0)
			return StackFrameLookupResult::Unknown;

		uint32_t  pTargetIndex = pLow - 1;
		uintptr_t pEntryAddr   = entriesAddr + pTargetIndex * sizeof (unwind_info_regular_second_level_entry);
		unwind_info_regular_second_level_entry pEntry;
		if (!ReadProcessMemoryInto (taskPort, pEntryAddr, &pEntry))
			return StackFrameLookupResult::Unknown;

		encoding = pEntry.encoding;

	} else if (kind == UNWIND_SECOND_LEVEL_COMPRESSED) {
		unwind_info_compressed_second_level_page_header pageHeader;
		if (!ReadProcessMemoryInto (taskPort, secondLevelAddr, &pageHeader))
			return StackFrameLookupResult::Unknown;

		uintptr_t entriesAddr	 = secondLevelAddr + sizeof (unwind_info_compressed_second_level_page_header);
		uint32_t  pageEntryCount = pageHeader.entryCount;

		uint32_t baseFunctionOffset = entry.functionOffset;

		uint32_t pLow  = 0;
		uint32_t pHigh = pageEntryCount;

		// See comment above about zero-length entries
		while (pLow < pHigh) {
			uint32_t  pMid		 = pLow + (pHigh - pLow) / 2;
			uintptr_t pEntryAddr = entriesAddr + pMid * sizeof (uint32_t);

			uint32_t pEntryVal;
			if (!ReadProcessMemoryInto (taskPort, pEntryAddr, &pEntryVal))
				return StackFrameLookupResult::Unknown;

			uint32_t entryFuncOffset = UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET (pEntryVal);

			if (entryFuncOffset <= (pcOffset - baseFunctionOffset)) {
				pLow = pMid + 1;
			} else {
				pHigh = pMid;
			}
		}

		if (pLow == 0)
			return StackFrameLookupResult::Unknown;

		uint32_t  pTargetIndex = pLow - 1;
		uintptr_t pEntryAddr   = entriesAddr + pTargetIndex * sizeof (uint32_t);
		uint32_t  pEntryVal;
		if (!ReadProcessMemoryInto (taskPort, pEntryAddr, &pEntryVal))
			return StackFrameLookupResult::Unknown;

		uint16_t encodingIndex = UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX (pEntryVal);

		if (encodingIndex < unwindHeader.commonEncodingsArrayCount) {
			uintptr_t commonEncodingsAddr = unwindInfoLoadAddr + unwindHeader.commonEncodingsArraySectionOffset +
											encodingIndex * sizeof (compact_unwind_encoding_t);
			if (!ReadProcessMemoryInto (taskPort, commonEncodingsAddr, &encoding))
				return StackFrameLookupResult::Unknown;
		} else {
			uint16_t  pageEncodingIndex = encodingIndex - unwindHeader.commonEncodingsArrayCount;
			uintptr_t pageEncodingsAddr = secondLevelAddr + pageHeader.encodingsPageOffset +
										  pageEncodingIndex * sizeof (compact_unwind_encoding_t);
			if (!ReadProcessMemoryInto (taskPort, pageEncodingsAddr, &encoding))
				return StackFrameLookupResult::Unknown;
		}
	} else {
		return StackFrameLookupResult::Unknown;
	}

	const compact_unwind_encoding_t mode = encoding & UNWIND_ARM64_MODE_MASK;
	switch (mode) {
		case UNWIND_ARM64_MODE_FRAME:
			return StackFrameLookupResult::HasFrame;
		case UNWIND_ARM64_MODE_FRAMELESS:
			return StackFrameLookupResult::Frameless;
		default:
			return StackFrameLookupResult::Unknown;
	}
}
#else
	#error Unsupported architecture
#endif

} // namespace

StackFrameLookupResult LookupStackFrameForPC (mach_port_t taskPort, const ModuleList& moduleList, uintptr_t pc)
{
#ifdef __x86_64__
	return LookupStackFrameForPCImplIntel (taskPort, moduleList, pc);
#elif defined __arm64__
	return LookupStackFrameForPCImplArm64 (taskPort, moduleList, pc);
#else
	#error Unsupported architecture
#endif
}

} // namespace MMD
