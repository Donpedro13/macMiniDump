#ifndef MMD_MACHOCOREINTERNAL
#define MMD_MACHOCOREINTERNAL

#pragma once

#include <mach/mach.h>
#include <uuid/uuid.h>

#include <cstdint>

#include "ZoneAllocator.hpp"

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
	uint64_t filepath_offset  = UINT64_MAX;
	uuid_t	 uuid			  = {};
	uint64_t load_address	  = UINT64_MAX;
	uint64_t seg_addrs_offset = UINT64_MAX;
	uint32_t segment_count	  = 0;
	uint32_t reserved		  = 0;
};

struct SegmentVMAddr {
	char	 segname[16] = {};
	uint64_t vmaddr		 = UINT64_MAX;
	uint64_t unused		 = 0;
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
	uint32_t   nWordCount;

#ifdef __x86_64__
	x86_thread_state64_t gpr;
#elif defined __arm64__
	arm_thread_state64_t gpr;
#endif
};

struct EXC {
	RegSetKind kind;
	uint32_t   nWordCount;

#ifdef __x86_64__
	x86_exception_state64_t exc;
#elif defined __arm64__
	arm_exception_state64_t exc;
#endif
};

class ThreadInfo final {
private:
	bool		 suspendWhileInspecting;
	thread_act_t threads_i;

public:
#ifdef __x86_64__
	x86_thread_state64_t	ts;
	x86_exception_state64_t es;
#elif defined __arm64__
	arm_thread_state64_t	ts;
	arm_exception_state64_t es;
#endif
	mach_msg_type_number_t gprCount;
	mach_msg_type_number_t excCount;
	thread_state_flavor_t  gprFlavor;
	thread_state_flavor_t  excFlavor;

	GPR gpr;
	EXC exc;

	ThreadInfo (thread_act_t, bool suspendWhileInspecting);
	~ThreadInfo ();

	bool healthy;
};

class Pointer final {
private:
	UniquePtr<uint8_t[]> ptr;

public:
	explicit Pointer (size_t widthInBytes, void* ptr);
	explicit Pointer (uintptr_t ptr);

	void*	  AsGenericPointer ();
	uintptr_t AsUIntPtr ();

	template<typename T>
	T As ();

	size_t WidthInBytes;
};

class GPRPointers final {
private:
	const GPR& gpr;

public:
	explicit GPRPointers (const GPR& gpr);

	Pointer BasePointer () const;
	Pointer InstructionPointer () const;
	Pointer StackPointer () const;

	size_t AddressWidthInBytes () const;
};

extern const char* AddrableBitsOwner;
extern const char* AllImageInfosOwner;
extern const char* ProcessMetadataOwner;

} // namespace MachOCore
} // namespace MMD

#endif // MMD_MACHOCOREINTERNAL
