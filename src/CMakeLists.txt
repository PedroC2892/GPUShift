find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPCI REQUIRED IMPORTED_TARGET libpci)

add_library(gpushift STATIC
  lib/conflicts.c
  lib/detect.c
  lib/modes.c
  lib/mux.c
  lib/pcinames.c
  lib/state.c
  lib/sysinfo.c
  lib/util.c
  lib/version.c)
target_include_directories(gpushift PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(gpushift PRIVATE GPUSHIFT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gpushift PUBLIC PkgConfig::LIBPCI ${CMAKE_DL_LIBS})

add_executable(gpushift-cli cli/main.c)
set_target_properties(gpushift-cli PROPERTIES OUTPUT_NAME gpushift)
target_link_libraries(gpushift-cli PRIVATE gpushift)
install(TARGETS gpushift-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
