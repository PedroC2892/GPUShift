add_library(gpushift STATIC lib/version.c)
target_include_directories(gpushift PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(gpushift PRIVATE GPUSHIFT_VERSION="${PROJECT_VERSION}")
