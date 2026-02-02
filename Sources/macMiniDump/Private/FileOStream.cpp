#include "MMD/FileOStream.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace MMD {

FileOStream::FileOStream (FILE* pFile): IRandomAccessBinaryOStream (), m_fd (fileno (pFile)) {}

FileOStream::FileOStream (int fd): m_fd (fd) {}

FileOStream::FileOStream (const std::string filePath)
{
	m_fd = open (filePath.c_str (), O_WRONLY);
}

bool FileOStream::Write (const void* pData, size_t size)
{
	return write (m_fd, pData, size) != -1;
}

bool FileOStream::Flush ()
{
	return fsync (m_fd);
}

size_t FileOStream::GetPosition ()
{
	return lseek (m_fd, 0, SEEK_CUR);
}

void FileOStream::SetPosition (size_t newPos)
{
	lseek (m_fd, newPos, SEEK_SET);
}

size_t FileOStream::GetSize ()
{
	const size_t prevPos = GetPosition ();
	const size_t size	 = lseek (m_fd, 0, SEEK_END);

	lseek (m_fd, prevPos, SEEK_SET);

	return size;
}

bool FileOStream::SetSize (size_t newSize)
{
	return ftruncate (m_fd, newSize) != -1;
}

FileOStream::~FileOStream ()
{
	Cleanup ();
}

bool FileOStream::IsValid () const
{
	return m_fd != -1;
}

void FileOStream::Cleanup ()
{
	if (!IsValid ())
		return;

	close (m_fd);
	m_fd = -1;
}

} // namespace MMD
