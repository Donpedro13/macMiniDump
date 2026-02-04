#ifndef MMD_READPROCESSMEMORY
#define MMD_READPROCESSMEMORY

#pragma once

#include <mach/port.h>

#include "ZoneAllocator.hpp"

namespace MMD {

// Reads process memory into a caller-provided buffer. Returns true on success.
bool ReadProcessMemoryInto (mach_port_t taskPort, uintptr_t address, void* buffer, size_t size);

// Convenience overload for reading a single value of type T
template<typename T>
bool ReadProcessMemoryInto (mach_port_t taskPort, uintptr_t address, T* pOut)
{
	return ReadProcessMemoryInto (taskPort, address, pOut, sizeof (T));
}

// Allocates and returns a buffer containing the read memory. Returns nullptr on failure.
UniquePtr<char[]> ReadProcessMemory (mach_port_t taskPort, uintptr_t address, size_t size);
bool			  ReadProcessMemoryString (mach_port_t taskPort, uintptr_t address, size_t maxSize, String* pStringOut);

} // namespace MMD

#endif // MMD_READPROCESSMEMORY
