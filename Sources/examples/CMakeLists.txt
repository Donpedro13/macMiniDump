SET(examples_sources
		CoreDump.cpp
        CoreDumpOfACrash.cpp
		)

ADD_LIBRARY(examples OBJECT ${examples_sources})

SOURCE_GROUP(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${examples_sources})

TARGET_INCLUDE_DIRECTORIES(examples PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

TARGET_LINK_LIBRARIES(examples macMiniDump)