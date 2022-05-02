#ifndef MMD_MACHOCOREINTERNAL
#define MMD_MACHOCOREINTERNAL

#pragma once

#include <mach/mach.h>
#include <uuid/uuid.h>

#include <cstdint>

namespace MMD {
namespace MachOCore {

struct AddrableBitsInfo {
	uint32_t version;
	uint32_t nBits;
	uint64_t unused;
};

struct AllImageInfosHeader {
	uint32_t version;
	uint32_t imgcount;
	uint64_t entries_fileoff;
	uint32_t entries_size;
	uint32_t reserved;
};

struct ImageEntry {
	uint64_t filepath_offset = UINT64_MAX;
	uuid_t uuid = {};
	uint64_t load_address = UINT64_MAX;
	uint64_t seg_addrs_offset = UINT64_MAX;
	uint32_t segment_count = 0;
	uint32_t reserved = 0;
};

struct SegmentVMAddr {
	char segname[16] = {};
	uint64_t vmaddr = UINT64_MAX;
	uint64_t unused = 0;
};

enum class RegSetKind : uint32_t {
#ifdef __x86_64__
	GPR = 4,
	EXC = 6
#elif defined __arm64__
	GPR = 6,
	EXC = 7
#endif
};

struct GPR {
	RegSetKind kind;
	uint32_t nWordCount;
	
#ifdef __x86_64__
	x86_thread_state64_t gpr;
#elif defined __arm64__
	arm_thread_state64_t gpr;
#endif
};

struct EXC {
	RegSetKind kind;
	uint32_t nWordCount;
	
#ifdef __x86_64__
	x86_exception_state64_t exc;
#elif defined __arm64__
	arm_exception_state64_t exc;
#endif
};

extern const char* AddrableBitsOwner;
extern const char* AllImageInfosOwner;

}	// namespace MachOCore
}	// namespace MMD

#endif	// MMD_MACHOCOREINTERNAL
