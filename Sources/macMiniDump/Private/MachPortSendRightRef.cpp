#include "MachPortSendRightRef.hpp"

#include <utility>

namespace MMD {

MachPortSendRightRef MachPortSendRightRef::Wrap (mach_port_t port, mach_port_t owningTask)
{
	return MachPortSendRightRef (port, owningTask);
}

MachPortSendRightRef::MachPortSendRightRef (MachPortSendRightRef&& other) noexcept:
	m_port (other.m_port),
	m_owningTask (other.m_owningTask)
{
	other.m_port = MACH_PORT_NULL;
}

MachPortSendRightRef& MachPortSendRightRef::operator= (MachPortSendRightRef&& other) noexcept
{
	if (this != &other) {
		Reset (std::exchange (other.m_port, MACH_PORT_NULL), other.m_owningTask);
	}

	return *this;
}

MachPortSendRightRef::~MachPortSendRightRef ()
{
	Reset ();
}

mach_port_t MachPortSendRightRef::Get () const
{
	return m_port;
}

mach_port_t MachPortSendRightRef::Release ()
{
	return std::exchange (m_port, MACH_PORT_NULL);
}

void MachPortSendRightRef::Reset (mach_port_t port, mach_port_t owningTask)
{
	if (m_port != MACH_PORT_NULL)
		mach_port_deallocate (m_owningTask, m_port);

	m_port		 = port;
	m_owningTask = owningTask;
}

MachPortSendRightRef::MachPortSendRightRef (mach_port_t port, mach_port_t owningTask):
	m_port (port),
	m_owningTask (owningTask)
{
}

} // namespace MMD
