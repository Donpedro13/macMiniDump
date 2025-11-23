#include "StackWalk.hpp"

#include <syslog.h>

#include <cassert>

#include "ReadProcessMemory.hpp"

namespace MMD {
namespace {

#if __arm64__
void StripPACFromPointer (uint64_t* ptr)
{
	asm("xpaci %0" : "+r"(*ptr)); // Clear pointer authentication bits
}
#endif

uint64_t DerefPtr (mach_port_t taskPort, const uint64_t ptr)
{
	assert (ptr != 0);

	uint64_t result = 0;

	std::unique_ptr<char[]> mem (ReadProcessMemory (taskPort, ptr, sizeof (uint64_t)));

	if (mem != nullptr)
		result = *reinterpret_cast<uint64_t*> (mem.get ());

	return result;
}

} // namespace

std::vector<uint64_t> WalkStack (mach_port_t taskPort, uint64_t instructionPointer, uint64_t basePointer)
{
	// "Classic" base pointer chasing algorithm
	std::vector<uint64_t> result;

	auto addIPToResult = [&result] (uint64_t ip) {
#ifdef __arm64__
		StripPACFromPointer (&ip);
#endif
		result.push_back (ip);
	};

	addIPToResult (instructionPointer);

	uint64_t prevBasePointer		= basePointer;
	uint64_t nextBasePointer		= 0;
	uint64_t nextInstructionPointer = 0;

	while (true) {
		nextBasePointer		   = DerefPtr (taskPort, prevBasePointer);
		nextInstructionPointer = DerefPtr (taskPort, prevBasePointer + sizeof prevBasePointer);

		if (nextBasePointer == 0)
			break; // Stack walk finished

		addIPToResult (nextInstructionPointer);

		prevBasePointer = nextBasePointer;
	}

	return result;
}

} // namespace MMD