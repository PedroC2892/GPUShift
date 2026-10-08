find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPCI REQUIRED IMPORTED_TARGET libpci)


add_library(gpushift STATIC
  lib/client.c
  lib/config.c
  lib/conflicts.c
  lib/detect.c
  lib/initramfs.c
  lib/modes.c
  lib/mux.c
  lib/pcinames.c
  lib/state.c
  lib/sysinfo.c
  lib/util.c
  lib/version.c)
target_include_directories(gpushift PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(gpushift
  PRIVATE GPUSHIFT_VERSION="${PROJECT_VERSION}"
          GPUSHIFT_HELPER_PATH="${GPUSHIFT_HELPER_PATH}"
          GPUSHIFT_DOC_DIR="${CMAKE_INSTALL_FULL_DOCDIR}"
  PUBLIC GPUSHIFT_MODPROBE_DIR="${GPUSHIFT_MODPROBE_DIR}"
         GPUSHIFT_UDEV_RULES_DIR="${GPUSHIFT_UDEV_RULES_DIR}")
target_link_libraries(gpushift PUBLIC PkgConfig::LIBPCI ${CMAKE_DL_LIBS})

add_executable(gpushift-cli cli/main.c)
set_target_properties(gpushift-cli PROPERTIES OUTPUT_NAME gpushift)
target_link_libraries(gpushift-cli PRIVATE gpushift)
install(TARGETS gpushift-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The privileged programs. *-test variants have test hooks, are never
# installed and only write below $GPUSHIFT_ETC_ROOT.
foreach(prog helper boot-check)
  set(variants gpushift-${prog})
  if(BUILD_TESTING)
    list(APPEND variants gpushift-${prog}-test)
  endif()
  foreach(target ${variants})
    add_executable(${target} helper/${prog}.c helper/ops.c)
    target_link_libraries(${target} PRIVATE gpushift)
  endforeach()
  if(BUILD_TESTING)
    target_compile_definitions(gpushift-${prog}-test PRIVATE GPUSHIFT_TEST_HOOKS)
  endif()
  install(TARGETS gpushift-${prog} RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
endforeach()

if(GPUSHIFT_BUILD_GUI)
  add_subdirectory(gui)
endif()
