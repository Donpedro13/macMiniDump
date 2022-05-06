#ifndef MMD_PROCESS_MEMORY_READER_DATA_PTR
#define MMD_PROCESS_MEMORY_READER_DATA_PTR

#pragma once

#include "ReadProcessMemory.hpp"
#include "DataAccess.hpp"

#include <mach/vm_map.h>
#include <mach/mach_vm.h>

#include <iostream>

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
	
	std::unique_ptr<char[]> m_currentCopy;
};


}

#endif // #ifndef MMD_PROCESS_MEMORY_READER_DATA_PTR
