#ifndef MMD_MACHPORTSENDRIGHTREF
#define MMD_MACHPORTSENDRIGHTREF

#pragma once

#include <mach/mach.h>

namespace MMD {

class MachPortSendRightRef {
public:
	static MachPortSendRightRef Wrap (mach_port_t port, mach_port_t owningTask = mach_task_self ());

	MachPortSendRightRef () = delete;

	MachPortSendRightRef (const MachPortSendRightRef&)			  = delete;
	MachPortSendRightRef& operator= (const MachPortSendRightRef&) = delete;

	MachPortSendRightRef (MachPortSendRightRef&& other) noexcept;
	MachPortSendRightRef& operator= (MachPortSendRightRef&& other) noexcept;

	~MachPortSendRightRef ();

	mach_port_t Get () const;
	mach_port_t Release ();
	void		Reset (mach_port_t port = MACH_PORT_NULL, mach_port_t owningTask = mach_task_self ());

private:
	explicit MachPortSendRightRef (mach_port_t port, mach_port_t owningTask);

	mach_port_t m_port		 = MACH_PORT_NULL;
	mach_port_t m_owningTask = MACH_PORT_NULL;
};

} // namespace MMD

#endif // MMD_MACHPORTSENDRIGHTREF