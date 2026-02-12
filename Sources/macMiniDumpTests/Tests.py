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

    if ("Crash" in operation or "Abort" in operation) and not is_oop:
        if not os.WIFSIGNALED(status):
            raise RuntimeError(f"dumpTester did not finish with a signal: {status}")
        
        if os.WTERMSIG(status) != signal.SIGKILL:
            raise RuntimeError(f"dumpTester finished with a signal, but it was not SIGKILL: {status}")
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

    required_images : Optional[list] = None

    crash : bool = False
    exception_string : Optional[str] = None
    exception_fault_address : Optional[int] = None

    relevant_thread_index : Optional[int] = None
    relevant_func_name : Optional[str] = None
    relevant_func_locals : Optional[dict] = None
    relevant_frame_index : int = 0
    
    def __or__(self, other: 'CoreFileTestExpectation') -> 'CoreFileTestExpectation':
        """Compose two expectations using the | operator. Non-default fields from 'other' override 'self'."""
        updates = {}
        for field in fields(self):
            other_value = getattr(other, field.name)
            # Override if the value in 'other' is different from the default
            if other_value != field.default:
                updates[field.name] = other_value
        
        return replace(self, **updates)
    
def VerifySegmentsInCoreFile(core_path: str, callstacks: list[list]):
    # We check if every address in the callstacks is within a segment in the core file. This basically tests if stackwalking
    # works correctly in MMD or not
    try:
        output = subprocess.check_output(["otool", "-l", core_path], stderr=subprocess.STDOUT).decode('utf-8')
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to run otool: {e.output.decode('utf-8')}")

    segments = []
    is_segment = False
    vmaddr = None
    vmsize = None

    for line in output.splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            cmd = line.split()[1]
            if cmd == "LC_SEGMENT_64":
                is_segment = True
                vmaddr = None
                vmsize = None
            else:
                is_segment = False
        
        if is_segment:
            if line.startswith("vmaddr "):
                vmaddr = int(line.split()[1], 16)
            elif line.startswith("vmsize "):
                vmsize = int(line.split()[1], 16)
            
            if vmaddr is not None and vmsize is not None:
                segments.append((vmaddr, vmaddr + vmsize))
                is_segment = False
                vmaddr = None
                vmsize = None

    for i, stack in enumerate(callstacks):
        for j, address in enumerate(stack):
            if address == 0 or address == 0xfffffffffffa7b00:
                continue
            
            if isinstance(address, str):
                address = int(address, 16)
            
            found = False
            for start, end in segments:
                if start <= address < end:
                    found = True
                    break
            
            if not found:
                raise RuntimeError(f"Address {hex(address)} (stack {i}, frame {j}) not found in any core file segment")
            
    # Check if there are overlapping segments in the core file
    # In other words: not a single byte of included memory should appear more than once in the core file
    if segments:
        segments_sorted = sorted(segments, key=lambda seg: seg[0])
        prev_start, prev_end = segments_sorted[0]
        for start, end in segments_sorted[1:]:
            if start < prev_end:
                raise RuntimeError(
                    f"Overlapping core segments detected: "
                    f"[{hex(prev_start)}, {hex(prev_end)}) overlaps [{hex(start)}, {hex(end)})"
                )
            if end > prev_end:
                prev_start, prev_end = start, end

