#include "StackWalk.hpp"

#include <syslog.h>

#include <cassert>
#include <cinttypes>

#include "ReadProcessMemory.hpp"

namespace MMD {
namespace {

#if __arm64__
void StripPACFromPointer (uintptr_t* ptr)
{
	asm ("xpaci %0" : "+r"(*ptr)); // Clear pointer authentication bits
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

#ifdef __arm64__
bool ExceptionMightBeControlTransferRelated (MachOCore::EXC const& exc)
{
	// Decode exception class from esr; we are interested in instruction abort and data abort
	const uint32_t esr			  = exc.exc.__esr;
	const uint32_t exceptionClass = (esr >> 26) & 0x3F;

	// See: aarch64/exceptions/exceptions/AArch64.ExceptionClass in the corresponding ARM Reference Manual
	switch (exceptionClass) {
		case 0x20: // Instruction Abort
		case 0x24: // Data Abort
			return true;
		default:
			return false;
	}
}
#endif

#ifdef __arm64__
bool IsPreviousInstructionCallLike (mach_port_t taskPort, uintptr_t instructionPointer)
{
	// arm64 instructions are fixed 4-bytes in size
	constexpr size_t		instructionSize = 4;
	std::unique_ptr<char[]> mem (ReadProcessMemory (taskPort, instructionPointer - instructionSize, instructionSize));

	if (mem == nullptr) {
		syslog (LOG_WARNING, "Failed to read memory at %" PRIuPTR, instructionPointer - instructionSize);

		return false;
	}

	const uint32_t instruction = *reinterpret_cast<uint32_t*> (mem.get ());

	// Check if instruction is BL or BLR
	// BL: bits [31:26] = 0b100101
	const uint32_t blOpcode = (instruction >> 26) & 0x3F;
	if (blOpcode == 0b100101)
		return true;

	// BLR: bits [31:22] = 0b1101011000
	const uint32_t blrOpcode = (instruction >> 22) & 0x3FF;
	return blrOpcode == 0b1101011000;
}
#endif

} // namespace

std::vector<uint64_t> WalkStack (mach_port_t			 taskPort,
								 const MemoryRegionList& memoryRegions,
								 const MachOCore::GPR&	 gpr,
								 const MachOCore::EXC&	 exc)
{
	// "Classic" base pointer chasing algorithm
	std::vector<uint64_t> result;

	const MachOCore::GPRPointers pointers (gpr);
	const uintptr_t				 basePointer		= pointers.BasePointer ().AsUIntPtr ();
	const uintptr_t				 instructionPointer = pointers.InstructionPointer ().AsUIntPtr ();

	auto addIPToResult = [&result] (uintptr_t ip) {
#ifdef __arm64__
		StripPACFromPointer (&ip);
#endif
		result.push_back (ip);
	};

	addIPToResult (instructionPointer);

	// We do best-effort detection of a "partial" stack frame. E.g. when an invalid pointer is call'd, the call
	// instruction "starts" building a new stack frame, but the frame pointer hasn't been updated yet, because the
	// function prologue hasn't executed. These cases most likely result in a function being skipped in the stack trace.
	// There are a dozen variations of this, such as partially executed prologues and epilogues. A 100% correct solution
	// would need a full-blown stack unwinding library with parsing compact unwind info, instruction emulation, etc.

	// We cherry pick the easiest case here: call-like instructions. Only on arm64 though, because disassembling x86-64
	// instructions properly would be a nightmare.
	[[maybe_unused]] bool topStackFramePartial = false;
#ifdef __arm64__
	if (ExceptionMightBeControlTransferRelated (exc)) {
		if (MemoryRegionInfo regionInfo; !memoryRegions.GetRegionInfoForAddress (instructionPointer, &regionInfo) ||
										 !(regionInfo.prot & MemProtExecute)) {
			syslog (LOG_WARNING,
					"Instruction pointer points to not mapped or non-executable memory: %" PRIuPTR,
					instructionPointer);
			topStackFramePartial = IsPreviousInstructionCallLike (taskPort, gpr.gpr.__lr);
		}
	}
#endif

	uintptr_t prevBasePointer		 = basePointer;
	uintptr_t nextBasePointer		 = 0;
	uintptr_t nextInstructionPointer = 0;

	size_t frameIndex = 0;
	while (true) {
		nextInstructionPointer = topStackFramePartial && frameIndex == 0 ?
									 gpr.gpr.__lr :
									 DerefPtr (taskPort, prevBasePointer + sizeof prevBasePointer);
		nextBasePointer =
			topStackFramePartial && frameIndex == 0 ? prevBasePointer : DerefPtr (taskPort, prevBasePointer);

		if (nextBasePointer == 0)
			break; // Stack walk finished

		addIPToResult (nextInstructionPointer);

		prevBasePointer = nextBasePointer;

		++frameIndex;
	}

	return result;
}

} // namespace MMD