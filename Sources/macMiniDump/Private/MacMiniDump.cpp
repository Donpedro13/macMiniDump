#include "MacMiniDump.hpp"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>

#include <sys/sysctl.h>
#include <syslog.h>

#include <CoreServices/CoreServices.h>

#include <inttypes.h>
#include <iostream>
#include <memory>
#include <vector>

#include "Defer.hpp"
#include "FileOStream.hpp"
#include "MachOCoreDumpBuilder.hpp"
#include "MachOCoreInternal.hpp"
#include "MemoryRegionList.hpp"
#include "ModuleList.hpp"
#include "ProcessMemoryReaderDataPtr.hpp"
#include "ReadProcessMemory.hpp"
#include "StackWalk.hpp"

namespace MMD {
namespace {

uint64_t GetMemoryProtection (mach_port_t taskPort, uint64_t addr, uint64_t size)
{
	natural_t							  nesting_depth;
	vm_region_submap_short_info_data_64_t info;
	mach_msg_type_number_t				  infoCnt;

	uint64_t	   recurseAddr = addr;
	mach_vm_size_t recurseSize = size;

	::mach_vm_region_recurse (taskPort,
							  &recurseAddr,
							  &recurseSize,
							  &nesting_depth,
							  (vm_region_recurse_info_t) &info,
							  &infoCnt);

	return info.protection;
}

bool AddSegmentCommandFromProcessMemory (mach_port_t		   taskPort,
										 MachOCoreDumpBuilder* pCoreBuilder,
										 MMD::MemoryProtection prot,
										 uint64_t			   startAddress,
										 size_t				   lengthInBytes)
{
	ProcessMemoryReaderDataPtr*	  dataPtr	   = new ProcessMemoryReaderDataPtr (taskPort, startAddress, lengthInBytes);
	std::unique_ptr<DataProvider> dataProvider = std::make_unique<DataProvider> (dataPtr, lengthInBytes);

	return pCoreBuilder->AddSegmentCommand (startAddress, prot, std::move (dataProvider));
}

bool AddSegmentCommandFromProcessMemory (mach_port_t		   taskPort,
										 MachOCoreDumpBuilder* pCoreBuilder,
										 uint64_t			   startAddress,
										 size_t				   lengthInBytes)
{
	MemoryProtection prot = GetMemoryProtection (taskPort, startAddress, lengthInBytes);
	return AddSegmentCommandFromProcessMemory (taskPort, pCoreBuilder, prot, startAddress, lengthInBytes);
}

// This function adds the surrounding piece of memory of address, using the given range. That is, (2 * range) + 1 bytes
// will be incuded, where address will be the "middle point"
void AddSurroundingMemoryToCore (mach_port_t		   taskPort,
								 MachOCoreDumpBuilder* pCoreBuilder,
								 uint64_t			   address,
								 size_t				   range)
{
	assert (address != 0);

	const uint64_t middleAddress = address;
	const uint64_t startAddress	 = middleAddress - range;
	const size_t   length		 = (2 * range) + 1;

	AddSegmentCommandFromProcessMemory (taskPort, pCoreBuilder, startAddress, length);
}

std::vector<char> CreateAllImageInfosPayload (mach_port_t taskPort, uint64_t payloadOffset, const ModuleList& modules)
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

	if (!modules.IsValid ())
		return {};

	const size_t				   nModules = modules.GetSize ();
	MachOCore::AllImageInfosHeader header	= {};
	header.version							= 1;
	header.imgcount							= (uint32_t) (nModules); // Modules are id'd by index in the structure
	header.entries_size						= sizeof (MachOCore::ImageEntry);
	header.entries_fileoff					= payloadOffset + sizeof (MachOCore::AllImageInfosHeader);

