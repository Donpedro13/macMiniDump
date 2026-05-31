#include <stdint.h>

#import <Foundation/Foundation.h>

#ifndef __OBJC__
	#error This file must be compiled as Objective-C!
#endif

#ifdef __cplusplus
	#error This file must be compiled as Objective-C, not Objective-C++!
#endif

#define NOINLINE __attribute__ ((noinline))

static const uintptr_t InvalidPtr = 0xFFFFFFFFFFFA7B00ULL;

__attribute__ ((objc_root_class))
@interface MMDObjectiveCCrashFixture
+ (void) crashInvalidPtrWrite;
+ (void) raiseUnhandledException;
@end

@implementation MMDObjectiveCCrashFixture

+ (void) crashInvalidPtrWrite
{
	volatile int local = 20250425;
	(void) local;

	volatile int* p = (int*) InvalidPtr;
	*p				= 42;
}

+ (void) raiseUnhandledException
{
	@throw [NSException exceptionWithName:@"MMDObjectiveCUnhandledException" reason:@"Unhandled Objective-C exception test" userInfo:nil];
}

@end

NOINLINE int CrashInvalidPtrWriteFromObjCImpl (void)
{
	[MMDObjectiveCCrashFixture crashInvalidPtrWrite];

	return 0; // Unreachable
}

NOINLINE int RaiseUnhandledObjCExceptionImpl (void)
{
	[MMDObjectiveCCrashFixture raiseUnhandledException];

	return 0; // Unreachable
}