import argparse
import os
import re
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

def RunDumpTester(operation: str, is_oop: bool, background_thread: bool, core_path: str):
    import signal

    global dumpTester_path
    process = subprocess.Popen(
        [dumpTester_path, operation, "OOP" if is_oop else "IP", "BackgroundThread" if background_thread else "MainThread", core_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    _, status = os.waitpid(process.pid, 0)   # returns (pid, status) like wait(2)

    if "Crash" in operation and not is_oop:
        if not os.WIFSIGNALED(status):
            raise RuntimeError(f"dumpTester did not finish with a signal: {status}")
        
        if os.WTERMSIG(status) != signal.SIGKILL:
            raise RuntimeError(f"dumpTester finished with a signal, but it was not SIGKILL {status}")
    else:
        if not os.WIFEXITED(status):
            raise RuntimeError(f"dumpTester did not finish with exit: {status}")
        
        if os.WEXITSTATUS(status) != 0:
            raise RuntimeError(f"dumpTester finished with non-zero exit code: {status}")

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

    stack_mindepth : Optional[int] = None

    crash : bool = False
    exception_string : Optional[str] = None
    exception_fault_address : Optional[int] = None
    crashed_thread_index : Optional[int] = None
    crashed_func_name : Optional[str] = None
    crashed_func_locals : Optional[dict] = None
    crash_relevant_frame_index : int = 0
    
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
EXPECTATION_CRASH = EXPECTATION_BASE | CoreFileTestExpectation(crash=True, crashed_thread_index=0, crashed_func_name="CrashInvalidPtrWrite", crashed_func_locals={"local": "20250425"}, exception_string="ESR_EC_DABORT_EL0", exception_fault_address=0xBEEF)

def VerifyCoreFile(core_path: str, expectation: CoreFileTestExpectation):
    # Check if reason is stopped
    process = CreateLLDBProcessForCoreFile(core_path)
    if process.GetState() != lldb.eStateStopped:
        raise RuntimeError("Process is not in stopped state after loading core file")
    
    # Check thread count
    if expectation.n_threads is not None:
        n_threads = process.GetNumThreads()
        if n_threads != expectation.n_threads:
            raise RuntimeError(f"Expected {expectation.n_threads} threads, but found {n_threads}")
        
    # Check for the crashed thread, check exception details
    if expectation.crash:       
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
            
        thread = process.GetThreadAtIndex(i_exception_thread)

        if expectation.exception_string is not None:
            stop_description = thread.GetStopDescription(1024)
            match = re.search(r'(.*?) \(fault address: (0x[0-9A-Fa-f]+)\)', stop_description, re.DOTALL)
            if not match:
                raise RuntimeError("Failed to parse exception details from stop description")
            exception_string = match.group(1)
            fault_address = int(match.group(2), 16)
            if exception_string != expectation.exception_string:
                raise RuntimeError(f"Expected exception string '{expectation.exception_string}', but found '{exception_string}'")
            if fault_address != expectation.exception_fault_address:
                raise RuntimeError(f"Expected fault address '{hex(expectation.exception_fault_address)}', but found '{hex(fault_address)}'")

    # Check stack depth and crashed function details
    i_exception_thread = -1
    for i in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(i)
        stack_depth = thread.GetNumFrames()
        if expectation.stack_mindepth is not None:
            if stack_depth < expectation.stack_mindepth:
                raise RuntimeError(f"Thread {i} has stack depth {stack_depth}, which is less than expected minimum {expectation.stack_mindepth}")
        
        if expectation.crashed_func_name is not None and expectation.crash:
            if i == expectation.crashed_thread_index:
                frame = thread.GetFrameAtIndex(expectation.crash_relevant_frame_index)
                func_name = frame.GetFunctionName()
                if expectation.crashed_func_name not in func_name:
                    raise RuntimeError(f"Expected crashed function name '{expectation.crashed_func_name}' on thread {i}, but found '{func_name}'")
                
                if expectation.crashed_func_locals is not None:
                    for var_name, expected_value in expectation.crashed_func_locals.items():
                        var = frame.FindVariable(var_name)
                        if not var.IsValid():
                            raise RuntimeError(f"Expected local variable '{var_name}' not found in crashed function")
                        
                        var_value = var.GetValue()
                        if var_value != str(expected_value):
                            raise RuntimeError(f"Expected local variable '{var_name}' to have value '{expected_value}', but found '{var_value}'")

corefile_test_fixture = CoreFileTestFixture()
testcases = {}
def add_testcase(fixture, name, operation, oop: bool, background_thread: bool, expectation):
    if fixture not in testcases:
        testcases[fixture] = []
    testcases[fixture].append({"name": name, "operation": operation, "oop": oop, "background_thread": background_thread, "expectation": expectation})

operations = ["CreateCore", "CreateCoreFromC", "CrashInvalidPtrWrite", "CrashNullPtrCall"]
oop = [True, False]
background_thread = [True, False]

# Run all combinations
for op in operations:
    for is_oop in oop:
        # Only crashes make sense to test OOP
        if is_oop and "Crash" not in op:
            continue

        for is_background in background_thread:
            test_name = op
            expectation = EXPECTATION_BASE
            if is_oop:
                test_name += "_OOP"
            if is_background:
                test_name += "_BackgroundThread"
                expectation |= CoreFileTestExpectation(n_threads=4)
            
            if "Crash" in op:
                exception_string = "ESR_EC_"
                if "Call" in op:
                    exception_string += "IABORT"
                else:
                    exception_string += "DABORT"

                exception_string += "_EL0"
                fault_address = 0xBEEF if "Write" in op else 0x0
                expectation |= CoreFileTestExpectation(crash=True, crashed_func_name=op, crashed_func_locals={"local": "20250425"}, exception_string=exception_string, exception_fault_address=fault_address)

                if is_background:
                    expectation = expectation | CoreFileTestExpectation(crashed_thread_index=3)

                if "PtrCall" in op:
                    expectation = expectation | CoreFileTestExpectation(crash_relevant_frame_index=1)

            add_testcase(corefile_test_fixture, test_name, op, is_oop, is_background, expectation)

def RunTests():
    for fixture, tests in testcases.items():
        for test in tests:
            test_name = test['name']
            test_operation = test['operation']

            print(f"{fixture.Name()}::{test_name}")

            fixture.Setup()

            try:
                RunDumpTester(test_operation, test['oop'], test['background_thread'], fixture.core_path)
                VerifyCoreFile(fixture.core_path, test['expectation'])
            except Exception as e:
                if isinstance(e, (SyntaxError, TypeError)):
                    raise
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