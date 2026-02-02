#include "MMD/FileOStream.hpp"
#include "MMD/MacMiniDump.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <mach/mach.h>

bool CreateCoreDumpExample (const std::string& outputPath)
{
	const char* pCorePath = outputPath.c_str ();
	// Make sure the destination file exists and is empty
	int fd = open (pCorePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		close (fd);
	else
		return false;

	MMD::FileOStream fos (pCorePath);

	return MiniDumpWriteDump (mach_task_self (), &fos);
}