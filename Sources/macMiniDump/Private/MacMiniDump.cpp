#include "MMD/MacMiniDump.hpp"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>

#include <sys/sysctl.h>

#include <CoreServices/CoreServices.h>

#include <cinttypes>
#include <map>
#include <memory>
#include <vector>

#include "MMD/FileOStream.hpp"

#include "Defer.hpp"
#include "Logging.hpp"
#include "MachOCoreDumpBuilder.hpp"
#include "MachOCoreInternal.hpp"
#include "MachPortSendRightRef.hpp"
#include "MemoryRegionList.hpp"
#include "ModuleList.hpp"
#include "ProcessMemoryReaderDataPtr.hpp"
#include "ReadProcessMemory.hpp"
#include "StackWalk.hpp"

namespace MMD {
namespace {

class DisjointIntervalSet {
public:
	// Insert interval [start, start + length). Overlapping intervals are merged.
	void InsertAndMergeIfNeeded (uint64_t start, uint64_t length)
	{
		if (length == 0)
			return;

		uint64_t end = start + length;

		// Find the first interval that could overlap (starts before or at our end)
		auto it = m_intervals.upper_bound (start);
		if (it != m_intervals.begin ())
			--it;

		// Merge with all overlapping or adjacent intervals
		while (it != m_intervals.end () && it->first <= end) {
			if (it->second >= start) {
				// Intervals overlap or are adjacent - merge them
				start = std::min (start, it->first);
				end	  = std::max (end, it->second);
				it	  = m_intervals.erase (it);
			} else {
				++it;
			}
		}

		m_intervals[start] = end;
	}

	// Iterate over all merged intervals as (start, length) pairs
	template<typename Func>
	void ForEach (Func&& func) const
	{
		for (const auto& [start, end] : m_intervals) {
			func (start, end - start);
		}
	}

private:
	std::map<uint64_t, uint64_t> m_intervals; // start -> end
};

bool GetMemoryProtection (mach_port_t taskPort, uint64_t addr, uint64_t size, MemoryProtection* pProtOut)
{
	natural_t							  nesting_depth = 0;
	vm_region_submap_short_info_data_64_t info;
	mach_msg_type_number_t				  infoCnt = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;

	uint64_t	   recurseAddr = addr;
	mach_vm_size_t recurseSize = size;

	if (::mach_vm_region_recurse (taskPort,
								  &recurseAddr,
								  &recurseSize,
								  &nesting_depth,
								  (vm_region_recurse_info_t) &info,
								  &infoCnt) != KERN_SUCCESS) {
		return false;
	} else {
		*pProtOut = info.protection;
		return true;
	}
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
	MemoryProtection prot;
	if (!GetMemoryProtection (taskPort, startAddress, lengthInBytes, &prot))
		return false;

	return AddSegmentCommandFromProcessMemory (taskPort, pCoreBuilder, prot, startAddress, lengthInBytes);
}

std::vector<char> CreateAllImageInfosPayload (uint64_t payloadOffset, const ModuleList& modules)
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

		MMD_DEBUGLOG_LINE << "\nImage"
						  << "\n"
						  << "\t" << moduleInfo.filePath << "\n\tLoad address: " << moduleInfo.loadAddress
						  << "\n\tSegment Count: " << moduleInfo.segments.size ();

