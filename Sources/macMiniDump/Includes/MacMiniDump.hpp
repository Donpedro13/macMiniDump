#ifndef MMD_MACMINIDUMP
#define MMD_MACMINIDUMP

#pragma once

#include <mach/port.h>
#include <sys/types.h>

#include <csignal>
#include <cstdio>

#include "IRandomAccessBinaryOStream.hpp"

#if !defined __x86_64__ && !defined __arm64__
	#error Unsupported CPU architecture!
#endif

namespace MMD {

struct CrashContext {
	__darwin_mcontext64 mcontext;

	uint64_t crashedTID;
};

bool MiniDumpWriteDump (mach_port_t taskPort, FILE* pFile, CrashContext* pCrashContext = nullptr);
bool MiniDumpWriteDump (mach_port_t taskPort, int fd, CrashContext* pCrashContext = nullptr);
bool MiniDumpWriteDump (mach_port_t					taskPort,
						IRandomAccessBinaryOStream* pOStream,
						CrashContext*				pCrashContext = nullptr);

} // namespace MMD

#endif // MMD_MACMINIDUMP
