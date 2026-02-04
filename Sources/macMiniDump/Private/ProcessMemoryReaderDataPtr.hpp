#ifndef MMD_PROCESS_MEMORY_READER_DATA_PTR
#define MMD_PROCESS_MEMORY_READER_DATA_PTR

#pragma once

#include "DataAccess.hpp"
#include "ReadProcessMemory.hpp"

#include <mach/mach_vm.h>
#include <mach/vm_map.h>

namespace MMD {

class ProcessMemoryReaderDataPtr : public IDataPtr {
public:
	ProcessMemoryReaderDataPtr (mach_port_t taskPort, vm_address_t startAddress, vm_size_t maxSize);

	virtual const char* Get (size_t offset, size_t size) override;
	virtual const char* Get () override;

private:
	mach_port_t	 m_taskPort;
	vm_address_t m_startAddress;
	vm_size_t	 m_maxSize;

	UniquePtr<char[]> m_currentCopy;
};

} // namespace MMD

#endif // #ifndef MMD_PROCESS_MEMORY_READER_DATA_PTR
