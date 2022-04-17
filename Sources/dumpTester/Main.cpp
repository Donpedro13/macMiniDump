#include <mach/mach.h>

#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

#include "MacMiniDump.hpp"
#include "FileOStream.hpp"

const std::string global1 = "This is a string!";
std::string global2 = "Another string!";
int global3 = 42;

void Function3 ()
{
	std::cout << global3 << std::endl;
	
	volatile int local = global3 * 2;
	std::cout << local << std::endl;
	
	sleep (50);
}

void Function2 ()
{
	std::cout << global2 << std::endl;
}

void Function1 ()
{
	global2 = "Tee-hee";
	
	Function2 ();
	
	std::thread t1 (Function3);
	
	sleep (2);
	
	MMD::FileOStream fos ("/Volumes/Dev/Dev/own.core");
	MMD::MiniDumpWriteDump (mach_task_self (), &fos);
	
	global3 = 2;
	
	t1.join ();
}

int main ()
{
    std::cout << "Hello, world!" << std::endl;
	
	Function1 ();
}
