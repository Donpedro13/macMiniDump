#include "ModuleList.hpp"

#include <mach-o/loader.h>
#include <mach/mach.h>

#include <array>
#include <iostream>

#include "ReadProcessMemory.hpp"

namespace MMD {
namespace {

struct dyld_image_info {
	uintptr_t imageLoadAddress;
	uintptr_t imageFilePath;
	uint64_t  imageFileModDate;
};

struct dyld_all_image_infos {
	uint32_t  version;
	uint32_t  infoArrayCount;
	uintptr_t infoArray;
};

std::vector<ModuleList::SegmentInfo> GetSegmentsOfModule (const char* pModuleFirstByte)
{
	const mach_header_64* pHeader = reinterpret_cast<const mach_header_64*> (pModuleFirstByte);

	std::vector<ModuleList::SegmentInfo> result;
	const char*							 pCmdRaw = pModuleFirstByte + sizeof (mach_header_64);
	for (size_t i = 0; i < pHeader->ncmds; ++i) {
		const load_command* pCmd = reinterpret_cast<const load_command*> (pCmdRaw);
		if (pCmd->cmd == LC_SEGMENT_64) {
			const segment_command_64* pSegCmd = reinterpret_cast<const segment_command_64*> (pCmd);
			ModuleList::SegmentInfo	  newInfo = {};
			strncpy (newInfo.segmentName, pSegCmd->segname, sizeof (pSegCmd->segname));
			newInfo.address = pSegCmd->vmaddr;
			newInfo.size	= pSegCmd->vmsize;

			result.push_back (newInfo);

			pCmdRaw += pCmd->cmdsize;
		}
	}

	return result;
}

std::array<char, 16> GetUUIDOfModule (const char* pModuleFirstByte)
{
	std::array<char, 16>  result  = {};
	const mach_header_64* pHeader = reinterpret_cast<const mach_header_64*> (pModuleFirstByte);

	const char* pCmdRaw = pModuleFirstByte + sizeof (mach_header_64);
	for (size_t i = 0; i < pHeader->ncmds; ++i) {
		const load_command* pCmd = reinterpret_cast<const load_command*> (pCmdRaw);
		if (pCmd->cmd == LC_UUID) {
			const uuid_command* pUUIDCommand = reinterpret_cast<const uuid_command*> (pCmd);
			memcpy (&result, pUUIDCommand->uuid, sizeof pUUIDCommand->uuid);

			return result;
		}

		pCmdRaw += pCmd->cmdsize;
	}

	return result;
}

bool GetTextSegmentOfModule (const ModuleList::ModuleInfo& moduleInfo, ModuleList::SegmentInfo* pSegInfoOut)
{
	for (const auto& seg : moduleInfo.segments) {
		if (strcmp (seg.segmentName, "__TEXT") == 0) {
			memcpy (pSegInfoOut, &seg, sizeof seg);

			return true;
		}
	}

	return false;
}

} // namespace

ModuleList::ModuleInfo::ModuleInfo () = default;

ModuleList::ModuleInfo::ModuleInfo (uintptr_t				 loadAddress,
									const uuid_t*			 pUUID,
									const std::string&		 filePath,
									std::vector<SegmentInfo> segments,
									bool					 executing,
									std::unique_ptr<char[]>	 headerAndLoadCommandBytes):
	loadAddress (loadAddress),
	uuid (),
	filePath (filePath),
	segments (segments),
	headerAndLoadCommandBytes (headerAndLoadCommandBytes.release ())
{
	memcpy (&uuid, pUUID, sizeof uuid);
}

ModuleList::ModuleList (mach_port_t taskPort)
{
	task_dyld_info_data_t  task_dyld_info;
	mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

	if (task_info (taskPort, TASK_DYLD_INFO, (task_info_t) &task_dyld_info, &count) != KERN_SUCCESS)
		return;

	const uintptr_t dyldInfoAddress = task_dyld_info.all_image_info_addr;

	std::unique_ptr<char[]> dyldInfoBytes =
		ReadProcessMemory (taskPort, dyldInfoAddress, sizeof (dyld_all_image_infos));
	if (dyldInfoBytes == nullptr)
		return;

	dyld_all_image_infos* pImageInfo = reinterpret_cast<dyld_all_image_infos*> (dyldInfoBytes.get ());

	std::unique_ptr<char[]> dyldInfosBytes =
		ReadProcessMemory (taskPort, pImageInfo->infoArray, pImageInfo->infoArrayCount * sizeof (dyld_image_info));
	if (dyldInfosBytes == nullptr)
		return;

	dyld_image_info* pImageInfoArray = reinterpret_cast<dyld_image_info*> (dyldInfosBytes.get ());
	for (uint32_t i = 0; i < pImageInfo->infoArrayCount; ++i) {
		std::string imagePath;
		if (!ReadProcessMemoryString (taskPort, pImageInfoArray[i].imageFilePath, 4096, &imagePath)) {
			Invalidate ();

			return;
		} else {
			std::unique_ptr<char[]> pHeaderRawBytes =
				ReadProcessMemory (taskPort, pImageInfoArray[i].imageLoadAddress, sizeof (mach_header_64));
			if (pHeaderRawBytes == nullptr) {
				Invalidate ();

				return;
			}

			mach_header_64*			pHeader	  = reinterpret_cast<mach_header_64*> (pHeaderRawBytes.get ());
			std::unique_ptr<char[]> pRawBytes = ReadProcessMemory (taskPort,
																   pImageInfoArray[i].imageLoadAddress,
																   sizeof (mach_header_64) + pHeader->sizeofcmds);
			if (pRawBytes == nullptr) {
				Invalidate ();

				return;
			}

			uint64_t							 loadAddress = pImageInfoArray[i].imageLoadAddress;
			std::vector<ModuleList::SegmentInfo> segments	 = GetSegmentsOfModule (pRawBytes.get ());
			for (auto& si : segments) {
				// HACK
				if (strcmp (si.segmentName, "__TEXT") == 0) {
					si.address = pImageInfoArray[i].imageLoadAddress;

					break;
				}
			}

			std::array<char, 16> rawUUID = GetUUIDOfModule (pRawBytes.get ());
			uuid_t				 uuid	 = {};
			memcpy (&uuid, &rawUUID, sizeof uuid);
			std::pair<uint64_t, ModuleInfo> p (loadAddress,
											   ModuleInfo (loadAddress,
														   &uuid,
														   imagePath,
														   segments,
														   false, // Assume it's not executing, for now
														   std::move (pRawBytes)));
			m_moduleInfos.emplace (std::move (p));
		}
	}
}

bool ModuleList::IsValid () const
{
	return !m_moduleInfos.empty ();
}

size_t ModuleList::GetSize () const
{
	return m_moduleInfos.size ();
}

bool ModuleList::GetModuleInfoForAddress (uint64_t address, ModuleInfo** pInfoOut)
{
	if (m_moduleInfos.empty ())
		return false;

	auto isCodeInModule = [] (uint64_t addr, const ModuleInfo& mi) {
		ModuleList::SegmentInfo si;
		if (!GetTextSegmentOfModule (mi, &si))
			return false;

		return addr >= si.address && addr <= si.address + si.size;
	};

	// Get the element that is greater or equal to the address
	auto it = m_moduleInfos.lower_bound (address);

	// Either the address is unknown, or it's "contained" in the last entry
	if (it == m_moduleInfos.end ()) {
		if (isCodeInModule (address, m_moduleInfos.rbegin ()->second)) {
			*pInfoOut = &m_moduleInfos.rbegin ()->second;

			return true;
		} else {
			return false;
		}
	}

	// If we got the first element, either the address is unknown, or it's contained in the first entry
	// If the address is right at the start of a region, no need to check the previous one
	// Else, the address is either contained in the previous region, or is unknown
	if (it != m_moduleInfos.begin () && address != it->first)
		--it;

	if (isCodeInModule (address, it->second)) {
		*pInfoOut = &it->second;

		return true;
	} else {
		return false;
	}
}

bool ModuleList::MarkAsExecuting (uint64_t codeAddress)
{
	ModuleInfo* pModuleInfo = nullptr;

	if (!GetModuleInfoForAddress (codeAddress, &pModuleInfo))
		return false;

	pModuleInfo->executing = true;

	return true;
}

void ModuleList::Invalidate ()
{
	m_moduleInfos.clear ();
}

} // namespace MMD
