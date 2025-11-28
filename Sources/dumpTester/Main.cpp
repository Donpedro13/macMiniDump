#include <mach-o/dyld.h>
#include <mach/mach.h>

#include <fcntl.h>
#include <inttypes.h>
#include <spawn.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "FileOStream.hpp"
#include "MacMiniDump.hpp"

namespace {

const std::string	 g_1 = "This is a string!";
std::string			 g_2 = "Another string!";
[[maybe_unused]] int g_3 = 42;

std::string g_corePath;

volatile int a = 0;

__attribute__ ((noinline)) void Spin ()
{
	for (size_t i = 0; i < 500'000'000'0; ++i)
		a *= 2;
}

[[maybe_unused]] void CrashInvalidPtrWrite ()
{
	volatile int* p = (int*) 0xBEEF;
	*p				= 42;
}

[[maybe_unused]] void CrashNullPtrCall ()
{
	typedef void (*FuncPtr) ();
	FuncPtr func = nullptr;
	func ();
}

bool CreateCoreFileImpl (mach_port_t task, const std::string& corePath, MMDCrashContext* pCrashContext = nullptr)
{
	const char* pCorePath = corePath.c_str ();
	// Make sure the destination file exists and is empty
	int fd = open (pCorePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		close (fd);
	else
		return false;

	MMD::FileOStream fos (pCorePath);

	return MiniDumpWriteDump (task, &fos, pCrashContext);
}

bool CreateCoreFile (const std::string& corePath)
{
	return CreateCoreFileImpl (mach_task_self (), corePath);
}

void SignalHandler (int sig, siginfo_t* sigInfo, void* context)
{
	__darwin_ucontext* ucontext		= (__darwin_ucontext*) context;
	MMDCrashContext  crashContext = {};
	crashContext.mcontext			= *reinterpret_cast<__darwin_mcontext64*> (ucontext->uc_mcontext);
	pthread_threadid_np (NULL, &crashContext.crashedTID);

	CreateCoreFileImpl (mach_task_self (), g_corePath, &crashContext);

	kill (getpid (), SIGKILL);
}

bool SetupSignalHandler (void (*handler) (int, siginfo_t*, void*))
{
	struct sigaction sa;
	sa.sa_sigaction = handler;
	sa.sa_flags		= SA_SIGINFO | SA_NODEFER;
	sigemptyset (&sa.sa_mask);

	return sigaction (SIGSEGV, &sa, nullptr) == 0 && sigaction (SIGBUS, &sa, nullptr) == 0 &&
		   sigaction (SIGILL, &sa, nullptr) == 0 && sigaction (SIGABRT, &sa, nullptr) == 0 &&
		   sigaction (SIGFPE, &sa, nullptr) == 0;
}

bool CrashOnMainThread (const std::string&)
{
	if (!SetupSignalHandler (SignalHandler))
		return false;

	CrashNullPtrCall ();

	return true; // Unreachable
}

bool CrashOnBackgroundThread (const std::string& corePath)
{
	if (!SetupSignalHandler (SignalHandler))
		return false;

	std::thread t (
		[] (const std::string& corePath) {
			CrashNullPtrCall ();
		},
		corePath);

	t.join ();

	return true; // Unreachable
}

bool OOPCrashImpl (const std::string& corePath, const std::string& scenario)
{
	char	 execPath[PATH_MAX];
	uint32_t size = sizeof (execPath);
	if (_NSGetExecutablePath (execPath, &size) != 0) {
		return false;
	}

	pid_t		pid;
	const char* argv[] = { execPath, scenario.c_str (), corePath.c_str (), nullptr };
	int			status;
	int			pipefd[2];

	if (pipe (pipefd) == -1)
		return false;

	posix_spawn_file_actions_t file_actions;
	posix_spawn_file_actions_init (&file_actions);
	posix_spawn_file_actions_adddup2 (&file_actions, pipefd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addclose (&file_actions, pipefd[0]);
	posix_spawn_file_actions_addclose (&file_actions, pipefd[1]);

	int result = posix_spawn (&pid, execPath, &file_actions, nullptr, (char* const*) argv, nullptr);

	if (result != 0) {
		posix_spawn_file_actions_destroy (&file_actions);
		close (pipefd[0]);
		close (pipefd[1]);

		return false;
	}

	posix_spawn_file_actions_destroy (&file_actions);
	close (pipefd[1]);

	// Get task port of the child process
	mach_port_t task;
	if (task_for_pid (mach_task_self (), pid, &task) != KERN_SUCCESS) {
		close (pipefd[0]);

		return false;
	}

	// CrashContext is passed to us via stdout as raw bytes
	MMDCrashContext crashContext;
	ssize_t			  bytesRead = read (pipefd[0], &crashContext, sizeof (crashContext));
	if (bytesRead != sizeof (crashContext)) {
		return false;
	}

	close (pipefd[0]);

	if (!CreateCoreFileImpl (task, g_corePath, &crashContext))
		return false;

	kill (pid, SIGKILL);
	waitpid (pid, &status, 0);

	return WIFSIGNALED (status) && WTERMSIG (status) == SIGKILL;
}

bool OOPCrash (const std::string& corePath)
{
	return OOPCrashImpl (corePath, "oopcrashworker");
}

void SignalHandlerForOOPWorker (int sig, siginfo_t* sigInfo, void* context)
{
	__darwin_ucontext* ucontext		= (__darwin_ucontext*) context;
	MMDCrashContext  crashContext = {};
	crashContext.mcontext			= *reinterpret_cast<__darwin_mcontext64*> (ucontext->uc_mcontext);
	pthread_threadid_np (NULL, &crashContext.crashedTID);

	// Print crashContext as bytes to stdout, so the parent can pick ut up
	write (STDOUT_FILENO, &crashContext, sizeof (crashContext));

	sleep (60); // Make sure the parent has time to read the data and create a core file

	kill (getpid (), SIGKILL); // Ideally, this is never reached, because the parent should kill us after it has written
							   // a core file
}

bool OOPCrashWorker (const std::string& corePath)
{
	if (!SetupSignalHandler (SignalHandlerForOOPWorker))
		return false;

	CrashNullPtrCall ();

	return true; // Unreachable
}

bool OOPCrashOnBackgroundThread (const std::string& corePath)
{
	return OOPCrashImpl (corePath, "oopcrashonbackgroundthreadworker");
}

bool OOPCrashOnBackgroundThreadWorker (const std::string& corePath)
{
	if (!SetupSignalHandler (SignalHandlerForOOPWorker))
		return false;

	std::thread t (
		[] (const std::string& corePath) {
			CrashNullPtrCall ();
		},
		corePath);

	t.join ();

	return true; // Unreachable
}

extern "C" int CreateCoreFromCImpl (char* pPath);	// From CCompatTest.c

bool CreateCoreFromC (const std::string& corePath)
{
	if (!SetupSignalHandler (SignalHandler))
		return false;

	CreateCoreFromCImpl (const_cast<char*> (corePath.c_str ()));

	return true; // Unreachable
}

void SetupMiscThreads ()
{
	std::thread t1 ([] () {
		Spin ();
	});

	t1.detach ();

	std::thread t2 ([] () {
		sleep (60);
	});

	t2.detach ();
}

std::map<std::string, std::function<bool (const std::string&)>> g_scenarios = {
	{ "mainthread", CreateCoreFile },
	//{"backgroundthread", CreateCoreFile},
	{ "crashonmainthread", CrashOnMainThread },
	{ "crashonbackgroundthread", CrashOnBackgroundThread },
	{ "oopcrash", OOPCrash },
	{ "oopcrashworker", OOPCrashWorker },
	{ "oopcrashonbackgroundthread", OOPCrashOnBackgroundThread },
	{ "oopcrashonbackgroundthreadworker", OOPCrashOnBackgroundThreadWorker },
	{ "createcorefromc", CreateCoreFromC }
};

void PrintUsage (const char* argv0)
{
	std::cout << "Usage: " << argv0 << " <dump_scenario> <core_path>" << std::endl;
	std::cout << "Scenarios:" << std::endl;
	for (const auto& [scenarioName, _] : g_scenarios) {
		std::cout << "  - " << scenarioName << std::endl;
	}
	std::cout << std::endl;
}

} // namespace

int main (int argc, char* argv[])
{
	if (argc < 3) {
		PrintUsage (argv[0]);

		return 1;
	}

	const std::string scenario = argv[1];
	g_corePath				   = argv[2];

	if (g_scenarios.find (scenario) == g_scenarios.end ()) {
		std::cerr << "Unknown scenario: " << scenario << std::endl;
		PrintUsage (argv[0]);

		return 1;
	}

	// Create a few threads so the dump contains more state
	SetupMiscThreads ();

	if (!g_scenarios[scenario](g_corePath)) {
		std::cerr << "Scenario " << scenario << " failed" << std::endl;

		return 1;
	}
}
