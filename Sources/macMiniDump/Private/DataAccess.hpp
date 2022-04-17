#ifndef MMD_DATAACCESS
#define MMD_DATAACCESS

#pragma once

#include <cstddef>
#include <memory>

namespace MMD {

class IDataPtr {
public:
	virtual const char* Get (size_t offset, size_t size) = 0;
	virtual const char* Get () = 0;
	
	virtual ~IDataPtr () = default;
};

// Class for providing data through a plain data pointer
class PlainDataPtr : public IDataPtr {
public:
	PlainDataPtr (const void* pData): m_pData (reinterpret_cast<const char*> (pData)) {}
	virtual const char* Get (size_t offset, size_t size) override { return m_pData + offset; }
	virtual const char* Get () override { return m_pData; }
	
private:
	const char* m_pData;
};

// Class for providing data with a copy of a piece of memory
class CopiedDataPtr : public IDataPtr {
public:
	CopiedDataPtr (const void* pData, size_t size);
	virtual const char* Get (size_t offset, size_t size) override { return m_pData.get () + offset; }
	virtual const char* Get () override { return m_pData.get (); }
	
private:
	std::unique_ptr<char[]> m_pData;
};

class IDataProvider {
public:
	virtual size_t GetSize () = 0;
	// Returns an IDataPtr object, or nullptr if an error occurred
	virtual IDataPtr* GetDataPtr () = 0;
	
	virtual ~IDataProvider () = default;
};

class DataProvider : public IDataProvider {
public:
	DataProvider (IDataPtr* pDataPtr, size_t size): m_size (size), m_pDataPtr (pDataPtr) {}
	
	virtual size_t GetSize () override { return m_size; }
	virtual IDataPtr* GetDataPtr () override { return m_pDataPtr.get (); }
	
private:
	size_t m_size;
	std::unique_ptr<IDataPtr> m_pDataPtr;
};

}	// namespace MMD

#endif	// MMD_DATAACCESS
