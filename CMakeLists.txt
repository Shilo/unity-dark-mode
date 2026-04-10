cmake_minimum_required(VERSION 3.20)

project(UnityDarkMode LANGUAGES CXX ASM_MASM)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

add_library(darkmode_proxy SHARED
    src/dllmain.cpp
    src/version_proxy.cpp
    src/version_proxy.asm
    src/darkmode.cpp
    src/version.def
)

target_include_directories(darkmode_proxy PRIVATE src)

if(MSVC)
  target_compile_definitions(darkmode_proxy PRIVATE NOMINMAX UNICODE _UNICODE)
  target_compile_options(darkmode_proxy PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/MP>)
endif()

# Output as version.dll (the proxy target)
set_target_properties(darkmode_proxy PROPERTIES OUTPUT_NAME "version")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT darkmode_proxy)
