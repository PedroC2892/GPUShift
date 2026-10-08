find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPCI REQUIRED IMPORTED_TARGET libpci)

add_library(gpushift STATIC
  lib/detect.c
  lib/pcinames.c
  lib/sysinfo.c
  lib/util.c
  lib/version.c)
target_include_directories(gpushift PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(gpushift PRIVATE GPUSHIFT_VERSION="${PROJECT_VERSION}")
target_link_libraries(gpushift PUBLIC PkgConfig::LIBPCI ${CMAKE_DL_LIBS})
