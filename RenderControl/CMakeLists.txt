################  Solution CMake File ################

################  全局信息设置        ################
# CMake 所需版本号
cmake_minimum_required(VERSION 2.8)

# solution名字
set(DOME_SOLUTIONNAME rc)

# 动态链接库还是静态库
set(BUILD_SHARED_LIBS TRUE CACHE BOOL "BUILD SHARED LIB?")
set(CMAKE_CONFIGURATION_TYPES Debug CACHE STRING "build configuration")
set(CMAKE_SUPPRESS_REGENERATION TRUE CACHE BOOL "Disable Zero Check Project")

# 所用编译工具名称，这个名称将被用来搜索外部库
set(DOME_IDE DOME_IDE_VISUALSTUDIO)
set(DOME_IDENAME "vs2012")
set(DOME_OS  DOME_OS_WINDESKTOP)
set(DOME_OSNAME "windt")
set(DOME_ARCH DOME_ARCH_X64)
set(DOME_ARCHNAME "x64")
set(DOME_LINK_STATIC_EXTERNAL_LIB 0)

string(FIND ${CMAKE_CONFIGURATION_TYPES} "Debug" _DebugStrPos)
if(${_DebugStrPos} EQUAL 0)
set(CMAKE_CONFIGURATION_TYPES "Debug")
set(DOME_ISDEBUG TRUE)
else()
set(CMAKE_CONFIGURATION_TYPES "Release")
set(DOME_ISDEBUG FALSE)
endif()

if(DOME_LINK_STATIC_EXTERNAL_LIB)
    if(DOME_ISDEBUG)
        set(DOME_CONFIGURATIONNAME "debuglib")
    else()
        set(DOME_CONFIGURATIONNAME "releaselib")
    endif()
else()
    if(DOME_ISDEBUG)
        set(DOME_CONFIGURATIONNAME "debug")
    else()
        set(DOME_CONFIGURATIONNAME "release")
    endif()
endif()

#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
SET( CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
SET( CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
SET( CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
SET( CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")
SET( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
SET( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")

project(${DOME_SOLUTIONNAME} CXX)

################  对于每个工程的操作  ################
# 设置工程目录的名字
set(DOME_PROJECTDIR domecore)
set(DOME_INCLUDEDIRS             
			"${CMAKE_BINARY_DIR}" 
            "${CMAKE_SOURCE_DIR}/external/${DOME_IDENAME}/${DOME_OSNAME}/${DOME_ARCHNAME}/${DOME_CONFIGURATIONNAME}_2012/include"
            "${CMAKE_SOURCE_DIR}/external/include"
	)
set(DOME_LINKDIRS 
			"${CMAKE_SOURCE_DIR}/external/${DOME_IDENAME}/${DOME_OSNAME}/${DOME_ARCHNAME}/${DOME_CONFIGURATIONNAME}_2012/lib"
	)
set(DOME_LINKLIBS 
			""
	)
set(DOME_DEFINES
            ""
    )
set(DOME_ISEXECUTABLE 0)
# 加入所有的工程目录
add_subdirectory(${DOME_PROJECTDIR})

# 设置工程目录的名字
set(DOME_PROJECTDIR rc)
set(DOME_INCLUDEDIRS             
			"${CMAKE_BINARY_DIR}" 
            "${CMAKE_SOURCE_DIR}/external/${DOME_IDENAME}/${DOME_OSNAME}/${DOME_ARCHNAME}/${DOME_CONFIGURATIONNAME}_2012/include"
            "${CMAKE_SOURCE_DIR}/external/include"
			"${CMAKE_SOURCE_DIR}/domecore"
	)
set(DOME_LINKDIRS 
			"${CMAKE_SOURCE_DIR}/external/${DOME_IDENAME}/${DOME_OSNAME}/${DOME_ARCHNAME}/${DOME_CONFIGURATIONNAME}_2012/lib"
	)
set(DOME_LINKLIBS 
			"domecore"
	)
set(DOME_DEFINES
            ""
    )
set(DOME_ISEXECUTABLE 0)
# 加入所有的工程目录
add_subdirectory(${DOME_PROJECTDIR})

add_subdirectory("tools/folder2index")