	// Prepare segment list of all modules (and while at it, calculate some of the space needed for the whole payload,
	//   this info will be used later)
	size_t											   nSegments	   = 0;
	size_t											   modulePathsSize = 0;
	std::vector<std::vector<MachOCore::SegmentVMAddr>> segmentListList;
	for (const auto& [loadAddr, moduleInfo] : modules) {
		std::vector<MachOCore::SegmentVMAddr> segmentVMAddrs;
		modulePathsSize += moduleInfo.filePath.length () + sizeof '\0';

		std::cout << "Image"
				  << "\n"
				  << "\t" << moduleInfo.filePath << "\n\tLoad address: " << moduleInfo.loadAddress
				  << "\n\tSegment Count: " << moduleInfo.segments.size () << std::endl;

		const ModuleList::Segments& segments = moduleInfo.segments;
		for (const auto& section : segments) {
			MachOCore::SegmentVMAddr newVMAddr = {};
			strncpy (newVMAddr.segname, section.segmentName, sizeof section.segmentName);
			newVMAddr.vmaddr = section.address;

			std::cout << "Segment"
					  << "\n"
					  << "\t" << section.segmentName << "\n\tAddress: " << section.address << std::endl;

			segmentVMAddrs.push_back (newVMAddr);
			++nSegments;
		}

		segmentListList.push_back (segmentVMAddrs);
	}

	// Tally how much space will be needed for the whole payload
	const size_t imageEntriesSize	= nModules * sizeof (MachOCore::ImageEntry);
	const size_t segmentEntriesSize = nSegments * sizeof (MachOCore::SegmentVMAddr);
	const size_t payloadSize =
		sizeof (MachOCore::AllImageInfosHeader) + imageEntriesSize + segmentEntriesSize + modulePathsSize;
	// Allocate payload, then lay out the data
	std::vector<char> result (payloadSize);

	// Header goes first
	size_t offset = 0;
	memcpy (&result[0], &header, sizeof header);
	offset += sizeof header;

