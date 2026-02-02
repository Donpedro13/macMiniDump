#ifndef MMD_MACMINIDUMP
#define MMD_MACMINIDUMP

#pragma once

#include <mach/port.h>
#include <sys/types.h>

#include <signal.h>
#include <stdio.h>

#ifdef __cplusplus
	#include "IRandomAccessBinaryOStream.hpp"
#endif // __cplusplus

#if !defined __x86_64__ && !defined __arm64__
	#error Unsupported CPU architecture!
#endif

struct MMDCrashContext {
	struct __darwin_mcontext64 mcontext;

	uint64_t crashedTID;
};

#ifdef __cplusplus
namespace MMD {

using CrashContext = MMDCrashContext;

bool MiniDumpWriteDump (mach_port_t					taskPort,
						IRandomAccessBinaryOStream* pOStream,
						MMDCrashContext*			pCrashContext = nullptr);

} // namespace MMD
#endif // __cplusplus

#ifdef __cplusplus
extern "C"
#endif // __cplusplus
	int MiniDumpWriteDump (mach_port_t taskPort, int fd, struct MMDCrashContext* pCrashContext);

#endif // MMD_MACMINIDUMP
