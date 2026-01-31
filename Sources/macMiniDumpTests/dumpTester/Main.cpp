#include <mach-o/dyld.h>
#include <mach/mach.h>

#include <fcntl.h>
#include <inttypes.h>
#include <spawn.h>
#include <syslog.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "FileOStream.hpp"
#include "MacMiniDump.hpp"

#define NOINLINE __attribute__ ((noinline))

namespace {

const std::string	 g_1 = "This is a string!";
std::string			 g_2 = "Another string!";
[[maybe_unused]] int g_3 = 42;

std::string g_corePath;

volatile int a = 0;

const uintptr_t InvalidPtr = 0xFFFFFFFFFFFA7B00;

size_t GetTotalMachPortRightsRefs ()
{
	mach_port_name_array_t names	  = nullptr;
	mach_msg_type_number_t namesCount = 0;
	mach_port_type_array_t types	  = nullptr;
	mach_msg_type_number_t typesCount = 0;

	if (mach_port_names (mach_task_self (), &names, &namesCount, &types, &typesCount) != KERN_SUCCESS)
		return 0;

	size_t result  = 0;

	for (size_t i = 0; i < namesCount; ++i) {
		mach_port_urefs_t refs = 0;

		if (mach_port_get_refs (mach_task_self (), names[i], MACH_PORT_RIGHT_RECEIVE, &refs) != KERN_SUCCESS)
			return 0;

		result += refs;
		
		if (mach_port_get_refs (mach_task_self (), names[i], MACH_PORT_RIGHT_SEND, &refs) != KERN_SUCCESS)
			return 0;

		result += refs;

		if (mach_port_get_refs (mach_task_self (), names[i], MACH_PORT_RIGHT_SEND_ONCE, &refs) != KERN_SUCCESS)
			return 0;

		result += refs;
	}

	vm_deallocate (mach_task_self (), (vm_address_t) names, namesCount * sizeof (mach_port_name_t));
	vm_deallocate (mach_task_self (), (vm_address_t) types, typesCount * sizeof (mach_port_type_t));

	return result;
}

NOINLINE void Spin ()
{
	for (size_t i = 0; i < 500'000'000'0; ++i)
		a *= 2;
}

NOINLINE bool CrashInvalidPtrWrite (const std::string& /*corePath*/)
{
	[[maybe_unused]] volatile int local = 20250425;

	volatile int* p = (int*) InvalidPtr;
	*p				= 42;

	return false; // Unreachable
}

NOINLINE bool CrashNullPtrCall (const std::string& /*corePath*/)
{
	[[maybe_unused]] volatile int local = 20250425;

	typedef void (*FuncPtr) ();
	volatile FuncPtr func = nullptr;
	func ();

	return false; // Unreachable
}

NOINLINE bool CrashInvalidPtrCall (const std::string& /*corePath*/)
{
	[[maybe_unused]] volatile int local = 20250425;

	typedef void (*FuncPtr) ();
	FuncPtr func = reinterpret_cast<FuncPtr> (InvalidPtr);
	func ();

	return false; // Unreachable
}

NOINLINE bool CrashNonExecutablePtrCall (const std::string& /*corePath*/)
{
	[[maybe_unused]] volatile int local = 20250425;

	typedef void (*FuncPtr) ();
	FuncPtr func = reinterpret_cast<FuncPtr> (const_cast<char*> (g_1.c_str ()));
	func ();

	return false; // Unreachable
}

class Base;
void CallOpViaBasePtr (Base* pObject);
class Base {
public:
	Base () { CallOpViaBasePtr (this); }
	
	virtual void Operation () = 0;
};

class Derived : public Base {
public:
	virtual void Operation () {	 }
};

void CallOpViaBasePtr (Base* pObject)
{
	pObject->Operation ();
}

NOINLINE bool AbortPureVirtualCall (const std::string& /*corePath*/)
{
	Derived d;

	return false; // Unreachable
}

bool CreateCoreFileImpl (mach_port_t task, const std::string& corePath, MMDCrashContext* pCrashContext = nullptr)
{
	// Best-effort (in-process crash cases won't get to execute the destructor below) mach port right refs leak checker 
	class MachPortRightRefsLeakChecker {
	public:
		MachPortRightRefsLeakChecker () : m_initialCount (GetTotalMachPortRightsRefs ()) {}
		~MachPortRightRefsLeakChecker ()
		{
			const size_t finalCount = GetTotalMachPortRightsRefs ();
			if (finalCount > m_initialCount) {
				std::cerr << "Detected mach port rights refs leak: initial=" << m_initialCount
						  << ", final=" << finalCount << ", leaked=" << (finalCount - m_initialCount) << std::endl;

				// No test case expects a crash with execution getting here, or no crash with exit code 1 -> failure
				_exit (1);
			}
		}
	private:
		size_t m_initialCount;
	} machPortLeakChecker;

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

void SignalHandler (int /*sig*/, siginfo_t* /*sigInfo*/, void* context)
{
	__darwin_ucontext* ucontext		= (__darwin_ucontext*) context;
	MMDCrashContext	   crashContext = {};
	crashContext.mcontext			= *reinterpret_cast<__darwin_mcontext64*> (ucontext->uc_mcontext);
	pthread_threadid_np (NULL, &crashContext.crashedTID);

	CreateCoreFileImpl (mach_task_self (), g_corePath, &crashContext);

	kill (getpid (), SIGKILL);
}

void SignalHandlerForOOPWorker (int /*sig*/, siginfo_t* /*sigInfo*/, void* context)
{
	__darwin_ucontext* ucontext		= (__darwin_ucontext*) context;
	MMDCrashContext	   crashContext = {};
	crashContext.mcontext			= *reinterpret_cast<__darwin_mcontext64*> (ucontext->uc_mcontext);
	pthread_threadid_np (NULL, &crashContext.crashedTID);

	// Print crashContext as bytes to stdout, so the parent can pick ut up
	write (STDOUT_FILENO, &crashContext, sizeof (crashContext));

	sleep (60); // Make sure the parent has time to read the data and create a core file

	kill (getpid (), SIGKILL); // Ideally, this is never reached, because the parent should kill us after it has written
							   // a core file
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

bool CreateOOPWorker (const std::string& operation,
					  bool				 onBackgroundThread,
					  bool				 crash,
					  const std::string& corePath,
					  int*				 pStdOutFd,
					  pid_t*			 pOutPid)
{
	char	 execPath[PATH_MAX];
	uint32_t size = sizeof execPath;
	if (_NSGetExecutablePath (execPath, &size) != 0)
		return false;

	const std::string backgroundThreadParam = onBackgroundThread ? "BackgroundThread" : "MainThread";

	pid_t		pid;
	const char* argv[] = { execPath,		  "OOPWorker", operation.c_str (), backgroundThreadParam.c_str (),
						   corePath.c_str (), nullptr };
	int			pipefd[2];

	if (crash && pipe (pipefd) == -1)
		return false;

	posix_spawn_file_actions_t file_actions;
	if (crash) {
		posix_spawn_file_actions_init (&file_actions);
		posix_spawn_file_actions_adddup2 (&file_actions, pipefd[1], STDOUT_FILENO);
		posix_spawn_file_actions_addclose (&file_actions, pipefd[0]);
		posix_spawn_file_actions_addclose (&file_actions, pipefd[1]);
	}

	int result = posix_spawn (&pid, execPath, crash ? &file_actions : nullptr, nullptr, (char* const*) argv, nullptr);

	if (result != 0 && crash) {
		posix_spawn_file_actions_destroy (&file_actions);
		close (pipefd[0]);
		close (pipefd[1]);

		return false;
	} else if (crash) {
		posix_spawn_file_actions_destroy (&file_actions);
		close (pipefd[1]);
	}

	*pOutPid   = pid;
	*pStdOutFd = crash ? pipefd[0] : -1;

	return true;
}

bool LaunchOOPWorkerForOperation (const std::string& operation, bool onBackgroundThread, const std::string& corePath)
{
	const bool crash = (operation.find ("Crash") != std::string::npos) || (operation.find ("Abort") != std::string::npos);
	pid_t	   pid;
	int		   stdOutFd;
	if (!CreateOOPWorker (operation, onBackgroundThread, crash, corePath, &stdOutFd, &pid))
		return false;

	if (!crash) {
		int status;
		waitpid (pid, &status, 0);

		return WIFEXITED (status) && WEXITSTATUS (status) == 0;
	}

	// If a crash was requested, create a core file from the crash context sent via stdout
	mach_port_t task;
	if (task_for_pid (mach_task_self (), pid, &task) != KERN_SUCCESS) {
		close (stdOutFd);

		return false;
	}

	// CrashContext is passed to us via stdout as raw bytes
	MMDCrashContext crashContext;
	ssize_t			bytesRead = read (stdOutFd, &crashContext, sizeof crashContext);
	if (bytesRead != sizeof crashContext)
		return false;

	close (stdOutFd);

	if (!CreateCoreFileImpl (task, corePath, &crashContext))
		return false;

	if (kill (pid, SIGKILL) != 0)
		return false;
	int	  status;
	pid_t waitResult;
	do {
		waitResult = waitpid (pid, &status, 0);
	} while (waitResult == -1 && errno == EINTR);

	if (errno != 0 && waitResult == -1)
		return false;

	return WIFSIGNALED (status) && WTERMSIG (status) == SIGKILL;
}

extern "C" int CreateCoreFromCImpl (char* pPath); // From CCompatTest.c

bool CreateCoreFromC (const std::string& corePath)
{
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

std::map<std::string, std::function<bool (const std::string&)>> g_operations = {
	{ "CreateCore", CreateCoreFile },
	{ "CreateCoreFromC", CreateCoreFromC },
	{ "CrashInvalidPtrWrite", CrashInvalidPtrWrite },
	{ "CrashNullPtrCall", CrashNullPtrCall },
	{ "CrashInvalidPtrCall", CrashInvalidPtrCall },
	{ "CrashNonExecutablePtrCall", CrashNonExecutablePtrCall },
	{ "AbortPureVirtualCall", AbortPureVirtualCall },
};

void PrintUsage (const char* argv0)
{
	std::cout << "Usage: " << argv0 << " <Operation> <IP|OOP> <MainThread|BackgroundThread> <CorePath>" << std::endl;
	std::cout << "Operations:" << std::endl;
	for (const auto& op : g_operations) {
		std::cout << "\t" << op.first << std::endl;
	}

	std::cout << std::endl;
}

bool PerformScenario (const std::string& operation, bool oop, bool onBackgroundThread, const std::string& corePath)
{
	// If OOP requested, launch ourself as a worker process
	if (oop)
		return LaunchOOPWorkerForOperation (operation, onBackgroundThread, corePath);

	const bool crash = (operation.find ("Crash") != std::string::npos) || (operation.find ("Abort") != std::string::npos);
	if (crash)
		SetupSignalHandler (SignalHandler);

	auto& opFn = g_operations.find (operation)->second;
	if (onBackgroundThread) {
		std::thread t ([&] () {
			opFn (corePath);
		});

		t.join ();

		return true;
	} else {
		return opFn (corePath);
	}

	return !crash;
}

bool PerformOperationOOP (const std::string& operation, bool onBackgroundThread, const std::string& corePath)
{
	const bool crash = (operation.find ("Crash") != std::string::npos) || (operation.find ("Abort") != std::string::npos);
	if (crash)
		SetupSignalHandler (SignalHandlerForOOPWorker);

	auto& opFn = g_operations.find (operation)->second;
	if (onBackgroundThread) {
		std::thread t ([&] () {
			opFn (corePath);
		});

		t.join ();

		return true;
	} else {
		return opFn (corePath);
	}

	return !crash; // Unreachable in case of a crash
}

} // namespace

int main (int argc, char* argv[])
{
	if (argc != 5) {
		PrintUsage (argv[0]);

		return 1;
	}

	// Create a few threads so the dump contains more state
	SetupMiscThreads ();

	// The first call to syslog allocates some port refs, so we explicitly make a (probably first) call here
	syslog (LOG_NOTICE, "dumpTester started");

	// "OOP worker mode" is not exposed directly, it's a technical detail. Parameters have different positions in
	// this mode. Cherry-pick that case first.
	if (std::string (argv[1]) == "OOPWorker") {
		const std::string operation				 = argv[2];
		const std::string mainOrBackgroundThread = argv[3];
		const std::string corePath				 = argv[4];

		if (mainOrBackgroundThread != "MainThread" && mainOrBackgroundThread != "BackgroundThread") {
			std::cerr << "Unknown thread type: " << mainOrBackgroundThread << std::endl;
			PrintUsage (argv[0]);

			return 1;
		}

		if (g_operations.find (operation) == g_operations.end ()) {
			std::cerr << "Unknown operation: " << operation << std::endl;
			PrintUsage (argv[0]);

			return 1;
		}

		return PerformOperationOOP (operation, mainOrBackgroundThread == "BackgroundThread", corePath) ? 0 : 1;
	}

	const std::string operation				 = argv[1];
	const std::string oopOrIP				 = argv[2];
	const std::string mainOrBackgroundThread = argv[3];
	g_corePath								 = argv[4];

	if (g_operations.find (operation) == g_operations.end ()) {
		std::cerr << "Unknown operation: " << operation << std::endl;
		PrintUsage (argv[0]);

		return 1;
	}

	if (oopOrIP != "IP" && oopOrIP != "OOP") {
		std::cerr << "Unknown process type: " << oopOrIP << std::endl;
		PrintUsage (argv[0]);

		return 1;
	}

	if (mainOrBackgroundThread != "MainThread" && mainOrBackgroundThread != "BackgroundThread") {
		std::cerr << "Unknown thread type: " << mainOrBackgroundThread << std::endl;
		PrintUsage (argv[0]);

		return 1;
	}

	if (!PerformScenario (operation, oopOrIP == "OOP", mainOrBackgroundThread == "BackgroundThread", g_corePath)) {
		std::cerr << "Operation " << operation << " failed" << std::endl;

		return 1;
	}
}