#include "MachOCoreInternal.hpp"

#include <cassert>

namespace MMD {
namespace MachOCore {

const char* AddrableBitsOwner  = "addrable bits";
const char* AllImageInfosOwner = "all image infos";

ThreadInfo::ThreadInfo (thread_act_t threads_i, bool suspendWhileInspecting):
	suspendWhileInspecting (suspendWhileInspecting),
	threads_i (threads_i),
#ifdef __x86_64__
	gprCount (x86_THREAD_STATE64_COUNT),
	excCount (x86_EXCEPTION_STATE64_COUNT),
	gprFlavor (x86_THREAD_STATE64),
	excFlavor (x86_EXCEPTION_STATE64),
#elif defined __arm64__
	gprCount (ARM_THREAD_STATE64_COUNT),
	excCount (ARM_EXCEPTION_STATE64_COUNT),
	gprFlavor (ARM_THREAD_STATE64),
	excFlavor (ARM_EXCEPTION_STATE64),
#endif
	healthy (false)
{
	if (suspendWhileInspecting) {
		if (thread_suspend (threads_i) != KERN_SUCCESS)
			return;
	}

	if (thread_get_state (threads_i, gprFlavor, (thread_state_t) &ts, &gprCount) != KERN_SUCCESS)
		return;

	if (thread_get_state (threads_i, excFlavor, (thread_state_t) &es, &excCount) != KERN_SUCCESS)
		return;

	healthy = true;

	gpr.kind	   = MachOCore::RegSetKind::GPR;
	gpr.nWordCount = sizeof ts / sizeof (uint32_t);
	memcpy (&gpr.gpr, &ts, sizeof ts);

	exc.kind	   = MachOCore::RegSetKind::EXC;
	exc.nWordCount = sizeof es / sizeof (uint32_t);
	memcpy (&exc.exc, &es, sizeof es);
}

ThreadInfo::~ThreadInfo ()
{
	if (suspendWhileInspecting)
		thread_resume (threads_i);
}

GPRPointers::GPRPointers (const GPR& gpr): gpr (gpr) {}

#ifdef __x86_64__

Pointer GPRPointers::BasePointer ()
{
	return Pointer (gpr.gpr.__rbp);
}

Pointer GPRPointers::InstructionPointer ()
{
	return Pointer (gpr.gpr.__rip);
}

Pointer GPRPointers::StackPointer ()
{
	return Pointer (gpr.gpr.__rsp);
}

size_t GPRPointers::AddressWidthInBytes ()
{
	return 8;
}

#elif defined __arm64__

Pointer GPRPointers::BasePointer ()
{
	return Pointer (gpr.gpr.__fp);
}

Pointer GPRPointers::InstructionPointer ()
{
	return Pointer (gpr.gpr.__pc);
}

Pointer GPRPointers::StackPointer ()
{
	return Pointer (gpr.gpr.__sp);
}

size_t GPRPointers::AddressWidthInBytes ()
{
	return 8;
}

#else
	#error Only x86_64 and ARM architectures are supported.
#endif

Pointer::Pointer (size_t widthInBytes, void* ptrIn): ptr (new uint8_t[widthInBytes]), WidthInBytes (widthInBytes)
{
	memcpy (ptr.get (), ptrIn, sizeof (uint8_t) * widthInBytes);
}

Pointer::Pointer (uint64_t ptrIn): ptr (new uint8_t[8]), WidthInBytes (8)
{
	memcpy (ptr.get (), &ptrIn, sizeof (uint8_t) * 8);
}

template<typename T>
T Pointer::As ()
{
	assert (WidthInBytes * sizeof (uint8_t) <= sizeof (T));

	T result;

	uint8_t* u8ptr = ptr.get ();
	memcpy (&result, u8ptr, sizeof (uint8_t) * WidthInBytes);

	return result;
}

void* Pointer::AsGenericPointer ()
{
	return As<void*> ();
}

uint64_t Pointer::AsUInt64 ()
{
	return As<uint64_t> ();
}

} // namespace MachOCore

} // namespace MMD
