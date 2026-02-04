#ifndef MMD_FILEOSTREAM
#define MMD_FILEOSTREAM

#pragma once

#include <cstdio>
#include <string>

#include "IRandomAccessBinaryOStream.hpp"

namespace MMD {

class FileOStream : public IRandomAccessBinaryOStream {
public:
	// Constructors
	FileOStream () = delete;
	FileOStream (FILE* pFile);					 // File must be opened for writing
	explicit FileOStream (int fd);				 // fd must be opened for writing
	explicit FileOStream (const char* filePath); // The file at this path must exist

	// Inherited from IRandomAccessBinaryOStream
	virtual bool Write (const void* pData, size_t size) override;

	virtual bool Flush () override;

	virtual size_t GetPosition () override;
	virtual void   SetPosition (size_t newPos) override;

	virtual size_t GetSize () override;
	virtual bool   SetSize (size_t newSize) override;

	virtual ~FileOStream ();

	// Miscellaneous
	bool IsValid () const;

private:
	int m_fd;

	void Cleanup ();
};

} // namespace MMD

#endif // MMD_FILEOSTREAM
