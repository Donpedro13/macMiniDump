#include "ProcessMemoryReaderDataPtr.hpp"

namespace MMD {

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

	if(m_currentCopy == nullptr) {
		std::cout << "\n\n!!! Failure reading memory from address at 0x" << std::hex << m_startAddress + offset << "\n" << std::endl;
	}
	
	return m_currentCopy.get ();
}

const char* ProcessMemoryReaderDataPtr::Get ()
{
	return nullptr;	// Not a good idea...
}

}