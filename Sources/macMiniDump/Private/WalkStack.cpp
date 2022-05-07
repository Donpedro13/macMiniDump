#include "WalkStack.hpp"
#include "ReadProcessMemory.hpp"
#include "Utils/AddSegmentCommandFromProcessMemory.hpp"
#include "Utils/RegionInfo.hpp"
#include "Utils/GetProtectionOf.hpp"

#include <iostream>
#include <syslog.h>

namespace MMD {
namespace WalkStack {

static const size_t BOLDLY_ASSUMED_ADDRESS_LENGTH_ON_ALL_PLATFOMRS_IN_BYTES = 8;

static uint64_t Deref (mach_port_t taskPort, const uint64_t ptr);

void WalkStack (mach_port_t taskPort, uint64_t instructionPointer, uint64_t basePointer, std::vector<IStackWalkVisitor *> visitors)
{
	
	std::cout << "Start walking from base pointer 0x"
			  << std::hex << basePointer 
			  << " and instruction pointer 0x"
			  << std::hex << instructionPointer << std::endl;
	
#ifdef __arm64__
	// Clear PAC bits from the pointer
	asm ("xpaci %0" : "+r" (instructionPointer));
#endif

	for (IStackWalkVisitor * visitor: visitors) visitor->Visit(taskPort, instructionPointer, basePointer);

	uint64_t upperFunctionBasePointer = 0;
	uint64_t upperFunctionReturnAddress = 0;

	for (;;)
	{
		upperFunctionBasePointer = Deref(taskPort, basePointer);
		upperFunctionReturnAddress = Deref(taskPort, basePointer + BOLDLY_ASSUMED_ADDRESS_LENGTH_ON_ALL_PLATFOMRS_IN_BYTES);
		
#ifdef __arm64__
	// Clear PAC bits from the pointer
	//
	//           >>\.
	//         /_  )`.
	//        /  _)`^)`.   _.---. _
	//       (_,' \  `^-)""      `.\
	//             |              | \
	//             \              / |
	//            / \  /.___.'\  (\ (_
	//           < ,"||     \ |`. \`-'
	//            \\ ()      )|  )/
	//            |_>|>     /_] //
	//              /_]        /_]
	//
	asm ("xpaci %0" : "+r" (upperFunctionReturnAddress));
#endif

		std::cout 
			<< "Upper function base pointer is 0x"
			<< std::hex
			<< upperFunctionBasePointer
			<< " and upper function return address is 0x"
			<< upperFunctionReturnAddress << std::endl;

		if(upperFunctionBasePointer != 0) {
			for (IStackWalkVisitor * visitor: visitors) {
				visitor->Visit(taskPort, upperFunctionReturnAddress, upperFunctionBasePointer);
			}

			basePointer = upperFunctionBasePointer;
		} else {
			break;
		}
	}
}

static uint64_t Deref (mach_port_t taskPort, const uint64_t ptr) {
	std::cout << "dereffering 0x" << std::hex << ptr << "... ";
	
	uint64_t result = 0;

	if (ptr != 0) {
		std::unique_ptr<char[]> mem (ReadProcessMemory (taskPort,
														ptr,
														BOLDLY_ASSUMED_ADDRESS_LENGTH_ON_ALL_PLATFOMRS_IN_BYTES));

		if (mem != nullptr) {
			result = *reinterpret_cast<uint64_t*> (mem.get());
		}
	}

	std::cout << "got dereffered new pointer: 0x" << std::hex << result << "... ";

	return result;
}

SegmentCollectorVisitor::SegmentCollectorVisitor (MMD::MachOCoreDumpBuilder *pCoreBuilder):
	pCoreBuilder (pCoreBuilder)
{

}

void SegmentCollectorVisitor::Visit (mach_port_t taskPort,
								    uint64_t nextCallStackAddress,
									uint64_t /*nextBasePointer*/)
{
	if(nextCallStackAddress == 0) return;

	static const size_t SEGMENT_DISTANCE = 256;

	uint64_t middleAddress = nextCallStackAddress;
	uint64_t startAddress = middleAddress - SEGMENT_DISTANCE;
	size_t length = (2 * SEGMENT_DISTANCE) + 1;

	std::cout << "\n" << std::hex << "--- start: 0x" << startAddress << " -- middle: 0x" << middleAddress << " -- ...." << std::endl;
	std::cout << "----------- " << std::dec << length << " bytes ---------------->\n" << std::endl;

	uint64_t protection = Utils::GetProtectionOf(taskPort, startAddress, length);

	MMD::Utils::AddSegmentCommandFromProcessMemory(pCoreBuilder, taskPort, protection, startAddress, length);
}

ExecutingModuleCollectorVisitor::ExecutingModuleCollectorVisitor (ModuleList* pModules):
	m_pModules (pModules)
{
}
	
void ExecutingModuleCollectorVisitor::Visit (mach_port_t taskPort, uint64_t nextCallStackAddress, uint64_t nextBasePointer)
{
	if (!m_pModules->MarkAsExecuting (nextCallStackAddress))
		syslog (LOG_WARNING, "Unable to mark module as executing for address %llu!", nextCallStackAddress);
}

} // namespace MMD

} // namespace WalkStack
