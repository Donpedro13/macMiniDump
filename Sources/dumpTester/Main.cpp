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
	
	std::cout << "Sleeping..." << std::endl;

	for (int i = 50; i > 0; i--) {
		sleep(1);
		std::cout << i << " " << std::flush;
	}
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
	
	MMD::FileOStream fos ("/usr/local/src/macMiniDump/dump/own.core");
	

	if(!MMD::MiniDumpWriteDump (mach_task_self (), &fos)) {
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
