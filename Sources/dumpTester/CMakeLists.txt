SET(dumpTester_sources
		Main.cpp
		)

ADD_EXECUTABLE(dumpTester ${dumpTester_sources} ${3rdparty_binaries})

SOURCE_GROUP(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${dumpTester_sources})

TARGET_INCLUDE_DIRECTORIES(dumpTester PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

TARGET_LINK_LIBRARIES(dumpTester macMiniDump)