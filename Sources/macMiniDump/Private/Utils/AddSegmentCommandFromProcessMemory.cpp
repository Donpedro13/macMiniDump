#include "AddSegmentCommandFromProcessMemory.hpp"
#include "../ProcessMemoryReaderDataPtr.hpp"

#include <iostream>

bool MMD::Utils::AddSegmentCommandFromProcessMemory (MachOCoreDumpBuilder* pCoreBuilder,
												mach_port_t taskPort,
												MMD::MemoryProtection prot,
											    uint64_t startAddress,
												size_t lengthInBytes)
{
	MMD::ProcessMemoryReaderDataPtr * dataPtr = new MMD::ProcessMemoryReaderDataPtr (taskPort, startAddress, lengthInBytes);
	std::unique_ptr<DataProvider> dataProvider = std::make_unique<DataProvider> (dataPtr, lengthInBytes);

	std::cout << "scheduling segment command from 0x" << std::hex << startAddress << " (length: " << std::dec << lengthInBytes << " bytes)... " << std::endl;
	
	return pCoreBuilder->AddSegmentCommand (startAddress, prot, std::move (dataProvider));
}
