#ifndef MMD_READPROCESSMEMORY
#define MMD_READPROCESSMEMORY

#pragma once

#include <mach/port.h>

#include <memory>
#include <string>

namespace MMD {

std::unique_ptr<char[]> ReadProcessMemory (mach_port_t taskPort, uintptr_t address, size_t size);
bool ReadProcessMemoryString (mach_port_t taskPort, uintptr_t address, size_t maxSize, std::string* pStringOut);

} // namespace MMD

#endif // MMD_READPROCESSMEMORY