def VerifyNoOverlappingModuleSections(process: lldb.SBProcess):
    """Check that no byte of memory maps to more than one loaded section (duplicate/overlapping
    sections cause LLDB warnings). dyld is excluded as it can legitimately appear twice.
    Modules from the dyld shared cache are also excluded from pairwise overlap checks,
    because their segments are interleaved by design inside the cache."""
    target = process.GetTarget()

    # Identify shared cache modules: they all share one __LINKEDIT region, so any two
    # modules whose __LINKEDIT sections overlap are part of the shared cache.
    linkedit_ranges = []
    for mi in range(target.GetNumModules()):
        module = target.GetModuleAtIndex(mi)
        mod_name = module.GetFileSpec().GetFilename()
        for si in range(module.GetNumSections()):
            section = module.GetSectionAtIndex(si)
            if section.GetName() == "__LINKEDIT":
                load_addr = section.GetLoadAddress(target)
                if load_addr != lldb.LLDB_INVALID_ADDRESS and section.GetByteSize() > 0:
                    linkedit_ranges.append((load_addr, load_addr + section.GetByteSize(), mod_name))

    shared_cache_modules = set()
    for i in range(len(linkedit_ranges)):
        for j in range(i + 1, len(linkedit_ranges)):
            s1, e1, m1 = linkedit_ranges[i]
            s2, e2, m2 = linkedit_ranges[j]
            if s1 < e2 and s2 < e1:
                shared_cache_modules.add(m1)
                shared_cache_modules.add(m2)

    loaded_sections = []
    for mi in range(target.GetNumModules()):
        module = target.GetModuleAtIndex(mi)
        mod_name = module.GetFileSpec().GetFilename()
        if mod_name == "dyld":
            continue
        for si in range(module.GetNumSections()):
            section = module.GetSectionAtIndex(si)
            load_addr = section.GetLoadAddress(target)
            if load_addr == lldb.LLDB_INVALID_ADDRESS:
                continue
            size = section.GetByteSize()
            if size == 0:
                continue
            loaded_sections.append((load_addr, load_addr + size, mod_name, section.GetName()))

    loaded_sections.sort(key=lambda s: s[0])
    for i in range(len(loaded_sections) - 1):
        _, prev_end, prev_module, prev_section = loaded_sections[i]
        next_start, _, next_module, next_section = loaded_sections[i + 1]
        if next_start < prev_end:
            # Shared cache modules have interleaved segments by design; skip those pairs
            if prev_module in shared_cache_modules and next_module in shared_cache_modules:
                continue
            raise RuntimeError(
                f"Overlapping loaded sections in core file: "
                f"{prev_module}:{prev_section} ends at {hex(prev_end)} but "
                f"{next_module}:{next_section} starts at {hex(next_start)}"
            )

def VerifyCoreFile(core_path: str, expectation: CoreFileTestExpectation):
    # Check if reason is stopped
    process = CreateLLDBProcessForCoreFile(core_path)

    VerifyNoOverlappingModuleSections(process)

    if process.GetState() != lldb.eStateStopped:
        raise RuntimeError("Process is not in stopped state after loading core file")
    
    # Check thread count
    if expectation.n_threads is not None:
        n_threads = process.GetNumThreads()
        if n_threads != expectation.n_threads:
            raise RuntimeError(f"Expected {expectation.n_threads} threads, but found {n_threads}")
    
    # Best-effort check for presence of 'process metadata' LC_NOTE
    # If this is missing, thread IDs will be ordinals assigned by LLDB starting from 0
    n_threads = process.GetNumThreads()
    thread_ids = [process.GetThreadAtIndex(i).GetThreadID() for i in range(n_threads)]
    expected_ordinals = list(range(n_threads))
    if thread_ids == expected_ordinals:
        raise RuntimeError(f"Thread IDs appear to be ordinals {thread_ids}, indicating 'process metadata' LC_NOTE is corrupt or missing from core file")
        
    # Check if all required images are loaded
    if expectation.required_images is not None:
        target = process.GetTarget()
        loaded_images = [target.GetModuleAtIndex(i).GetFileSpec().GetFilename() for i in range(target.GetNumModules())]
        for required_image in expectation.required_images:
            if required_image not in loaded_images:
                raise RuntimeError(f"Required image '{required_image}' not found in loaded modules")
        
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
        
        if expectation.relevant_thread_index is not None:
            if expectation.relevant_thread_index != i_exception_thread:
                raise RuntimeError(f"Expected crashed thread at index {expectation.relevant_thread_index}, but found at {i_exception_thread}")
            
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
            if expectation.exception_fault_address and fault_address != expectation.exception_fault_address:
                raise RuntimeError(f"Expected fault address '{hex(expectation.exception_fault_address)}', but found '{hex(fault_address)}'")

    # Check stack and crashed function details
    i_exception_thread = -1
    callstacks = []
    for i in range(process.GetNumThreads()):
        callstack = []
        callstacks.append(callstack)
        thread = process.GetThreadAtIndex(i)
        stack_depth = thread.GetNumFrames()
        if expectation.stack_mindepth is not None:
            if stack_depth < expectation.stack_mindepth:
                raise RuntimeError(f"Thread {i} has stack depth {stack_depth}, which is less than expected minimum {expectation.stack_mindepth}")
        
        # Inlined functions are tricky, because the stack walking code in MMD is not aware of them.
        # Consider the following call stack:
        # #0: DoWork                   PC1
        # #1: Boilerplate2 (inlined)   PC2
        # #2: Boilerplate1 (inlined)   PC3
        # #3: PerformAction            PC4
        # #4: Main                     PC5
        #
        # In this case, even though #1-#3 share the same physical stack frame, stack walking code in MMD will "attribute"
        # the physical frame to #1. This will result in the following callstack: [PC1, PC2, PC5]
        # LLDB has access and will use debug info, so its callstack will include inline functions, too.
        # So we need to skip the check for:
        # - All consecutive inlined functions after the first one
        # - The first non-inlined function after a block of inlined functions
        in_inline_block = False
        for j in range(stack_depth):
            frame = thread.GetFrameAtIndex(j)
            if frame.IsInlined():
                if not in_inline_block:
                    callstack.append(frame.GetPCAddress().GetLoadAddress(process.GetTarget()))
                    in_inline_block = True
            else:
                if in_inline_block:
                    in_inline_block = False
                else:
                    callstack.append(frame.GetPCAddress().GetLoadAddress(process.GetTarget()))
        
        if expectation.relevant_func_name is not None:
            if i == expectation.relevant_thread_index:
                frame = thread.GetFrameAtIndex(expectation.relevant_frame_index)
                func_name = frame.GetFunctionName()
                if expectation.relevant_func_name not in func_name:
                    raise RuntimeError(f"Expected relevant function name '{expectation.relevant_func_name}' on thread {i}, but found '{func_name}'")
                
                if expectation.relevant_func_locals is not None:
                    for var_name, expected_value in expectation.relevant_func_locals.items():
                        var = frame.FindVariable(var_name)
                        if not var.IsValid():
                            raise RuntimeError(f"Expected local variable '{var_name}' not found in relevant function")
                        
                        var_value = var.GetValue()
                        if var_value != str(expected_value):
                            raise RuntimeError(f"Expected local variable '{var_name}' to have value '{expected_value}', but found '{var_value}'")
                        
    VerifySegmentsInCoreFile(core_path, callstacks)