	size_t currModulePathOffset	   = payloadOffset + payloadSize - modulePathsSize;
	size_t currSegAddrsOffset	   = currModulePathOffset - segmentEntriesSize;
	size_t currImageEntryMemOffset = offset;
	for (const auto& [loadAddr, moduleInfo] : modules) {
		MachOCore::ImageEntry imageEntry = {};
		imageEntry.filepath_offset		 = currModulePathOffset;
		memcpy (&imageEntry.uuid, &moduleInfo.uuid, sizeof imageEntry.uuid);
		imageEntry.load_address		= moduleInfo.loadAddress;
		imageEntry.seg_addrs_offset = currSegAddrsOffset;
		imageEntry.segment_count	= (uint32_t)moduleInfo.segments.size ();
		imageEntry.reserved			= moduleInfo.executing ? 1 : 0;

		memcpy (&result[currImageEntryMemOffset], &imageEntry, sizeof imageEntry);

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
	for (const auto& [loadAddr, moduleInfo] : modules) {
		strcpy (&result[currModulePathMemOffset], moduleInfo.filePath.c_str ());

		currModulePathMemOffset += moduleInfo.filePath.length () + sizeof '\0';
	}

	return result;
}

bool AddPayloadsAndWrite (mach_port_t				  taskPort,
						  MachOCoreDumpBuilder*		  pCoreBuilder,
						  const ModuleList&			  modules,
						  IRandomAccessBinaryOStream* pOStream)
{
	// Add all load command payloads when needed, calculate data offsets, then write out core dump content

	// Addressable bits of the address space of the process
	uint32_t nAddrabbleBits = 0;
	size_t	 len			= sizeof nAddrabbleBits;
	if ((::sysctlbyname ("machdep.virtual_address_size", &nAddrabbleBits, &len, NULL, 0) != 0) &&
		(::sysctlbyname ("machdep.cpu.address_bits.virtual", &nAddrabbleBits, &len, NULL, 0) != 0))
		return false;

	MachOCore::AddrableBitsInfo abInfo = {};
	abInfo.version					   = 3;
	abInfo.nBits					   = nAddrabbleBits;
	pCoreBuilder->AddDataProviderForNoteCommand (MachOCore::AddrableBitsOwner,
												 std::make_unique<DataProvider> (new CopiedDataPtr (&abInfo,
																									sizeof abInfo),
																				 sizeof abInfo));

	// All image infos
	// The paylod of all image infos is dependent of the size of all load commands, so we have to "finalize" them before
	// creating it
	pCoreBuilder->FinalizeLoadCommands ();

	uint64_t imageInfosPayloadOffset = 0;
	pCoreBuilder->GetOffsetForNoteCommandPayload (MachOCore::AllImageInfosOwner, &imageInfosPayloadOffset);
	std::vector<char> imageInfosPayload = CreateAllImageInfosPayload (taskPort, imageInfosPayloadOffset, modules);
	if (imageInfosPayload.empty ())
		return false;

	pCoreBuilder
		->AddDataProviderForNoteCommand (MachOCore::AllImageInfosOwner,
										 std::make_unique<DataProvider> (new CopiedDataPtr (&imageInfosPayload[0],
																							imageInfosPayload.size ()),
																		 imageInfosPayload.size ()));

	for (size_t i = 0; i < pCoreBuilder->GetNumberOfSegmentCommands (); ++i) {
		segment_command_64* pSegment = pCoreBuilder->GetSegmentCommand (i);
		pCoreBuilder->GetOffsetForSegmentCommandPayload (pSegment->vmaddr, &pSegment->fileoff);
	}

	return pCoreBuilder->Build (pOStream);
}

bool AddThreadsToCore (mach_port_t			 taskPort,
					   MachOCoreDumpBuilder* pCoreBuilder,
					   ModuleList*			 pModules,
					   CrashContext*		 pCrashContext /*= nullptr*/)
{
	thread_act_port_array_t threads;
	mach_msg_type_number_t	nThreads;
	mach_port_t				thisThread = mach_thread_self ();

	if (task_threads (taskPort, &threads, &nThreads) != KERN_SUCCESS)
		return false;

	syslog (LOG_NOTICE, "Enumerating %d threads...", nThreads);

	MemoryRegionList memoryRegions (taskPort);

	// If the task is not suspended, there is a race condition: threads might start and end in the meantime
	// We have to handle this situation (by gracefully handling errors)
	for (int i = 0; i < nThreads; ++i) {
		const bool threadSuspended = threads[i] != thisThread;

		defer {
			if (threadSuspended)
				thread_resume (threads[i]);
		};

#ifdef __x86_64__
		x86_thread_state64_t		ts;
		x86_exception_state64_t		es;
		mach_msg_type_number_t		gprCount  = x86_THREAD_STATE64_COUNT;
		mach_msg_type_number_t		excCount  = x86_EXCEPTION_STATE64_COUNT;
		const thread_state_flavor_t gprFlavor = x86_THREAD_STATE64;
		const thread_state_flavor_t excFlavor = x86_EXCEPTION_STATE64;
#elif defined __arm64__
		arm_thread_state64_t		ts;
		arm_exception_state64_t		es;
		mach_msg_type_number_t		gprCount  = ARM_THREAD_STATE64_COUNT;
		mach_msg_type_number_t		excCount  = ARM_EXCEPTION_STATE64_COUNT;
		const thread_state_flavor_t gprFlavor = ARM_THREAD_STATE64;
		const thread_state_flavor_t excFlavor = ARM_EXCEPTION_STATE64;
#endif

		// If the thread is the crashing one, start stackwalking etc. from the provided crash context
		thread_identifier_info_data_t identifier_info;
		mach_msg_type_number_t identifier_info_count = THREAD_IDENTIFIER_INFO_COUNT;
		uint64_t tid = 0;
		
		if (thread_info(threads[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&identifier_info, &identifier_info_count) == KERN_SUCCESS) {
			tid = identifier_info.thread_id;
		} else {
			syslog (LOG_NOTICE, "Unable to get tid for thread #%d!", i);
		}
		
		if (pCrashContext != nullptr && tid == pCrashContext->crashedTID) {
			syslog (LOG_NOTICE, "Found crashing thread (tid %" PRIu64 " )", tid);

			memcpy (&ts, &pCrashContext->mcontext.__ss, sizeof ts);
			memcpy (&es, &pCrashContext->mcontext.__es, sizeof es);
		} else {
			syslog (LOG_NOTICE, "Adding thread (tid %" PRIu64 " )", tid);

			if (thread_get_state (threads[i], gprFlavor, (thread_state_t) &ts, &gprCount) != KERN_SUCCESS)
				continue;

			if (thread_get_state (threads[i], excFlavor, (thread_state_t) &es, &excCount) != KERN_SUCCESS)
				continue;
		}

		// TODO what's up with this? When a core file of a process that has a debugger attached,
		//   this seems to happen sometimes. Maybe we should just ignore this edge case...
#ifdef __x86_64__
		if (ts.__rip == 0) {
#elif defined __arm64__
		if (ts.__pc == 0) {
#endif
			syslog (LOG_WARNING, "Skipping thread #%d because pc was 0!", i);

			continue;
		}

		MachOCore::GPR gpr;
		gpr.kind	   = MachOCore::RegSetKind::GPR;
		gpr.nWordCount = sizeof ts / sizeof (uint32_t);
		memcpy (&gpr.gpr, &ts, sizeof ts);

		MachOCore::EXC exc;
		exc.kind	   = MachOCore::RegSetKind::EXC;
		exc.nWordCount = sizeof es / sizeof (uint32_t);
		memcpy (&exc.exc, &es, sizeof es);

		pCoreBuilder->AddThreadCommand (gpr, exc);

		MachOCore::GPRPointers pointers (gpr);

		std::vector<uint64_t> callStack =
			WalkStack (taskPort, pointers.InstructionPointer ().AsUInt64 (), pointers.BasePointer ().AsUInt64 ());

		for (const auto ip : callStack) {
			// Add some memory before and after every instruction pointer on the call stack. This is needed for
			// stack walking to work properly when opening the core, as lldb checks the protection of the memory
			// these addresses point to during stack walking. If the memory is not included, it will assume these as
			// non-executable, and simply abort the stackwalk. In addition, we also have the nice benefit of being
			// able to see some disassembly, even if modules are missing. Modified code bytes are a use case, too.

			// FIXME: if an address appears multiple times on a call stack (maybe even on multiple threads), we add
			// duplicate memory
			const size_t SurroundingsRange = 256;
			AddSurroundingMemoryToCore (taskPort, pCoreBuilder, ip, SurroundingsRange);

			// Mark modules as executing if an address corresponding to a module is on a call stack. According to lldb's
			// code, this is used for some kind of symbol loading optimization. Without this, everything still functions
			// as intended, and I could not measure a speed difference, but let's be nice and do it anyway.
			pModules->MarkAsExecuting (ip);
		}

		uint64_t		 sp = pointers.StackPointer ().AsUInt64 ();
		MemoryRegionInfo regionInfo;
		if (!memoryRegions.GetRegionInfoForAddress (sp, &regionInfo)) {
			syslog (LOG_WARNING, "Stack pointer of thread #%d points to invalid memory: %llu", i, sp);

			continue;
		}

		if (regionInfo.type != MemoryRegionType::Stack)
			syslog (LOG_WARNING, "Stack pointer of thread #%d points to non-stack memory: %llu", i, sp);

		const uint64_t stackStart	 = regionInfo.vmaddr + regionInfo.vmsize;
		size_t		   lengthInBytes = stackStart - sp;

		// FIXME: in case of a "self dump", the main thread's stack memory is not included, because it might have
		// changed since the state was captured (above). Should we capture it nonetheless, we would get a garbled call
		// stack. This should be handled with explicitly copying the stack memory of the current thread, as close as
		// possible to the context capture.
		if (threads[i] != thisThread) {
			AddSegmentCommandFromProcessMemory (taskPort, pCoreBuilder, stackStart - lengthInBytes, lengthInBytes);
		}
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

} // namespace

bool MiniDumpWriteDump (mach_port_t taskPort, FILE* pFile, CrashContext* pCrashContext /*= nullptr*/)
{
	assert (pFile != nullptr);

	MMD::FileOStream fos (pFile);

	return MiniDumpWriteDump (taskPort, &fos, pCrashContext);
}

bool MiniDumpWriteDump (mach_port_t taskPort, int fd, CrashContext* pCrashContext /*= nullptr*/)
{
	MMD::FileOStream fos (fd);

	return MiniDumpWriteDump (taskPort, &fos, pCrashContext);
}

bool MiniDumpWriteDump (mach_port_t					taskPort,
						IRandomAccessBinaryOStream* pOStream,
						CrashContext*				pCrashContext /*= nullptr*/)
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

	// FIXME: currently, in case of a self dump, threads are suspended on a one-by-one basis in AddThreadsToCore.
	// For robustness, we should do this instead:
	//   1.) suspend all threads (except this one), and store their id's
	//   2.) create the core file
	//   3.) resume all previously suspended threads

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
	ModuleList			 modules (taskPort);
	if (!AddThreadsToCore (taskPort, &coreBuilder, &modules, pCrashContext))
		return false;

	if (!AddNotesToCore (taskPort, &coreBuilder))
		return false;

	if (!AddPayloadsAndWrite (taskPort, &coreBuilder, modules, pOStream))
		return false;

	return true;
}

} // namespace MMD
