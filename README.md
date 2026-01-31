# macMiniDump

macMiniDump is a static library providing programmatic access to creating minimal size, post-mortem debuggable memory dumps (core files) on macOS. In other words, it's [`MiniDumpWriteDump`](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump), but for macOS.

## Usage

Include the main header file, and call `MiniDumpWriteDump`. See [CoreDump.cpp](Sources/examples/CoreDump.cpp) for a minimal example.

### Crashes

One of the most frequent use-cases of memory dumps is post-mortem analysis of crashes. This is supported, but additional data must be provided for the library:
* register state of the crash
* the (pthread) id of the crashing thread

All of this can be achieved with a signal handler, see [CoreDumpOfACrash.cpp](Sources/examples/CoreDumpOfACrash.cpp).

Note that a crashing process might have corrupt state, so the success of core file creation is not guaranteed in this case.

### Core file creation of another process

This library supports the creation of core files of other processes. However, due to strict security rules, this (`task_for_pid`) is generally blocked by macOS.

To make this work, your target process can voluntarily transfer the necessary (so-called) mach port of its own task using some IPC machinery. See [this source file](https://github.com/chromium/crashpad/blob/main/util/mach/child_port_handshake.cc) from the crashpad project, and   [`NSMachBootstrapServer`]([Sources/examples/CoreDumpOfACrash.cpp](https://developer.apple.com/documentation/foundation/nsmachbootstrapserver)) as a reference.

## Building

The project is self-contained: no special environment, no 3rd party dependencies. The only requirements for building is a working compiler and CMake.

## Limitations

* Currently, only minimal-size core files are supported: that is, only the stack memory of threads is included with other necessary structures, such as the list of threads.
* When a process creates a core dump of itself, the thread invoking core file creation won't have its proper state recorded.
* Since Apple's platforms lack the necessary infrastructure (e.g. system binaries are not provided for download, and let's not even talk about the lack of a symbol/binary/source server technology...), symbols for binaries that you don't have the exact version of (most notably, system binaries) will not show up in call stacks, if a core file is opened on a different system that it was captured on.
* Capturing a core file of an other process with a different architecture is not supported.
* While x86-64 is supported, this library has several limitations with that architecture, resulting in reduced functionality. Support will be removed altogether in a future version.
  * The stackwalking code necessary to determine which pieces of memory containing code is saved in core files in certain situations skips some frames. This sometimes makes backtraces incomplete when opening core files in LLDB.
  * Recent versions of LLDB are unable to process exception register state from x86-64 core files.
  * Overall, this architecture receives much less usage and testing, so chances are there are bugs.

## FAQ

__Q:__ __Why the weird name for the core file creation function?__  
__A:__ The function has the same name as its Windows counterpart. It's a tribute to a venerable legend.

__Q:__ __Why would anyone want to use this, instead of using [breakpad](https://github.com/google/breakpad) to create a memory dump?__  
__A:__ breakpad creates dumps in a non-native format (by the way, the Windows minidump format...), so you can't post-mortem debug those using a debugger.

__Q:__ __Why would anyone want to use this, instead of using LLDB to create a memory dump?__  
__A:__ LLDB is a debugger, while this library provides _programmatic_ access to this functionality.

__Q:__ __Are there any notable differences in the core files created by this library compared to LLDB?__  
__A:__ Core files created by this library are similar to those created by LLDB's `process save-core -s stack` command. However, LLDB tries really hard to keep the output size of the created file minimal, even if this means sacrificing usability. This library prioritizes usability, so sometimes its created core files are a bit bigger.

## License

See [LICENSE.txt](LICENSE.txt)