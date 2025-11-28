#include <mach/mach.h>

#include <stdio.h>

#include "MacMiniDump.hpp"

#ifdef __cplusplus
	#error This file must be compiled as C, not C++!
#endif

int CreateCoreFromCImpl (char* pPath)
{
	mach_port_t task	= mach_task_self ();
	FILE*		outfile = fopen (pPath, "wb");

	if (outfile == NULL) {
		return 1;
	}

	int result = MiniDumpWriteDump (task, fileno (outfile), NULL);
	fclose (outfile);

	return result;
}
