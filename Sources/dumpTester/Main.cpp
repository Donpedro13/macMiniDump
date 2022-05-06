#include <mach/mach.h>

#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>
#include <fcntl.h>

#include "MacMiniDump.hpp"
#include "FileOStream.hpp"

const std::string global1 = "This is a string!";
std::string global2 = "Another string!";
int global3 = 42;

volatile int a = 0;

__attribute__((noinline)) void BusyWait ()
{
	for (size_t i = 0; i < 500'000'000'0; ++i)
		a *= 2;
}

void Function3 ()
{
	std::cout << global3 << std::endl;
	
	volatile int local = global3 * 2;
	std::cout << local << std::endl;
	
	sleep (50);
	//BusyWait ();
}

void Function2 ()
{
	std::cout << global2 << std::endl;
}

void Function1 ()
{
	global2 = "Tee-hee";
	
	int fd = open ("/Volumes/Dev/Dev/own.core", O_WRONLY);
	/*int flags = fcntl (fd, F_GETFD);
	int res = write (fd, "a", 2);
	printf ("%d", res);
	printf ("%d\n", flags);*/
	//close (fd);
	
	Function2 ();
	
	std::thread t1 (Function3);
	
	sleep (2);
	
	//MMD::FileOStream fos ("/Volumes/Dev/Dev/own.core");
	

	//if(!MMD::MiniDumpWriteDump (mach_task_self (), &fos)) {
	if(!MMD::MiniDumpWriteDump (mach_task_self (), fd)) {
		std::cout << "MMD::MiniDumpWriteDump() has returned false" << std::endl;
	} else {
		std::cout << "Dump written." << std::endl;
	}
	
	global3 = 2;

	std::exit(0);
}

int main ()
{
    std::cout << "Hello, world!" << std::endl;
	
	Function1 ();
}
