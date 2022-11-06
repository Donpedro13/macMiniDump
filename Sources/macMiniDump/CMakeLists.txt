SET(macMiniDump_sources
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/MacMiniDump.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/IRandomAccessBinaryOStream.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/FileOStream.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Includes/Logging.hpp

		${CMAKE_CURRENT_SOURCE_DIR}/Private/MacMiniDump.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/IRandomAccessBinaryOStream.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/FileOStream.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/MachOCoreDumpBuilder.inl
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
		${CMAKE_CURRENT_SOURCE_DIR}/Private/WalkStack.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/WalkStack.hpp

		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/Defer.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/AddSegmentCommandFromProcessMemory.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/AddSegmentCommandFromProcessMemory.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/GetProtectionOf.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/LoggingPrivate.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils/Logging.cpp
		)

ADD_LIBRARY(macMiniDump ${macMiniDump_sources})

SOURCE_GROUP(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${macMiniDump_sources})

TARGET_INCLUDE_DIRECTORIES(macMiniDump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Private ${CMAKE_CURRENT_SOURCE_DIR}/Private/Utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Includes)