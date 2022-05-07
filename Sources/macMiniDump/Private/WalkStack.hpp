#ifndef MMD_WALKSTACK
#define MMD_WALKSTACK

#pragma once

#include <mach/vm_map.h>
#include <mach/mach_vm.h>

#include <list>

#include "MachOCoreDumpBuilder.hpp"
#include "ModuleList.hpp"

namespace MMD {
namespace WalkStack {

class IStackWalkVisitor {
public:
    virtual void Visit(mach_port_t taskPort, uint64_t nextCallStackAddress, uint64_t nextBasePointer) = 0;
};

class SegmentCollectorVisitor final : public IStackWalkVisitor {
    MMD::MachOCoreDumpBuilder *pCoreBuilder;
public:
    SegmentCollectorVisitor(MMD::MachOCoreDumpBuilder *pCoreBuilder);
    virtual void Visit(mach_port_t taskPort, uint64_t nextCallStackAddress, uint64_t nextBasePointer) override;
};

class ExecutingModuleCollectorVisitor final: public IStackWalkVisitor {
public:
	ExecutingModuleCollectorVisitor (ModuleList* pModules);
	
	virtual void Visit(mach_port_t taskPort, uint64_t nextCallStackAddress, uint64_t nextBasePointer) override;
	
private:
	ModuleList* m_pModules;
};

void WalkStack (mach_port_t taskPort, uint64_t instructionPointer, uint64_t basePointer, std::vector<IStackWalkVisitor *> visitor);

} // namespace MMD

} // namespace WalkStack

#endif // #ifndef MMD_WALKSTACK
