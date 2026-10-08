cmake_minimum_required(VERSION 3.20)
project(gpushift VERSION 0.1.0
        DESCRIPTION "GPU mode manager for Linux"
        HOMEPAGE_URL "https://github.com/PedroC2892/GPUShift"
        LANGUAGES C)

option(GPUSHIFT_BUILD_GUI "Build the Qt6 graphical interface" ON)
if(GPUSHIFT_BUILD_GUI)
  enable_language(CXX)
endif()
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

# Runtime configuration written by gpushift-helper. These are the
# administrator directories, which take precedence over the vendor ones.
set(GPUSHIFT_MODPROBE_DIR "/etc/modprobe.d" CACHE PATH "modprobe.d directory written at runtime")
set(GPUSHIFT_UDEV_RULES_DIR "/etc/udev/rules.d" CACHE PATH "udev rules directory written at runtime")

# polkit only reads actions from its own directory, whatever the install prefix.
find_package(PkgConfig REQUIRED)
pkg_get_variable(_polkit_policydir polkit-gobject-1 policydir)
if(NOT _polkit_policydir)
  set(_polkit_policydir "/usr/share/polkit-1/actions")
endif()
set(GPUSHIFT_POLKIT_ACTIONS_DIR "${_polkit_policydir}" CACHE PATH "polkit actions directory")

set(GPUSHIFT_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/gpushift-helper")

add_subdirectory(src)
add_subdirectory(data)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# Packages: configure with -DCMAKE_INSTALL_PREFIX=/usr, then run cpack.
set(CPACK_PACKAGE_NAME gpushift)
set(CPACK_PACKAGE_VENDOR "GPUShift")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU mode manager for Linux laptops (integrated, hybrid, dedicated)")
set(CPACK_PACKAGE_DESCRIPTION
  "GPUShift detects the GPUs of the system, shows detailed information about them and switches between the GPU modes the hardware supports. It uses only kernel interfaces (sysfs, modprobe.d, udev) and polkit.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Pedro Cruz")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "pkexec | policykit-1, pci.ids")

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Base")
set(CPACK_RPM_PACKAGE_REQUIRES "polkit, hwdata")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /usr/libexec /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
  /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
  /usr/share/polkit-1 /usr/share/polkit-1/actions)
include(CPack)
