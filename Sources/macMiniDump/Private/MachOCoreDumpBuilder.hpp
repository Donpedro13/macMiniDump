#ifndef MMD_MACHOCOREDUMPBUILDER
#define MMD_MACHOCOREDUMPBUILDER

#pragma once

#include <mach-o/loader.h>

#include <memory>
#include <utility>
#include <vector>

#include "DataAccess.hpp"
#include "IRandomAccessBinaryOStream.hpp"

namespace MMD {

// Class for constructing and writing out a Mach-O core dump
// By using this class, only structural correctness is guaranteed, semantical is not
class MachOCoreDumpBuilder {
public:
	MachOCoreDumpBuilder ();

	bool Build (IRandomAccessBinaryOStream* pOStream);

	void FinalizeLoadCommands ();

	bool AddNoteCommand (const char* pOwnerName, std::unique_ptr<IDataProvider> dataProvider = nullptr);
	template<typename... ThreadStates>
	bool AddThreadCommand (ThreadStates... threadStates);
	bool AddSegmentCommand (uintptr_t vmaddr, uint32_t prot, std::unique_ptr<IDataProvider> dataProvider = nullptr);

	bool AddDataProviderForNoteCommand (const char* pOwnerName, std::unique_ptr<IDataProvider> pDataProvider);
	bool AddDataProviderForSegmentCommand (uintptr_t vmaddr, std::unique_ptr<IDataProvider> pDataProvider);

	bool GetOffsetForNoteCommandPayload (const char* pOwnerName, uint64_t* pOffsetOut) const;
	bool GetOffsetForSegmentCommandPayload (uintptr_t vmaddr, uint64_t* pOffsetOut) const;

	size_t				GetNumberOfSegmentCommands () const;
	segment_command_64* GetSegmentCommand (size_t index);

private:
	template<typename LC>
	using LoadCommandsWithLazyData = std::vector<std::pair<LC, std::unique_ptr<IDataProvider>>>;
	using ThreadCommands		   = std::vector<std::unique_ptr<thread_command>>;

	template<typename T>
	bool WriteToOStream (const T& data, IRandomAccessBinaryOStream* pOStream);
	bool WriteToOStream (const void* pData, size_t size, IRandomAccessBinaryOStream* pOStream);

	bool m_loadCommandsFinalized;

	mach_header_64 m_header;

	LoadCommandsWithLazyData<note_command>		 m_note_cmds;
	ThreadCommands								 m_thread_cmds;
	LoadCommandsWithLazyData<segment_command_64> m_segment_cmds;

#ifdef _DEBUG
	std::vector<std::pair<size_t, size_t>> m_writtenRanges;
#endif
};

#include "MachOCoreDumpBuilder.inl"

} // namespace MMD

#endif // MMD_MACHOCOREDUMPBUILDER
