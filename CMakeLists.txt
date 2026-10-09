cmake_minimum_required(VERSION 3.20)
project(CloudSpotlight LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CS_BUILD_TESTS "Build unit tests for portable code" ON)

# Portable code: no Windows headers, unit-tested on any OS.
file(GLOB CS_PORTABLE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/logic/*.cpp)

if(MSVC)
  add_compile_options(/utf-8 /permissive- /W4 /EHsc /Zc:__cplusplus /MP)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT, no VC++ redist needed
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /Oi /GL /Gy /DNDEBUG")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG /OPT:REF /OPT:ICF")
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O2 -DNDEBUG")
endif()

if(WIN32)
  file(GLOB CS_WIN_SOURCES CONFIGURE_DEPENDS
       src/platform/*.cpp src/app/*.cpp src/ui/*.cpp src/providers/*.cpp src/main.cpp)
  add_executable(CloudSpotlight WIN32 ${CS_PORTABLE_SOURCES} ${CS_WIN_SOURCES} res/app.rc)
  target_include_directories(CloudSpotlight PRIVATE src)
  target_compile_definitions(CloudSpotlight PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000C)
  target_link_libraries(CloudSpotlight PRIVATE
    d2d1 dwrite dwmapi windowscodecs uxtheme shell32 shlwapi ole32 oleaut32 uuid propsys
    user32 gdi32 advapi32 winhttp powrprof comctl32 imm32)
  if(MSVC)
    set_property(TARGET CloudSpotlight PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    target_link_options(CloudSpotlight PRIVATE /MANIFEST:NO)  # manifest is embedded via res/app.rc
  else()
    target_compile_options(CloudSpotlight PRIVATE -municode)
    target_link_options(CloudSpotlight PRIVATE -municode -mwindows -static -s)
  endif()
endif()

if(CS_BUILD_TESTS)
  file(GLOB CS_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(cs_tests ${CS_PORTABLE_SOURCES} ${CS_TEST_SOURCES})
  target_include_directories(cs_tests PRIVATE src tests)
  if(MINGW)
    target_link_options(cs_tests PRIVATE -static)
  endif()
  enable_testing()
  add_test(NAME cs_tests COMMAND cs_tests)
endif()
