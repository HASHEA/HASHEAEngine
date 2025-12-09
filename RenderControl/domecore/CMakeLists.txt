################  Project CMake File  ################

# 收集项目的所有源文件
set(DOME_PROJECTSOURCES "")
file(GLOB_RECURSE DOME_PROJECTSOURCES 
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} 
        *.h *.cpp *.inc
    )
# 把收集到的代码文件按所在文件夹分组
foreach(DOME_SOURCEFILE ${DOME_PROJECTSOURCES})
    get_filename_component(DOME_SOURCEPATH ${DOME_SOURCEFILE} PATH)
    if(DOME_SOURCEPATH)
        string(REPLACE "/" "\\" DOME_SOURCEGROUP ${DOME_SOURCEPATH})
		source_group(${DOME_SOURCEGROUP} FILES ${DOME_SOURCEFILE})
	endif()
endforeach()

# 在这里加入头文件目录
include_directories(${DOME_INCLUDEDIRS})

# 在这里加入库文件目录
link_directories(${DOME_LINKDIRS})

# 在这里加入工程定义宏
add_definitions(${DOME_DEFINES})

# 把 Debug Information Format 改为 Z7
if(MSVC)
    string(REGEX REPLACE "/Z[iI7]" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Z7")
    string(REGEX REPLACE "/Z[iI7]" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Z7")
endif()

# 加入工程目标
if(DOME_ISEXECUTABLE)
add_executable(${DOME_PROJECTDIR} ${DOME_PROJECTSOURCES})
else()
add_library(${DOME_PROJECTDIR} ${DOME_PROJECTSOURCES})
endif()

# 如果外部库是以静态库的方式接入，给工程加入相应的宏定义
if(DOME_LINK_STATIC_EXTERNAL_LIB)
add_definitions(-DDOME_LINK_STATIC_EXTERNAL_LIB)
endif()

# 如果是库工程，按是否是静态库，给工程加入相应的宏定义
string(TOUPPER ${DOME_PROJECTDIR} DOME_PROJECTNAMECAPITAL)
if(BUILD_SHARED_LIBS)
add_definitions(-D${DOME_PROJECTNAMECAPITAL}_EXPORTS)
else()
add_definitions(-D${DOME_PROJECTNAMECAPITAL}_AS_LIB)
add_definitions(-DDOME_MODULE_AS_LIB)
endif()

# 在这里加入需要连接的库文件，如果有的话
if(DOME_LINKLIBS)
	target_link_libraries(${DOME_PROJECTDIR} ${DOME_LINKLIBS})
endif()

# 安装生成的文件
install(
    TARGETS ${DOME_PROJECTDIR}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)