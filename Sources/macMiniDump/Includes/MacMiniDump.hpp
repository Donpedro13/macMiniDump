#ifndef MMD_MACMINIDUMP
#define MMD_MACMINIDUMP

#pragma once

#include <mach/port.h>

#include <cstdio>

#include "IRandomAccessBinaryOStream.hpp"

#if !defined __x86_64__ && !defined __arm64__
#error Unsupported CPU architecture!
#endif

namespace MMD {

bool MiniDumpWriteDump (mach_port_t taskPort, FILE* pFile);
bool MiniDumpWriteDump (mach_port_t taskPort, int fd);
bool MiniDumpWriteDump (mach_port_t taskPort, IRandomAccessBinaryOStream* pOStream);

}	// namespace MMD

#endif	// MMD_MACMINIDUMP
