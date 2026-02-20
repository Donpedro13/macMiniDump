#ifndef MMD_IRANDOMACCESSBINARYOSTREAM
#define MMD_IRANDOMACCESSBINARYOSTREAM

#pragma once

#include <cstddef>
#include <type_traits>

namespace MMD {

class IRandomAccessBinaryOStream {
public:
	IRandomAccessBinaryOStream ();
	IRandomAccessBinaryOStream (const IRandomAccessBinaryOStream& rhs)			  = delete;
	IRandomAccessBinaryOStream& operator= (const IRandomAccessBinaryOStream& rhs) = delete;

	virtual bool Write (const void* pData, size_t size) = 0;

	template<typename T>
	bool Write (const T& data);

	virtual bool Flush () = 0;

	virtual size_t GetPosition ()			   = 0;
	virtual void   SetPosition (size_t newPos) = 0;

	virtual size_t GetSize ()				= 0;
	virtual bool   SetSize (size_t newSize) = 0;

	virtual ~IRandomAccessBinaryOStream ();
};

template<typename T>
bool IRandomAccessBinaryOStream::Write (const T& data)
{
	static_assert (std::is_trivially_copyable_v<T>);

	return Write (reinterpret_cast<const char*> (&data), sizeof data);
}

} // namespace MMD

#endif // MMD_IRANDOMACCESSBINARYOSTREAM
