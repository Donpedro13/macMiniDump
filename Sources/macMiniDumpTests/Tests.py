import argparse
import os
import sys
import subprocess

from dataclasses import dataclass, replace, fields
from typing import Optional

sys.path.append(subprocess.check_output(["xcrun", "lldb", "-P"], stderr=subprocess.STDOUT).decode().strip())

import lldb

dumpTester_path = ""

def GetDebugger() -> lldb.SBDebugger:
    # Create a single global debugger instance
    global _debugger_instance
    try:
        _debugger_instance
    except NameError:
        _debugger_instance = None

    if _debugger_instance is None:
        _debugger_instance = lldb.SBDebugger.Create()
        _debugger_instance.SetAsync(False)
        error_file = open(os.devnull, 'w')
        _debugger_instance.SetErrorFileHandle(error_file, False)

    return _debugger_instance

def DebugProcessWithCoreFile (exe_path: str, core_path: str) -> lldb.SBProcess:
    debugger = GetDebugger()
    target = debugger.CreateTarget(exe_path)
    
    error = lldb.SBError()
    process = target.LoadCore(core_path, error)
    if not process.IsValid():
        raise RuntimeError(f"Failed to load core file: {core_path}, error: {error.GetCString()}")

    return process

def Init():
    lldb.SBDebugger.Initialize()

def Deinit():
    lldb.SBDebugger.Terminate()

def RunDumpTester(operation: str, dump_path: str) -> bool:
    import signal

    global dumpTester_path
    process = subprocess.Popen(
        [dumpTester_path, operation, dump_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    _, status = os.waitpid(process.pid, 0)   # returns (pid, status) like wait(2)

    if "crash" in operation:
        if not os.WIFSIGNALED(status):
            raise RuntimeError(f"dumpTester did not finish with a signal: {status}")
        
        if os.WTERMSIG(status) != signal.SIGKILL:
            raise RuntimeError(f"dumpTester finished with a signal, but it was not SIGKILL {status}")
    else:
        if not os.WIFEXITED(status):
            raise RuntimeError(f"dumpTester did not finish with exit: {status}")
        
        if os.WEXITSTATUS(status) != 0:
            raise RuntimeError(f"dumpTester finished with non-zero exit code: {status}")
        

testcases = {}

# Decorator for test cases
def testcase(fixture, name: str, operation: str):
    if fixture is None:
        raise ValueError("fixture must be provided for a testcase")

    def decorator(func):
        # Store tests grouped by fixture instance
        if fixture not in testcases:
            testcases[fixture] = []

        testcases[fixture].append({
            'name': name,
            'function': func,
            'operation': operation,
        })

        func.testcase_name = name
        func.testcase_operation = operation
        func.testcase_fixture = fixture

        return func

    return decorator

def CreateLLDBProcessForCoreFile(core_path: str) -> lldb.SBProcess:
    global dumpTester_path
    if not os.path.isfile(core_path):
        raise FileNotFoundError(f"Core file not found at path: {core_path}")
    
    process = DebugProcessWithCoreFile(dumpTester_path, core_path)
    if not process.IsValid():
        raise RuntimeError("Failed to load core file into debugger")
    
    return process

class CoreFileTestFixture:
    def Setup(self):
        import tempfile
        import uuid

        self.tempdir_path = tempfile.mkdtemp()
        self.core_path = os.path.join(self.tempdir_path, f"{uuid.uuid4()}.core")

    def Teardown(self):
        import shutil

        shutil.rmtree(self.tempdir_path)

    def Name(self) -> str:
        return "CorefileTests"
    
@dataclass
class CoreFileTestExpectation:
    n_threads : Optional[int] = None
    crash : bool = False
    crashed_thread_index : Optional[int] = None
    
    def __or__(self, other: 'CoreFileTestExpectation') -> 'CoreFileTestExpectation':
        """Compose two expectations using the | operator. Non-default fields from 'other' override 'self'."""
        updates = {}
        for field in fields(self):
            other_value = getattr(other, field.name)
            # Override if the value in 'other' is different from the default
            if other_value != field.default:
                updates[field.name] = other_value
        
        return replace(self, **updates)

EXPECTATION_BASE = CoreFileTestExpectation(n_threads=3)
EXPECTATION_CRASH = EXPECTATION_BASE | CoreFileTestExpectation(crash=True, crashed_thread_index=0)

def VerifyCoreFile(core_path: str, expectation: CoreFileTestExpectation):
    process = CreateLLDBProcessForCoreFile(core_path)
    # Check if the process is stopped
    if process.GetState() != lldb.eStateStopped:
        raise RuntimeError("Process is not in stopped state after loading core file")
    
    if expectation.n_threads is not None:
        n_threads = process.GetNumThreads()
        if n_threads != expectation.n_threads:
            raise RuntimeError(f"Expected {expectation.n_threads} threads, but found {n_threads}")
        
    if expectation.crash:       
        # Enumerate all threads, and check if one has a stop reason of exception
        i_exception_thread = -1
        for i in range(process.GetNumThreads()):
            thread = process.GetThreadAtIndex(i)
            if thread.GetStopReason() == lldb.eStopReasonException:
                i_exception_thread = i
                break
        
        if i_exception_thread == -1:
            raise RuntimeError("No thread with stop reason exception found")
        
        if expectation.crashed_thread_index is not None:
            if expectation.crashed_thread_index != i_exception_thread:
                raise RuntimeError(f"Expected crashed thread at index {expectation.crashed_thread_index}, but found at {i_exception_thread}")

corefile_test_fixture = CoreFileTestFixture()

@testcase(fixture=corefile_test_fixture, name="CrashOnMainThread", operation="crashonmainthread")
def CoreFileOnMainThread():
    RunDumpTester(CoreFileOnMainThread.testcase_operation, CoreFileOnMainThread.testcase_fixture.core_path)

    VerifyCoreFile(CoreFileOnMainThread.testcase_fixture.core_path, EXPECTATION_BASE | EXPECTATION_CRASH)

@testcase(fixture=corefile_test_fixture, name="MainThread", operation="mainthread")
def MainThread():
    RunDumpTester(MainThread.testcase_operation, MainThread.testcase_fixture.core_path)

    VerifyCoreFile(MainThread.testcase_fixture.core_path, EXPECTATION_BASE)


def RunTests():
    for fixture, tests in testcases.items():
        for test in tests:
            test_name = test['name']
            test_function = test['function']
            test_operation = test['operation']

            print(f"{fixture.Name()}::{test_name}")

            fixture.Setup()

            try:
                test_function()
            except Exception as e:
                print(f"\t[FAILED] {e}")
                
                continue
            finally:
                fixture.Teardown()

            print(f"\t[OK]")


def Main():
    parser = argparse.ArgumentParser(description='Run macMiniDump tests with dumpTester binary')
    parser.add_argument('dumpTester_path', help='Path to the dumpTester binary')
    args = parser.parse_args()

    global dumpTester_path
    dumpTester_path = args.dumpTester_path
    
    Init()
    RunTests()
    Deinit()

if __name__ == "__main__":
    Main()