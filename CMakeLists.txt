CMAKE_MINIMUM_REQUIRED(VERSION 3.16.0 FATAL_ERROR)

# Check platform
IF(NOT APPLE)
	MESSAGE(FATAL_ERROR "macMiniDump can only be built on macOS!")
ENDIF()

PROJECT(macMiniDump)

# Global CMake configurations
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)
#SET_PROPERTY(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT etwprof)

# Build configurations
SET(CMAKE_CONFIGURATION_TYPES
	Debug
	Release)

SET(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING
    "Configurations"
    FORCE)

# Overwrite "default" C++ compiler flags, set new ones
# (No need to do the same for linker flags, as on Windows
#  they contain the architecture only TODO?)
STRING(CONCAT CXX_FLAGS_IMPL
			  "-Wall "				# Turn on all warnings
			  "-Werror "			# Treat warnings as errors
			  "-g "				    # Complete debug info
			  "-std=c++17 "		    # Resort to C++17 (for now) TODO how about C?
			  )
SET(CMAKE_CXX_FLAGS	${CXX_FLAGS_IMPL})

# Add default linker flags
STRING(CONCAT EXE_LINKER_FLAGS_IMPL
			  )
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EXE_LINKER_FLAGS_IMPL}")

# Compile flags for all debug-like builds
STRING(CONCAT BASE_DEBUG_FLAGS_IMPL
			  "-O0 "			# Disable optimization
			  "-D _DEBUG "
			  )
SET(BASE_DEBUG_FLAGS ${BASE_DEBUG_FLAGS_IMPL})

# Compile flags for all release-like builds
STRING(CONCAT BASE_RELEASE_FLAGS_IMPL
			  "-O2 "
			  "/D NDEBUG "
			  )
SET(BASE_RELEASE_FLAGS ${BASE_RELEASE_FLAGS_IMPL})

# Configuration specific compiler and linker options
SET(CMAKE_CXX_FLAGS_DEBUG "${BASE_DEBUG_FLAGS} ")
SET(CMAKE_CXX_FLAGS_RELEASE "${BASE_RELEASE_FLAGS} ")

SET(CMAKE_EXE_LINKER_FLAGS_DEBUG "")
SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "")

# Set global directory for build output
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/Binaries")
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/Binaries")
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/Binaries")

ADD_SUBDIRECTORY(Sources)