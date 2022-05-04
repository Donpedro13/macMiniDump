#include "MachOCoreDumpBuilder.hpp"

#include <cmath>

namespace MMD {
namespace {

size_t RoundUp (size_t number, size_t roundTo)
{
	const size_t quotient = number / roundTo;
	const size_t remainder = number % roundTo;
	
	return remainder == 0 ? number : (quotient + 1) * roundTo;
}

}	// namespace

MachOCoreDumpBuilder::MachOCoreDumpBuilder (): m_loadCommandsFinalized (false)
{
	m_header.magic = MH_MAGIC_64;
	m_header.cputype =
#ifdef __x86_64__
	CPU_TYPE_X86_64;
#elif defined __arm64__
	CPU_TYPE_ARM64;
#endif

#ifdef __x86_64__
	m_header.cpusubtype = CPU_SUBTYPE_I386_ALL;
#elif defined __arm64__
	m_header.cpusubtype = 0;
#endif

	m_header.filetype = MH_CORE;
	m_header.ncmds = 0;
	m_header.sizeofcmds = 0;
	m_header.flags = 0;
	m_header.reserved = 0;
}

bool MachOCoreDumpBuilder::Build (IRandomAccessBinaryOStream* pOStream)
{
#ifdef _DEBUG
	m_writtenRanges.clear ();
#endif
	
	FinalizeLoadCommands ();
	
	// Write out the header
	if (!WriteToOStream (m_header, pOStream))
		return false;
	
	// Update payload offsets, then write out load commands
	for (auto& nc : m_note_cmds) {
		GetOffsetForNoteCommandPayload (nc.first.data_owner, &nc.first.offset);
		
		if (!WriteToOStream (nc.first, pOStream))
			return false;
	}

	// Thread commands are self-contained (no payload)
	for (const auto& pTc : m_thread_cmds) {
		if (!WriteToOStream (pTc.get (), pTc->cmdsize, pOStream))
			return false;
	}
	
	for (auto& sc : m_segment_cmds) {
		GetOffsetForSegmentCommandPayload(sc.first.vmaddr, &sc.first.fileoff);
		
		if (!WriteToOStream (sc.first, pOStream))
			return false;
	}
	
	// Time for writing out payloads
	for (const auto& nc : m_note_cmds) {
		uint64_t offset;
		GetOffsetForNoteCommandPayload (nc.first.data_owner, &offset);
		pOStream->SetPosition (offset);
		
		if (!WriteToOStream (nc.second->GetDataPtr ()->Get (), nc.first.size, pOStream))
			return false;
	}
	
	for (const auto& sc : m_segment_cmds) {
		assert (sc.second->GetSize () == sc.first.filesize);
		
		uint64_t startOffset;
		GetOffsetForSegmentCommandPayload (sc.first.vmaddr, &startOffset);
		pOStream->SetPosition (startOffset);
		
		// Some segments might be huge, so we might need to write them out in chunks
		const size_t MaxChunkSize = 4'096 * 1'024;
		const size_t dataSize = sc.second->GetSize ();
		const size_t chunkSize = std::min (dataSize, MaxChunkSize);
		const size_t nLeftOverBytes = dataSize % chunkSize;
		const size_t iters = dataSize / chunkSize;

		size_t offset = 0;
		for (size_t i = 0; i < iters; ++i) {
			if (!WriteToOStream (sc.second->GetDataPtr ()->Get (offset, chunkSize), chunkSize, pOStream))
				return false;
			
			offset += chunkSize;
		}
		
		if (nLeftOverBytes >= 0) {
			if (!WriteToOStream (sc.second->GetDataPtr ()->Get (offset, nLeftOverBytes), nLeftOverBytes, pOStream))
				return false;
		}
	}
	
	return true;
}

void MachOCoreDumpBuilder::FinalizeLoadCommands ()
{
	// It's legal to call this function multiple times (further modification of load commands is guarded against elsewhere)
	if (m_loadCommandsFinalized)
		return;
	
	// Update some fields of the header
	
	m_header.ncmds = m_note_cmds.size () + m_thread_cmds.size () + m_segment_cmds.size ();
	size_t sizeOfCmds = 0;
	for (const auto& nc : m_note_cmds)
		sizeOfCmds += nc.first.cmdsize;
	for (const auto& tc : m_thread_cmds)
		sizeOfCmds += tc->cmdsize;
	for (const auto& sc : m_segment_cmds)
		sizeOfCmds += sc.first.cmdsize;
	
	m_header.sizeofcmds = sizeOfCmds;
	
	m_loadCommandsFinalized = true;
}

bool MachOCoreDumpBuilder::AddNoteCommand (const char* pOwner, std::unique_ptr<IDataProvider> dataProvider)
{
	assert (pOwner != nullptr);
	
	if (m_loadCommandsFinalized)
		return false;
	
	note_command newCommand = {};
	newCommand.cmd = LC_NOTE;
	newCommand.cmdsize = sizeof (note_command);
	
	const size_t ownerLen = strlen(pOwner);
	if (ownerLen > 16)
		return false;
	
	memset(newCommand.data_owner, 0, sizeof newCommand.data_owner);
	// Not a null terminated string, so strncpy is fine
	strncpy(newCommand.data_owner, pOwner, ownerLen);
	
	// Due to file alignment, offset will be calculated later (depends e.g. on the number of other load commands, as well)
	
	if (dataProvider != nullptr)
		newCommand.size = dataProvider->GetSize ();
	
	m_note_cmds.emplace_back (std::make_pair (newCommand, std::move(dataProvider)));
	
	return true;
}

bool MachOCoreDumpBuilder::AddSegmentCommand (uint64_t vmaddr, uint32_t prot, std::unique_ptr<IDataProvider> dataProvider)
{
	if (m_loadCommandsFinalized)
		return false;
	
	const uint64_t size = dataProvider == nullptr ? 0 : dataProvider->GetSize ();
	
	segment_command_64 segCommand = {};
	segCommand.cmd = LC_SEGMENT_64;
	segCommand.cmdsize = sizeof (segCommand);
	segCommand.vmaddr = vmaddr;
	segCommand.vmsize = size;
	// fileoff depends on other load commands as well, so it will be calculated later
	segCommand.filesize = size;
	segCommand.maxprot = prot;
	segCommand.initprot = prot;
	segCommand.nsects = 0;
	segCommand.flags = 0;
	
	m_segment_cmds.emplace_back (std::make_pair (segCommand, std::move (dataProvider)));
	
	return true;
}

bool MachOCoreDumpBuilder::AddDataProviderForNoteCommand (const char* pOwnerName, std::unique_ptr<IDataProvider> pDataProvider)
{	
	for (auto& ncPair : m_note_cmds) {
		if (strcmp (ncPair.first.data_owner, pOwnerName) == 0) {
			assert (ncPair.second == nullptr);
			
			ncPair.second = std::move (pDataProvider);
			ncPair.first.size = ncPair.second->GetSize ();
			
			return true;
		}
	}
	
	return false;
}

bool MachOCoreDumpBuilder::AddDataProviderForSegmentCommand (uint64_t vmaddr, std::unique_ptr<IDataProvider> pDataProvider)
{
	for (auto& scPair : m_segment_cmds) {
		if (scPair.first.vmaddr == vmaddr) {
			assert (scPair.second == nullptr);
			
			scPair.second = std::move (pDataProvider);
			scPair.first.filesize = scPair.first.vmsize = scPair.second->GetSize ();
			
			return true;
		}
	}
	
	return false;
}

bool MachOCoreDumpBuilder::GetOffsetForNoteCommandPayload (const char* pOwnerName, uint64_t* pOffsetOut) const
{
	if (!m_loadCommandsFinalized)
		return false;
	
	const size_t payloadStartOffset = sizeof m_header + m_header.sizeofcmds;
	// The very first payload will be aligned to a 16-byte boundary
	size_t payloadOffset = RoundUp (payloadStartOffset, 16);
	
	for (const auto& nc : m_note_cmds) {
		if (strcmp (pOwnerName, nc.first.data_owner) == 0) {
			*pOffsetOut = payloadOffset;
			
			return true;
		} else {
			assert (nc.second != nullptr);	// Sizes of all previous note commands must be known
			
			payloadOffset += nc.first.size;
		}
	}
	
	// Note command not found
	return false;
}

bool MachOCoreDumpBuilder::GetOffsetForSegmentCommandPayload (uint64_t vmaddr, uint64_t* pOffsetOut) const
{
	if (!m_loadCommandsFinalized)
		return false;
	
	const note_command* pLastNoteCmd = &m_note_cmds.back ().first;
	uint64_t lastNoteCommandPayloadOffset = 0;
	GetOffsetForNoteCommandPayload(pLastNoteCmd->data_owner, &lastNoteCommandPayloadOffset);
	
	// The first segment payload should be written to a 4K boundary
	uint64_t payloadOffset = RoundUp (lastNoteCommandPayloadOffset + pLastNoteCmd->size, 0x1000);
	for (const auto& sc : m_segment_cmds) {
		if (vmaddr == sc.first.vmaddr) {
			*pOffsetOut = payloadOffset;
			
			return true;
		} else {
			assert (sc.second != nullptr);	// Sizes of all previous segment commands must be known
			
			payloadOffset += sc.first.filesize;
		}
	}
	
	// Segment command not found
	return false;
}

size_t MachOCoreDumpBuilder::GetNumberOfSegmentCommands () const
{
	return m_segment_cmds.size();
}

segment_command_64* MachOCoreDumpBuilder::GetSegmentCommand (size_t index)
{
	return &m_segment_cmds[index].first;
}

bool MachOCoreDumpBuilder::WriteToOStream (const void* pData, size_t size, IRandomAccessBinaryOStream* pOStream)
{
	// Debug feature: lLet's check if we would overwrite parts or not
#ifdef _DEBUG
	size_t start = pOStream->GetPosition ();
	size_t end = start + size;
	
	// Poor man's interval search...
	for (const auto& interval : m_writtenRanges) {
		if ((start < interval.second && start > interval.first) ||
			(end > interval.first && end < interval.second))
		{
			assert (false); // Existing range would be overwritten
			
			return false;
		}
	}
	
	m_writtenRanges.push_back({ start, end });
#endif	// _DEBUG
	
	return pOStream->Write (pData, size);
}

}	// namespace MMD
