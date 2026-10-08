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
  PUBLIC GPUSHIFT_MODPROBE_DIR="${GPUSHIFT_MODPROBE_DIR}"
         GPUSHIFT_UDEV_RULES_DIR="${GPUSHIFT_UDEV_RULES_DIR}")
target_link_libraries(gpushift PUBLIC PkgConfig::LIBPCI ${CMAKE_DL_LIBS})

add_executable(gpushift-cli cli/main.c)
set_target_properties(gpushift-cli PROPERTIES OUTPUT_NAME gpushift)
target_link_libraries(gpushift-cli PRIVATE gpushift)
install(TARGETS gpushift-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(gpushift-helper helper/helper.c)
target_link_libraries(gpushift-helper PRIVATE gpushift)
install(TARGETS gpushift-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

if(BUILD_TESTING)
  # Same helper with test hooks: never installed, only writes below $GPUSHIFT_ETC_ROOT.
  add_executable(gpushift-helper-test helper/helper.c)
  target_link_libraries(gpushift-helper-test PRIVATE gpushift)
  target_compile_definitions(gpushift-helper-test PRIVATE GPUSHIFT_TEST_HOOKS)
endif()