corefile_test_fixture = CoreFileTestFixture()
testcases = {}
def add_testcase(fixture, name, operation, oop: bool, background_thread: bool, expectation):
    if fixture not in testcases:
        testcases[fixture] = []
    testcases[fixture].append({"name": name, "operation": operation, "oop": oop, "background_thread": background_thread, "expectation": expectation})

operations = ["CreateCore", "CreateCoreFromC", "CrashInvalidPtrWrite", "CrashNullPtrCall", "CrashInvalidPtrCall", "CrashNonExecutablePtrCall", "AbortPureVirtualCall"]
oop = [True, False]
background_thread = [True, False]

# Run all combinations
for op in operations:
    for is_oop in oop:
        # Only crashes and aborts make sense to test OOP
        if is_oop and ("Crash" not in op and "Abort" not in op):
            continue

        for is_background in background_thread:
            test_name = op
            expectation = CoreFileTestExpectation(n_threads=3, required_images=["dumpTester", "dyld", "libsystem_platform.dylib", "libdyld.dylib"])
            if is_oop:
                test_name += "_OOP"
            if is_background:
                test_name += "_BackgroundThread"
                expectation |= CoreFileTestExpectation(n_threads=4)

            if is_background:
                expectation = expectation | CoreFileTestExpectation(relevant_thread_index=3)
            else:
                expectation = expectation | CoreFileTestExpectation(relevant_thread_index=0)
            
            if "Crash" in op:
                exception_string = "ESR_EC_"
                if "Call" in op:
                    exception_string += "IABORT"
                else:
                    exception_string += "DABORT"

                exception_string += "_EL0"
                if "InvalidPtr" in op:
                    fault_address = 0xFFFFFFFFFFFA7B00
                elif "NonExecutablePtrCall" in op:
                    fault_address = None
                else:
                    fault_address = 0x0
                expectation |= CoreFileTestExpectation(crash=True, relevant_func_name=op, relevant_func_locals={"local": "20250425"}, exception_string=exception_string, exception_fault_address=fault_address)

                if "NonExecutablePtrCall" in op:
                    expectation = expectation | CoreFileTestExpectation(relevant_func_name="g_1", relevant_func_locals={})
                elif "PtrCall" in op:
                    expectation = expectation | CoreFileTestExpectation(relevant_frame_index=1)
            elif "Abort" in op:
                expectation = expectation | CoreFileTestExpectation(relevant_frame_index=2, relevant_func_name="abort")

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