function(gpushift_unit_test name)
  add_executable(test_${name} test_${name}.c)
  target_link_libraries(test_${name} PRIVATE gpushift)
  target_compile_definitions(test_${name} PRIVATE
    GPUSHIFT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

gpushift_unit_test(version)
gpushift_unit_test(detect)
