#include "WalkStack.hpp"
#include "ReadProcessMemory.hpp"
#include "Utils/AddSegmentCommandFromProcessMemory.hpp"

#include <iostream>

// scummed from llvm, MachVMRegion.h and .cpp
#if defined(VM_REGION_SUBMAP_SHORT_INFO_COUNT_64)
  typedef vm_region_submap_short_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 };
#else
  typedef vm_region_submap_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_INFO_COUNT_64 };
#endif

static uint64_t GetProtectionOf (mach_port_t taskPort, uint64_t addr, uint64_t size)
{
	natural_t nesting_depth;
	RegionInfo info;
	mach_msg_type_number_t infoCnt;

	uint64_t       recurseAddr = addr;
	mach_vm_size_t recurseSize = size;

	::mach_vm_region_recurse (taskPort,
							&recurseAddr,
							&recurseSize,
							&nesting_depth,
							(vm_region_recurse_info_t)&info, // scummed from llvm
							&infoCnt);

	return info.protection;
}

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

	uint64_t protection = GetProtectionOf(taskPort, startAddress, length);

	MMD::Utils::AddSegmentCommandFromProcessMemory(pCoreBuilder, taskPort, protection, startAddress, length);
}

} // namespace MMD

} // namespace WalkStack