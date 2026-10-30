cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(SvenBXTMovement LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Requires Windows MSVC x86 (-A Win32)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(METAHOOK_SDK_DIR "" CACHE PATH "Checkout of MetaHookSv/MetaHook")
set(VGUI2EXTENSION_SDK_DIR "" CACHE PATH "Checkout of MetaHookSv/VGUI2Extension (public interfaces)")
if(NOT EXISTS "${METAHOOK_SDK_DIR}/include/metahook.h")
    message(FATAL_ERROR "Set METAHOOK_SDK_DIR to MetaHookSv/MetaHook checkout")
endif()
if(NOT EXISTS "${VGUI2EXTENSION_SDK_DIR}/include/Interface/IVGUI2Extension.h")
    message(FATAL_ERROR "Set VGUI2EXTENSION_SDK_DIR to the VGUI2Extension checkout")
endif()
add_library(SvenBXTMovement SHARED src/plugin.cpp
    "${METAHOOK_SDK_DIR}/include/HLSDK/common/interface.cpp")
target_include_directories(SvenBXTMovement PRIVATE
    "${METAHOOK_SDK_DIR}/include"
    "${METAHOOK_SDK_DIR}/include/Interface"
    "${METAHOOK_SDK_DIR}/include/Interface/VGUI"
    "${METAHOOK_SDK_DIR}/include/HLSDK/common"
    "${METAHOOK_SDK_DIR}/include/HLSDK/engine"
    "${METAHOOK_SDK_DIR}/include/HLSDK/cl_dll"
    "${METAHOOK_SDK_DIR}/include/HLSDK/pm_shared"
    "${METAHOOK_SDK_DIR}/include/HLSDK/public"
    "${METAHOOK_SDK_DIR}/include/SourceSDK"
    "${METAHOOK_SDK_DIR}/include/SourceSDK/tier0"
    "${METAHOOK_SDK_DIR}/include/SourceSDK/tier1"
    "${METAHOOK_SDK_DIR}/include/SourceSDK/tier3"
    "${METAHOOK_SDK_DIR}/include/SourceSDK/vstdlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(SvenBXTMovement PRIVATE WIN32 _WINDOWS _USRDLL NOMINMAX NO_MALLOC_OVERRIDE _CRT_SECURE_NO_WARNINGS)
target_compile_options(SvenBXTMovement PRIVATE /W3 /EHsc /MP /utf-8)
set_target_properties(SvenBXTMovement PROPERTIES OUTPUT_NAME SvenBXTMovement)
install(TARGETS SvenBXTMovement RUNTIME DESTINATION svencoop/metahook/plugins)

# VGUI interfaces are resolved from already loaded modules at runtime.
# No engine calls or panel creation during LoadEngine/LoadClient.
include(cmake/HudEditVguiSources.cmake)
add_library(SvenBXTHudEdit SHARED
    src/hudedit.cpp
    src/hudedit_options.cpp
    ${HUDEDIT_VGUI_SOURCES}
    "${METAHOOK_SDK_DIR}/include/HLSDK/common/interface.cpp")
get_target_property(_svenbxt_include_dirs SvenBXTMovement INCLUDE_DIRECTORIES)
target_include_directories(SvenBXTHudEdit PRIVATE ${_svenbxt_include_dirs})
target_include_directories(SvenBXTHudEdit BEFORE PRIVATE
    "${VGUI2EXTENSION_SDK_DIR}/include/Interface"
    "${VGUI2EXTENSION_SDK_DIR}/include/Interface/VGUI")
target_include_directories(SvenBXTHudEdit PRIVATE
    "${METAHOOK_SDK_DIR}/include/VGUI"
    "${METAHOOK_SDK_DIR}/include/vgui_controls")
target_compile_definitions(SvenBXTHudEdit PRIVATE
    WIN32 _WINDOWS _USRDLL NOMINMAX NO_MALLOC_OVERRIDE NO_TIER0_MINIDUMP NO_VCR
    VGUI_USE_SURFACE2 VGUI_USE_SCHEME2 VGUI_USE_INPUT2 _CRT_SECURE_NO_WARNINGS)
target_compile_options(SvenBXTHudEdit PRIVATE /W3 /EHsc /MP /utf-8)
set_target_properties(SvenBXTHudEdit PROPERTIES OUTPUT_NAME SvenBXTHudEdit)
install(TARGETS SvenBXTHudEdit RUNTIME DESTINATION svencoop/metahook/plugins)

# DLL import and factory tests run before uploading the release assets.
add_executable(SvenBXTLoadSmoke tests/plugin_load_smoke.cpp)
target_include_directories(SvenBXTLoadSmoke PRIVATE ${_svenbxt_include_dirs})
target_compile_definitions(SvenBXTLoadSmoke PRIVATE WIN32 _WINDOWS NOMINMAX NO_MALLOC_OVERRIDE)
target_compile_options(SvenBXTLoadSmoke PRIVATE /W3 /utf-8)
enable_testing()
add_test(NAME client_lifecycle COMMAND SvenBXTLoadSmoke
    "$<TARGET_FILE:SvenBXTMovement>" "$<TARGET_FILE:SvenBXTHudEdit>")
