#include "MacMiniDump.hpp"

#include <mach/mach.h>
#include <sys/sysctl.h>

#include <functional>
#include <memory>
#include <vector>

#include "Defer.hpp"
#include "FileOStream.hpp"
#include "MachOCoreDumpBuilder.hpp"
#include "MachOCoreInternal.hpp"
#include "ModuleList.hpp"
#include "MemoryRegionList.hpp"
#include "ReadProcessMemory.hpp"

namespace MMD {
namespace {

class ProcessMemoryReaderDataPtr : public IDataPtr {
public:
	ProcessMemoryReaderDataPtr (mach_port_t taskPort, vm_address_t startAddress, vm_size_t maxSize);
	
	virtual const char* Get (size_t offset, size_t size) override;
	virtual const char* Get () override;
	
private:
	mach_port_t	 m_taskPort;
	vm_address_t m_startAddress;
	vm_size_t	 m_maxSize;
	
	std::unique_ptr<char[]> m_currentCopy;
};

ProcessMemoryReaderDataPtr::ProcessMemoryReaderDataPtr (mach_port_t taskPort, vm_address_t startAddress, vm_size_t size):
	m_taskPort (taskPort),
	m_startAddress (startAddress),
	m_maxSize (size)
{
}

const char* ProcessMemoryReaderDataPtr::Get (size_t offset, size_t size)
{
	if (offset + size > m_maxSize)
		return nullptr;
	
	m_currentCopy = ReadProcessMemory (m_taskPort, m_startAddress + offset, size);
	
	return m_currentCopy.get ();
}

const char* ProcessMemoryReaderDataPtr::Get ()
{
	return nullptr;	// Not a good idea...
}

std::vector<char> CreateAllImageInfosPayload (mach_port_t taskPort, uint64_t payloadOffset)
{
	// The structure of this payload is the following:
	/*
							┌─────────────────┐ <- payloadOffset
							│     Header      │
							│                 │
							│                 │
							│                 │
							│                 │
							│                 │
							│                 │
							├─────────────────┤}
							│  Image entry 1  │ |
							└─────────────────┘ |
								   ...           > imageEntriesSize
							┌─────────────────┐ |
							│  Image entry N  │ |
							├─────────────────┤}
							│Segment VMAddr 1 │ |
							└─────────────────┘ |
								   ...           > segmentEntriesSize
							┌─────────────────┐ |
							│Segment VMAddr M │ |
							├─────────────────┤}
							│  Module path 1  │ |
							└─────────────────┘ |
								   ...           > modulePathsSize
							┌─────────────────┐ |
							│  Module path N  │ |
							└─────────────────┘} <- payloadOffset + payloadSize
	 */
	
	// Even though this looks pretty straightforward, it's not, because many (sub)structures are
	//   referring to each other (offsets, sizes, etc.). This is why structures are not produced in order,
	//   and why the code is so complex
	
	ModuleList modules (taskPort);
	if (!modules.IsValid ())
		return {};
	
	const size_t nModules = modules.GetSize ();
	MachOCore::AllImageInfosHeader header = {};
	header.version = 1;
	header.imgcount = nModules;	// Modules are id'd by index in the structure
	header.entries_size = sizeof (MachOCore::ImageEntry);
	header.entries_fileoff = payloadOffset + sizeof (MachOCore::AllImageInfosHeader);
	
	// Prepare segment list of all modules (and while at it, calculate some of the space needed for the whole payload,
	//   this info will be used later)
	size_t nSegments = 0;
	size_t modulePathsSize = 0;
	std::vector<std::vector<MachOCore::SegmentVMAddr>> segmentListList;
	for (size_t i = 0; i < nModules; ++i) {
		std::vector<MachOCore::SegmentVMAddr> segmentVMAddrs;
		const ModuleList::ModuleInfo& moduleInfo = modules.GetModuleInfo (i);
		modulePathsSize += moduleInfo.filePath.length () + sizeof '\0';
		
		std::cout << "Image" << "\n" << "\t" << moduleInfo.filePath << "\n\tLoad address: " << moduleInfo.loadAddress << "\n\tSegment Count: " << moduleInfo.segments.size () << std::endl;
		
		const ModuleList::Segments& segments = moduleInfo.segments;
		for (const auto& section : segments) {
			MachOCore::SegmentVMAddr newVMAddr = {};
			strncpy (newVMAddr.segname, section.segmentName, sizeof section.segmentName);
			newVMAddr.vmaddr = section.address;
			
			std::cout << "Segment" << "\n" << "\t" << section.segmentName << "\n\tAddress: " << section.address << std::endl;
			
			segmentVMAddrs.push_back (newVMAddr);
			++nSegments;
		}
		
		segmentListList.push_back(segmentVMAddrs);
	}
	
	// Tally how much space will be needed for the whole payload
	const size_t imageEntriesSize = nModules * sizeof (MachOCore::ImageEntry);
	const size_t segmentEntriesSize = nSegments * sizeof (MachOCore::SegmentVMAddr);
	const size_t payloadSize = sizeof (MachOCore::AllImageInfosHeader) +
							   imageEntriesSize +
							   segmentEntriesSize +
							   modulePathsSize;
	// Allocate payload, then lay out the data
	std::vector<char> result (payloadSize);
	
	// Header goes first
	size_t offset = 0;
	memcpy (&result[0], &header, sizeof header);
	offset += sizeof header;
	
	size_t currModulePathOffset = payloadOffset + payloadSize - modulePathsSize;
	size_t currSegAddrsOffset = currModulePathOffset - segmentEntriesSize;
	size_t currImageEntryMemOffset = offset;
	for (size_t i = 0; i < nModules; ++i) {
		const ModuleList::ModuleInfo& moduleInfo = modules.GetModuleInfo (i);
		
		MachOCore::ImageEntry imageEntry = {};
		imageEntry.filepath_offset = currModulePathOffset;
		memcpy (&imageEntry.uuid, &moduleInfo.uuid, sizeof imageEntry.uuid);
		imageEntry.load_address = moduleInfo.loadAddress;
		imageEntry.seg_addrs_offset = currSegAddrsOffset;
		imageEntry.segment_count = moduleInfo.segments.size ();
		imageEntry.reserved = 1;	// TODO this should only be set to 1 for modules that are currently executing
		
		memcpy(&result[currImageEntryMemOffset], &imageEntry, sizeof imageEntry);
		
		currModulePathOffset += moduleInfo.filePath.size () + sizeof '\0';
		currSegAddrsOffset += imageEntry.segment_count * sizeof (MachOCore::SegmentVMAddr);
		currImageEntryMemOffset += sizeof (MachOCore::ImageEntry);
	}
	
	// Then segment vmaddr arrays
	size_t currSegAddrMemOffset = sizeof (MachOCore::AllImageInfosHeader) + imageEntriesSize;
	for (const auto& sl : segmentListList) {
		for (const auto& segment : sl) {
			memcpy (&result[currSegAddrMemOffset], &segment, sizeof segment);
			
			currSegAddrMemOffset += sizeof (MachOCore::SegmentVMAddr);
		}
	}
	
	// And finally, module path (zero-terminated) strings
	size_t currModulePathMemOffset = payloadSize - modulePathsSize;
	for (size_t i = 0; i < nModules; ++i) {
		const ModuleList::ModuleInfo& moduleInfo = modules.GetModuleInfo (i);
		strcpy (&result[currModulePathMemOffset], moduleInfo.filePath.c_str ());
		
		currModulePathMemOffset += moduleInfo.filePath.length () + sizeof '\0';
	}
	
	return result;
}

bool AddPayloadsAndWrite (mach_port_t taskPort, MachOCoreDumpBuilder* pCoreBuilder, IRandomAccessBinaryOStream* pOStream)
{
	// Add all load command payloads when needed, calculate data offsets, then write out core dump content
	
	// Addressable bits of the address space of the process
	uint32_t nAddrabbleBits = 0;
	size_t len = sizeof nAddrabbleBits;
	  if (  (::sysctlbyname ("machdep.virtual_address_size",     &nAddrabbleBits, &len, NULL, 0) != 0)
	  	 && (::sysctlbyname ("machdep.cpu.address_bits.virtual", &nAddrabbleBits, &len, NULL, 0) != 0) )
		  return false;
	
	MachOCore::AddrableBitsInfo abInfo = {};
	abInfo.version = 3;
	abInfo.nBits = nAddrabbleBits;
	pCoreBuilder->AddDataProviderForNoteCommand (MachOCore::AddrableBitsOwner,
												std::make_unique<DataProvider> (new CopiedDataPtr (&abInfo, sizeof abInfo), sizeof abInfo));
	
	// All image infos
	// The paylod of all image infos is dependent of the size of all load commands, so we have to "finalize" them before creating it
	pCoreBuilder->FinalizeLoadCommands ();
	
	uint64_t imageInfosPayloadOffset = 0;
	pCoreBuilder->GetOffsetForNoteCommandPayload (MachOCore::AllImageInfosOwner, &imageInfosPayloadOffset);
	std::vector<char> imageInfosPayload = CreateAllImageInfosPayload(taskPort, imageInfosPayloadOffset);
	if (imageInfosPayload.empty ())
		return false;
	
	pCoreBuilder->AddDataProviderForNoteCommand(MachOCore::AllImageInfosOwner,
												std::make_unique<DataProvider> (new CopiedDataPtr (&imageInfosPayload[0], imageInfosPayload.size ()),
																				imageInfosPayload.size ()));
	
	
	for (size_t i = 0; i < pCoreBuilder->GetNumberOfSegmentCommands(); ++i) {
		segment_command_64* pSegment = pCoreBuilder->GetSegmentCommand(i);
		pCoreBuilder->GetOffsetForSegmentCommandPayload (pSegment->vmaddr, &pSegment->fileoff);
	}
	
	return pCoreBuilder->Build (pOStream);
}

bool AddThreadsToCore (mach_port_t taskPort, MachOCoreDumpBuilder* pCoreBuilder)
{
	thread_act_port_array_t threads;
	mach_msg_type_number_t nThreads;
	mach_port_t thisThread = mach_thread_self ();
	
	if (task_threads (taskPort, &threads, &nThreads) != KERN_SUCCESS)
		return false;
	
	// If the task is not suspended, there is a race condition: threads might start and end in the meantime
	// We have to handle this situation (by gracefully handling errors)
	for (size_t i = 0; i < nThreads; ++i) {
		const bool suspend = threads[i] != thisThread;
		if (suspend) {
			if (thread_suspend (threads[i]) != KERN_SUCCESS)
				continue;
		}
		
		defer {
			if (suspend)
				thread_resume (threads[i]);
		};
		
#ifdef __x86_64__
		x86_thread_state64_t ts;
		x86_exception_state64_t es;
		mach_msg_type_number_t gprCount = x86_THREAD_STATE64_COUNT;
		mach_msg_type_number_t excCount = x86_EXCEPTION_STATE64_COUNT;
		const thread_state_flavor_t gprFlavor = x86_THREAD_STATE64;
		const thread_state_flavor_t excFlavor = x86_EXCEPTION_STATE64;
#elif defined __arm64__
		arm_thread_state64_t ts;
		arm_exception_state64_t es;
		mach_msg_type_number_t gprCount = ARM_THREAD_STATE64_COUNT;
		mach_msg_type_number_t excCount = ARM_EXCEPTION_STATE64_COUNT;
		const thread_state_flavor_t gprFlavor = ARM_THREAD_STATE64;
		const thread_state_flavor_t excFlavor = ARM_EXCEPTION_STATE64;
#endif
		
		if (thread_get_state (threads[i], gprFlavor, (thread_state_t)&ts, &gprCount) != KERN_SUCCESS)
			continue;

		if (thread_get_state (threads[i], excFlavor, (thread_state_t)&es, &excCount) != KERN_SUCCESS)
			continue;

		MachOCore::GPR gpr;
		gpr.kind = MachOCore::RegSetKind::GPR;
		gpr.nWordCount = sizeof ts / sizeof (uint32_t);
		memcpy(&gpr.gpr, &ts, sizeof ts);
		
		MachOCore::EXC exc;
		exc.kind = MachOCore::RegSetKind::EXC;
		exc.nWordCount = sizeof es / sizeof (uint32_t);
		memcpy(&exc.exc, &es, sizeof es);
		
		pCoreBuilder->AddThreadCommand (gpr, exc);
	}
	
	return true;
}

bool AddNotesToCore (mach_port_t taskPort, MachOCoreDumpBuilder* pCoreBuilder)
{
	// Payloads for these will be added later
	pCoreBuilder->AddNoteCommand (MachOCore::AddrableBitsOwner);
	pCoreBuilder->AddNoteCommand (MachOCore::AllImageInfosOwner);
	
	return true;
}

bool AddSegmentsToCore (mach_port_t taskPort, MachOCoreDumpBuilder* pCoreBuilder)
{
	MemoryRegionList regionList (taskPort);
	
	for (size_t i = 0; i < regionList.GetSize (); ++i) {
		const MemoryRegionInfo& regionInfo = regionList.GetMemoryRegionInfo (i);
		
		if (regionInfo.type != MemoryRegionType::Stack)
			continue;
		
		if (!pCoreBuilder->AddSegmentCommand (regionInfo.vmaddr,
											  regionInfo.prot,
											  std::make_unique<DataProvider> (new ProcessMemoryReaderDataPtr (taskPort,
																											  regionInfo.vmaddr,
																											  regionInfo.vmsize),
																			  regionInfo.vmsize)))
		{
			return false;
		}
	}
	
	// TODO remove
	/*ModuleList modules (taskPort);
	if (!modules.IsValid ())
		return false;
	
	std::vector<std::pair<uint64_t, uint64_t>> relevantRanges;
	for (size_t i = 0; i < modules.GetSize (); ++i) {
		const ModuleList::ModuleInfo& moduleInfo = modules.GetModuleInfo (i);
		const char* prefix = "/usr/lib";
		if (strncmp (prefix, moduleInfo.filePath.c_str (), strlen (prefix)) != 0)
			continue;
		
		if (strstr (moduleInfo.filePath.c_str (), "libsystem_c.dylib") == 0)
			continue;

		//relevantRanges.push_back({moduleInfo.loadAddress, moduleInfo.);
		
		for (const auto& segmentInfo : moduleInfo.segments) {
			if (std::string (segmentInfo.segmentName) == "__TEXT") {
				relevantRanges.push_back({segmentInfo.address, segmentInfo.size });
				
				if (ReadProcessMemory(taskPort, segmentInfo.address, 1) == nullptr)
					printf ("Text section of module %s at address 0x%llx seems to be invalid!\n", moduleInfo.filePath.c_str (), segmentInfo.address);
			}
			
		}
	}
	
	size_t writtenTotal = 0;
	MemoryRegionList regions (taskPort);
	std::vector<uint64_t> addedAddresses;
	for (size_t i = 0; i < regions.GetSize (); ++i) {
		const MemoryRegionInfo& regionInfo = regions.GetMemoryRegionInfo (i);
		
		for (const auto& range : relevantRanges) {
			if (range.first >= regionInfo.vmaddr && range.first < regionInfo.vmaddr + regionInfo.vmsize) {
				addedAddresses.push_back (range.first);
				writtenTotal += range.second;
				
				printf ("Added %llu bytes\n", range.second);
				
				if (!pCoreBuilder->AddSegmentCommand (range.first,
													  regionInfo.prot,
													  std::make_unique<DataProvider> (new ProcessMemoryReaderDataPtr (taskPort,
																													  range.first,
																													  range.second),
																					  range.second)))
				{
					return false;
				}
			}
		}
	}
	
	//if (addedAddresses.size() != relevantAddresses.size())
	//	return false;
	
	printf ("Total written: %zu\n", writtenTotal);*/	
	
	return true;
}

}	// namespace

bool MiniDumpWriteDump (mach_port_t taskPort, FILE* pFile)
{
	assert (pFile != nullptr);
	
	MMD::FileOStream fos (pFile);
	
	return MiniDumpWriteDump (taskPort, &fos);
}

bool MiniDumpWriteDump (mach_port_t taskPort, int fd)
{
	MMD::FileOStream fos (fd);
	
	return MiniDumpWriteDump (taskPort, &fos);
}

bool MiniDumpWriteDump (mach_port_t taskPort, IRandomAccessBinaryOStream* pOStream)
{
	assert (pOStream != nullptr);
	
	// Is the passed port valid, and of a task?
	int pid;
	if (pid_for_task (taskPort, &pid) != KERN_SUCCESS)
		return false;
	
	if (!pOStream->SetSize (0))
		return false;
	
	// If we are writing a core dump of another process, we need to suspend the task
	const bool selfDump = taskPort == mach_task_self ();
	if (!selfDump) {
		if (task_suspend (taskPort) != KERN_SUCCESS)
			return false;
	}
	
	defer {
		if (!selfDump)
			task_resume (taskPort);
	};
	
	// Core files have peculiar structure:
	//  * Offsets are included in many payloads (so they are *not* self-contained)
	//  * The payload part comes after the header and load command part, which is at the very beginning
	//
	// The practical implication of this is that we have to prepare everything in advance:
	//  * we have to first decide what to put inside the core dump (add load commands)
	//  * we have to know the size of all payloads
	//  * we need to update offset fields in the load commands, and payloads
	//  * then finally, we can write out the content itself
	MachOCoreDumpBuilder coreBuilder;
	if (!AddThreadsToCore (taskPort, &coreBuilder))
		return false;
	
	if (!AddNotesToCore (taskPort, &coreBuilder))
		return false;
	
	if (!AddSegmentsToCore (taskPort, &coreBuilder))
		return false;
	
	if (!AddPayloadsAndWrite (taskPort, &coreBuilder, pOStream))
		return false;
	
	return true;
}

}	// namespace MMD
