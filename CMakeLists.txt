cmake_minimum_required(VERSION 3.20)
project(gpushift VERSION 0.1.0
        DESCRIPTION "GPU mode manager for Linux"
        HOMEPAGE_URL "https://github.com/pedroc2892/gpushift"
        LANGUAGES C)

option(GPUSHIFT_BUILD_GUI "Build the Qt6 graphical interface" ON)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>
                    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
                    $<$<CONFIG:Debug>:-fno-sanitize-recover=all>)
add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)

add_subdirectory(src)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
