SET(macMiniDump_sources
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/MMD/MacMiniDump.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/MMD/IRandomAccessBinaryOStream.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/MMD/FileOStream.hpp

		${CMAKE_CURRENT_SOURCE_DIR}/Private/MacMiniDump.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/IRandomAccessBinaryOStream.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/FileOStream.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.inl
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachPortSendRightRef.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachPortSendRightRef.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/DataAccess.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/DataAccess.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ModuleList.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ModuleList.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ReadProcessMemory.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ReadProcessMemory.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreInternal.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreInternal.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MemoryRegionList.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MemoryRegionList.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ProcessMemoryReaderDataPtr.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/ProcessMemoryReaderDataPtr.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/StackWalk.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/StackWalk.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/StackFrame.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/StackFrame.hpp

		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/Defer.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/Logging.hpp
		)

ADD_LIBRARY(macMiniDump ${macMiniDump_sources})

SOURCE_GROUP(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${macMiniDump_sources})

TARGET_INCLUDE_DIRECTORIES(macMiniDump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Private ${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Includes)