#ifndef MMD_WALKSTACK
#define MMD_WALKSTACK

#pragma once

#include <mach/vm_map.h>
#include <mach/mach_vm.h>
#include "MachOCoreDumpBuilder.hpp"


namespace MMD {
namespace WalkStack {

/// @returns True if walking may be continued, false otherwise.
typedef void (*WalkStackVisitorFn)(mach_port_t taskPort, uint64_t nextCallStackAddress, void * other);
void WalkStack (mach_port_t taskPort, uint64_t instructionPointer, uint64_t basePointer, WalkStackVisitorFn visitor, void* payload);

void SegmentCollectorVisitor(mach_port_t taskPort, uint64_t nextCallStackAddress, MMD::MachOCoreDumpBuilder *pCoreBuilder);
void SegmentCollectorVisitor(mach_port_t taskPort, uint64_t nextCallStackAddress, void *pCoreBuilder);

} // namespace MMD

} // namespace WalkStack

#endif // #ifndef MMD_WALKSTACK