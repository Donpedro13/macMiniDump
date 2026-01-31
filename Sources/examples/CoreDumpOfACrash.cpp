#include <fcntl.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unistd.h>

#include "FileOStream.hpp"
#include "MacMiniDump.hpp"

void SignalHandler (int /*signal*/, siginfo_t* /*pSigInfo*/, void* pContext)
{
	__darwin_ucontext* ucontext		= (__darwin_ucontext*) pContext;
	MMDCrashContext	   crashContext = {};
	crashContext.mcontext			= *reinterpret_cast<__darwin_mcontext64*> (ucontext->uc_mcontext);
	pthread_threadid_np (NULL, &crashContext.crashedTID);

	const char* pCorePath = "/tmp/test.core";
	// Make sure the destination file exists and is empty
	int fd = open (pCorePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		close (fd);

	MMD::FileOStream fos (pCorePath);

	MiniDumpWriteDump (mach_task_self (), &fos);

	kill (getpid (), SIGKILL);
}

void SetupSignalHandler (void (*handler) (int, siginfo_t*, void*))
{
	struct sigaction sa;
	sa.sa_sigaction = handler;
	sa.sa_flags		= SA_SIGINFO | SA_NODEFER;
	sigemptyset (&sa.sa_mask);

	sigaction (SIGSEGV, &sa, nullptr);
	sigaction (SIGBUS, &sa, nullptr);
	sigaction (SIGILL, &sa, nullptr);
	sigaction (SIGABRT, &sa, nullptr);
	sigaction (SIGFPE, &sa, nullptr);
}

int main ()
{
	SetupSignalHandler (SignalHandler);

	[[maybe_unused]] int i = *(volatile int*) 0;
}