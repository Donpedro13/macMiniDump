#include "DataAccess.hpp"

namespace MMD {

CopiedDataPtr::CopiedDataPtr (const void* pData, size_t size): m_pData (new char[size])
{
	memcpy (m_pData.get (), pData, size);
}

} // namespace MMD