		const ModuleList::Segments& segments = moduleInfo.segments;
		for (const auto& section : segments) {
			MachOCore::SegmentVMAddr newVMAddr = {};
			strncpy (newVMAddr.segname, section.segmentName, sizeof section.segmentName);
			newVMAddr.vmaddr = section.address;

			MMD_DEBUGLOG_LINE << "\nSegment"
							  << "\n"
							  << "\t" << section.segmentName << "\n\tAddress: " << section.address;

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
		imageEntry.segment_count	= (uint32_t) moduleInfo.segments.size ();
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

bool AddPayloadsAndWrite (MachOCoreDumpBuilder*		  pCoreBuilder,
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
	// The payload of all image infos is dependent of the size of all load commands, so we have to "finalize" them
	// before creating it
	pCoreBuilder->FinalizeLoadCommands ();

	uint64_t imageInfosPayloadOffset = 0;
	pCoreBuilder->GetOffsetForNoteCommandPayload (MachOCore::AllImageInfosOwner, &imageInfosPayloadOffset);
	std::vector<char> imageInfosPayload = CreateAllImageInfosPayload (imageInfosPayloadOffset, modules);
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

bool SuspendAllThreadsExceptCurrentOne (mach_port_t taskPort, std::vector<MachPortSendRightRef>* pSuspendedThreadsOut)
{
	std::vector<MachPortSendRightRef> suspendedThreads;
	thread_act_port_array_t			  threads;
	mach_msg_type_number_t			  nThreads;
	MachPortSendRightRef			  thisThreadRef = MachPortSendRightRef::Wrap (mach_thread_self ());

	if (task_threads (taskPort, &threads, &nThreads) != KERN_SUCCESS)
		return false;

	std::vector<MachPortSendRightRef> threadRefs;
	threadRefs.reserve (nThreads);
	for (unsigned int i = 0; i < nThreads; ++i) {
		threadRefs.push_back (MachPortSendRightRef::Wrap (threads[i]));
	}

	for (unsigned int i = 0; i < nThreads; ++i) {
		MachPortSendRightRef& threadRef = threadRefs[i];

		if (threadRef.Get () == thisThreadRef.Get ()) {
			continue;
		}

		if (thread_suspend (threadRef.Get ()) == KERN_SUCCESS) {
			suspendedThreads.push_back (std::move (threadRef));
		} else {
			// Threads might start and end after calling task_threads, so we handle failures here gracefully
			MMD_DEBUGLOG_LINE << "Failed to suspend thread #" << i << " port " << threadRef.Get ();
		}
	}

	vm_deallocate (mach_task_self (), (vm_address_t) threads, nThreads * sizeof (thread_act_t));

	MMD_DEBUGLOG_LINE << "Suspended " << suspendedThreads.size () << " threads for self-dump";

	*pSuspendedThreadsOut = std::move (suspendedThreads);

	return true;
}

void ResumeThreads (const std::vector<MachPortSendRightRef>& threads)
{
	for (const MachPortSendRightRef& threadRef : threads) {
		if (thread_resume (threadRef.Get ()) != KERN_SUCCESS)
			MMD_DEBUGLOG_LINE << "Failed to resume thread port " << threadRef.Get ();
	}
}

bool AddThreadsToCore (mach_port_t			 taskPort,
					   MachOCoreDumpBuilder* pCoreBuilder,
					   ModuleList*			 pModules,
					   MMDCrashContext*		 pCrashContext /*= nullptr*/)
{
	thread_act_port_array_t threads;
	mach_msg_type_number_t	nThreads;
	MachPortSendRightRef	thisThreadRef = MachPortSendRightRef::Wrap (mach_thread_self ());

	if (task_threads (taskPort, &threads, &nThreads) != KERN_SUCCESS)
		return false;

	std::vector<MachPortSendRightRef> threadRefs;
	threadRefs.reserve (nThreads);
	for (unsigned int i = 0; i < nThreads; ++i)
		threadRefs.push_back (MachPortSendRightRef::Wrap (threads[i]));

	MMD_DEBUGLOG_LINE << "Enumerating " << nThreads << " threads...";

	MemoryRegionList memoryRegions (taskPort);

	// Collect all memory ranges to add, then merge overlapping ones before adding to core
	DisjointIntervalSet memoryRangesToAdd;

	for (unsigned int i = 0; i < nThreads; ++i) {
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
		mach_msg_type_number_t		  identifier_info_count = THREAD_IDENTIFIER_INFO_COUNT;
		uint64_t					  tid					= 0;

		if (thread_info (threadRefs[i].Get (),
						 THREAD_IDENTIFIER_INFO,
						 (thread_info_t) &identifier_info,
						 &identifier_info_count) == KERN_SUCCESS) {
			tid = identifier_info.thread_id;
		} else {
			MMD_DEBUGLOG_LINE << "Unable to get tid for thread #" << i << "!";
		}

		if (pCrashContext != nullptr && tid == pCrashContext->crashedTID) {
			MMD_DEBUGLOG_LINE << "Found crashing thread (tid " << tid << " )";

			memcpy (&ts, &pCrashContext->mcontext.__ss, sizeof ts);
			memcpy (&es, &pCrashContext->mcontext.__es, sizeof es);
		} else {
			MMD_DEBUGLOG_LINE << "Adding thread (tid " << tid << " )";

			if (thread_get_state (threadRefs[i].Get (), gprFlavor, (thread_state_t) &ts, &gprCount) != KERN_SUCCESS)
				continue;

			if (thread_get_state (threadRefs[i].Get (), excFlavor, (thread_state_t) &es, &excCount) != KERN_SUCCESS)
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

		std::vector<uint64_t> callStack = WalkStack (taskPort, memoryRegions, *pModules, gpr, exc);

		for (const auto ip : callStack) {
			// Add some memory before and after every instruction pointer on the call stack. This is needed for
			// stack walking to work properly when opening the core, as LLDB checks the protection of the memory
			// these addresses point to during stack walking. This is crucial for modules which are not available when
			// opening the core file (frequent case: system libraries). If the memory is not included, it will assume
			// these as non-executable, and simply abort the stackwalk. In addition, we also have the nice benefit of
			// being able to see some disassembly, even if modules are missing. Modified code bytes are a use case, too.

			const size_t SurroundingsRange = 256;
			// Make sure we do not under- or overflow (e.g. nullptr, or a very large address)
			if (ip >= SurroundingsRange && ip <= UINT64_MAX - SurroundingsRange) {
				const uint64_t start  = ip - SurroundingsRange;
				const size_t   length = (2 * SurroundingsRange) + 1;
				memoryRangesToAdd.InsertAndMergeIfNeeded (start, length);
			} else {
				MMD_DEBUGLOG_LINE << "Skipping address " << ip << " on thread #" << i << " because it is out of range!";
			}

			// Mark modules as executing if an address corresponding to a module is on a call stack. According to lldb's
			// code, this is used for some kind of symbol loading optimization. Without this, everything still functions
			// as intended, and I could not measure a speed difference, but let's be nice and do it anyway.
			pModules->MarkAsExecuting (ip);
		}

		uintptr_t		 sp = pointers.StackPointer ().AsUIntPtr ();
		MemoryRegionInfo regionInfo;
		if (!memoryRegions.GetRegionInfoForAddress (sp, &regionInfo)) {
			MMD_DEBUGLOG_LINE << "Stack pointer of thread #" << i << " points to invalid memory: " << sp;

			continue;
		}

		if (regionInfo.type != MemoryRegionType::Stack)
			MMD_DEBUGLOG_LINE << "Stack pointer of thread #" << i << " points to non-stack memory: " << sp;

		const uintptr_t stackStart	  = regionInfo.vmaddr + regionInfo.vmsize;
		size_t			lengthInBytes = stackStart - sp;

		// FIXME: in case of a "self dump", the main thread's stack memory is not included, because it might have
		// changed since the state was captured (above). Should we capture it nonetheless, we would get a garbled call
		// stack. This should be handled with explicitly copying the stack memory of the current thread, as close as
		// possible to the context capture.
		if (threadRefs[i].Get () != thisThreadRef.Get () || pCrashContext != nullptr) {
			const uint64_t stackSegmentStart = stackStart - lengthInBytes;
			memoryRangesToAdd.InsertAndMergeIfNeeded (stackSegmentStart, lengthInBytes);
		}
	}

	// Add all merged memory ranges to core
	memoryRangesToAdd.ForEach ([&] (uint64_t start, size_t length) {
		if (!AddSegmentCommandFromProcessMemory (taskPort, pCoreBuilder, start, length)) {
			MMD_DEBUGLOG_LINE << "Failed to add memory segment at 0x" << std::hex << start << " (length " << std::dec
							  << length << ")";
		}
	});

	vm_deallocate (mach_task_self (), (vm_address_t) threads, nThreads * sizeof (thread_act_t));

	return true;
}

bool AddNotesToCore (MachOCoreDumpBuilder* pCoreBuilder)
{
	// Payloads for these will be added later
	pCoreBuilder->AddNoteCommand (MachOCore::AddrableBitsOwner);
	pCoreBuilder->AddNoteCommand (MachOCore::AllImageInfosOwner);

	return true;
}

} // namespace

extern "C" int MiniDumpWriteDump (mach_port_t taskPort, int fd, struct MMDCrashContext* pCrashContext)
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

	// We want to create a core dump of the process with consistent (memory) state.
	// Because of this, if its of another process, we need to suspend the task
	// For a self dump, we suspend all threads except the current one upfront (there is an unavoidable race condition,
	//   though: threads might start and end in the meantime)
	const bool						  selfDump = taskPort == mach_task_self ();
	std::vector<MachPortSendRightRef> suspendedThreads;

	if (!selfDump) {
		if (task_suspend (taskPort) != KERN_SUCCESS)
			return false;
	} else {
		if (!SuspendAllThreadsExceptCurrentOne (taskPort, &suspendedThreads))
			return false;
	}

	defer {
		if (!selfDump)
			task_resume (taskPort);
		else
			ResumeThreads (suspendedThreads);
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

	if (!AddNotesToCore (&coreBuilder))
		return false;

	if (!AddPayloadsAndWrite (&coreBuilder, modules, pOStream))
		return false;

	return true;
}

} // namespace MMD
