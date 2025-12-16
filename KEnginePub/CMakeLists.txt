cmake_minimum_required(VERSION 3.29.7)

set(MODULE_NAME "KEnginePub")

file(GLOB MODULE_SRC CONFIGURE_DEPENDS
    Private/*.h
    Private/*.cpp
    Private/comm/*.h
    Private/comm/*.cpp
    Private/dx12/*.h
    Private/dx12/*.cpp
    Private/dx12/DMA_2.0.0/*.h
    Private/dx12/DMA_2.0.0/*.cpp
    Private/loader/*.h
    Private/loader/*.cpp
    Private/loader/image/*.h
    Private/loader/image/*.cpp
    Private/loader/image/etcpak/*.h
    Private/loader/image/etcpak/*.cpp
    Private/profile/*.h
    Private/profile/*.cpp
    Private/profile/microprofile/*.h
    Private/profile/microprofile/*.cpp
    Private/profile/microprofile/stb/*.h
    Private/profile/microprofile/stb/*.cpp
    Private/recorder/*.h
    Private/recorder/*.cpp
    Private/resource/*.h
    Private/resource/*.cpp
    Private/shader/*.h
    Private/shader/*.cpp
    Private/switchoption/*.h
    Private/switchoption/*.cpp
    Private/vulkan/*.h
    Private/vulkan/*.cpp
    Private/vulkan/aftermath/*.cpp
    Private/vulkan/aftermath/*.h

    Public/*.h
    Public/switchoption/*.cpp
    Public/vulkan/*.h
)

if(ANDROID)
    file(GLOB REMOVE_SRC CONFIGURE_DEPENDS
        Private/comm/*.h
        Private/comm/*.cpp
        Private/dx12/*.h
        Private/dx12/*.cpp
        Private/dx12/DMA_2.0.0/*.h
        Private/dx12/DMA_2.0.0/*.cpp
    )
    list(REMOVE_ITEM MODULE_SRC ${REMOVE_SRC})
endif()

# 所有目标默认开启警告即错误
if(MSVC)
    add_compile_options(/W3 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

kg_add_library(${MODULE_NAME} STATIC ${MODULE_SRC})
# 只有x86/x64平台需要定义__SSE4_1__
if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE
        NOMINMAX
        __SSE4_1__
    )
endif()

if(WIN32)
    target_link_libraries(${MODULE_NAME}
        PRIVATE dxguid
        $<$<CONFIG:Debug>:DirectXTex>
        $<$<CONFIG:Release>:DirectXTex>
        dxcompiler
        GFSDK_Aftermath_Lib.x64
        $<$<CONFIG:Debug>:WinPixEventRuntime>
	nvapi64
    )

    target_link_directories(${MODULE_NAME}
        PUBLIC $<$<CONFIG:Debug>:${KG_ROOT_CODE_PATH}/3rd/DirectXTex/build2/lib/Debug>
        PUBLIC $<$<CONFIG:Release>:${KG_ROOT_CODE_PATH}/3rd/DirectXTex/build2/lib/Release>
        PUBLIC $<$<CONFIG:Debug>:${KG_ROOT_CODE_PATH}/3rd/PIX/bin/x64>
        PUBLIC ${KG_ROOT_CODE_PATH}/3rd/dxc/dxc_2025_02_20/lib/x64
        PUBLIC ${KG_ROOT_CODE_PATH}/3rd/aftermath-2025.4.0.25209/lib/x64
	PUBLIC ${KG_ROOT_CODE_PATH}/3rd/nvapi/amd64
    )
endif()

target_include_directories(${MODULE_NAME}
    PRIVATE ${engine_include_dirs}
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/zlib-1.2.8
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/rapidjson-2025.4.15/include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/vulkan/windows/include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/gli
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/gli/external
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/vulkan/windows/include/glslang/Include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/vulkan/windows/include/glslang/Public
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/vulkan/windows/include/glslang/SPIRV
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/FreeImage-3.17.0/include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd
    PRIVATE ${KG_ROOT_CODE_PATH}/Engine
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/dxc
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/DirectXTex
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/PIX/Include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/dxc/dxc_2025_02_20/inc
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/aftermath-2025.4.0.25209/include
    PRIVATE ${KG_ROOT_CODE_PATH}/3rd/nvapi
)
