#include "StackWalk.hpp"

#include <mach-o/compact_unwind_encoding.h>
#include <mach-o/loader.h>
#include <syslog.h>

#include <cassert>
#include <cinttypes>

#include "ReadProcessMemory.hpp"
#include "StackFrame.hpp"

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
bool IsPreviousInstructionBLKind (mach_port_t taskPort, uintptr_t instructionPointer)
{
	// arm64 instructions are fixed 4-bytes in size
	constexpr size_t		instructionSize = 4;
	std::unique_ptr<char[]> mem (ReadProcessMemory (taskPort, instructionPointer - instructionSize, instructionSize));

	if (mem == nullptr) {
		syslog (LOG_WARNING, "Failed to read memory at %" PRIuPTR, instructionPointer - instructionSize);

		return false;
	}

	const uint32_t instruction = *reinterpret_cast<uint32_t*> (mem.get ());

	// BL: bits [31:26]; see:
	// https://developer.arm.com/documentation/ddi0602/2024-09/Base-Instructions/BL--Branch-with-link-
	const uint32_t blMask	= 0b111111;
	const uint32_t blOpcode = (instruction >> 26) & blMask;
	if (blOpcode == 0b100101)
		return true;

	// BLR: bits [31:10]; see:
	// https://developer.arm.com/documentation/ddi0602/2024-09/Base-Instructions/BLR--Branch-with-link-to-register-
	const uint32_t blrMask	 = 0b1111111111111111111111;
	const uint32_t blrOpcode = (instruction >> 10) & blrMask;

	if (blrOpcode == 0b1101011000111111000000)
		return true;

	// BLRA*: bits [31:11], where bit 24 (Z) is either 0 or 1 see:
	// https://developer.arm.com/documentation/ddi0602/2024-09/Base-Instructions/BLRAA--BLRAAZ--BLRAB--BLRABZ--Branch-with-link-to-register--with-pointer-authentication-
	const uint32_t blraMask	  = 0b111111101111111111111;
	const uint32_t blraOpcode = (instruction >> 11) & blraMask;

	return blraOpcode == 0b110101100011111100001;
}

bool IsPreviousInstructionSVC ([[maybe_unused]] mach_port_t		  taskPort,
							   [[maybe_unused]] const ModuleList& moduleList,
							   [[maybe_unused]] uintptr_t		  instructionPointer)
{
	// arm64 instructions are fixed 4-bytes in size
	constexpr size_t		instructionSize = 4;
	std::unique_ptr<char[]> mem (ReadProcessMemory (taskPort, instructionPointer - instructionSize, instructionSize));

	if (mem == nullptr) {
		syslog (LOG_WARNING, "Failed to read memory at %" PRIuPTR, instructionPointer - instructionSize);

		return false;
	}

	const uint32_t instruction = *reinterpret_cast<uint32_t*> (mem.get ());

	// Check if instruction is SVC; see:
	// SVC: bits [31:21] = 0b11010100000
	const uint32_t svcOpcode = (instruction >> 21) & 0x7FF;

	return svcOpcode == 0b11010100000;
}
#endif

} // namespace

std::vector<uint64_t> WalkStack (mach_port_t							  taskPort,
								 [[maybe_unused]] const MemoryRegionList& memoryRegions,
								 [[maybe_unused]] const ModuleList&		  moduleList,
								 const MachOCore::GPR&					  gpr,
								 [[maybe_unused]] const MachOCore::EXC&	  exc)
{
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

	// While this function implements a very basic "frame pointer chasing" algorithm, it also does some best-effort
	// handling (on arm64) of two special cases revolving around stack frames of
	// the top function: 1.) "partial" stack frames
	//					 2.) frameless (~leaf) functions
	// These cases most likely would result in a function being skipped in the stack trace.

	// 1.) is e.g. when an invalid pointer is call'd, the call instruction "starts" building a new stack frame, but the
	// frame pointer hasn't been updated yet, because the function prologue hasn't executed. There are a dozen
	// variations of this, such as partially executed prologues and epilogues. A 100% correct solution would need a
	// full-blown stack unwinding library with parsing compact and DWARF unwind info, instruction emulation, etc.

	// For 2.), we parse the compact unwind info (if present) for the top instruction pointer to see if the function is
	// frameless. We also handle a tiny edge case: syscall wrappers (see the explanation below)
	[[maybe_unused]] bool topPCNoStackFrame = false;
#ifdef __arm64__
	if (ExceptionMightBeControlTransferRelated (exc)) {
		if (MemoryRegionInfo regionInfo; !memoryRegions.GetRegionInfoForAddress (instructionPointer, &regionInfo) ||
										 !(regionInfo.prot & MemProtExecute)) {
			syslog (LOG_WARNING,
					"Instruction pointer points to not mapped or non-executable memory: %" PRIuPTR,
					instructionPointer);
			topPCNoStackFrame = IsPreviousInstructionBLKind (taskPort, gpr.gpr.__lr);
		}
	}

	if (!topPCNoStackFrame) {
		auto frameLookupResult = LookupStackFrameForPC (taskPort, moduleList, instructionPointer);

		// Edge case: syscall wrappers in libsystem_kernel.dylib are frameless, but they do not have corresponding
		// unwind info. We detect this and go with "frameless" in these cases. This isn't perfect, as the PC might
		// reside inside such a function pointing to a different instruction, but it's quite easy to cherry-pick this
		// case. The chance of the PC being ~on the SVC instruction is somewhat high, as:
		// - kernel to user mode and vice versa transitions take a non-trivial amount of time
		// - syscalls themselves take a non-trivial amount of time
		// - quite a few syscalls are for waiting on something
		if (frameLookupResult == StackFrameLookupResult::Unknown) {
			frameLookupResult = IsPreviousInstructionSVC (taskPort, moduleList, instructionPointer) ?
									StackFrameLookupResult::Frameless :
									frameLookupResult;
		}
		// In case of StackFrameLookupResult::Unknown, we also presume there is a frame (the safer assumption)
		topPCNoStackFrame = frameLookupResult == StackFrameLookupResult::Frameless;
	}
#endif

	uintptr_t prevBasePointer		 = basePointer;
	uintptr_t nextBasePointer		 = 0;
	uintptr_t nextInstructionPointer = 0;

	size_t frameIndex = 0;
	while (true) {
		nextInstructionPointer =
#ifdef __arm64__
			topPCNoStackFrame && frameIndex == 0 ? gpr.gpr.__lr :
#endif
												   DerefPtr (taskPort, prevBasePointer + sizeof prevBasePointer);
		nextBasePointer = topPCNoStackFrame && frameIndex == 0 ? prevBasePointer : DerefPtr (taskPort, prevBasePointer);

		if (nextBasePointer == 0)
			break; // Stack walk finished

		addIPToResult (nextInstructionPointer);

		prevBasePointer = nextBasePointer;

		++frameIndex;
	}

	return result;
}

} // namespace MMD