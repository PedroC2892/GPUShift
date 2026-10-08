function(gpushift_unit_test name)
  add_executable(test_${name} test_${name}.c)
  target_link_libraries(test_${name} PRIVATE gpushift)
  target_compile_definitions(test_${name} PRIVATE
    GPUSHIFT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

gpushift_unit_test(version)
gpushift_unit_test(detect)
gpushift_unit_test(modes)

add_test(NAME cli
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh
          $<TARGET_FILE:gpushift-cli> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
gpushift_unit_test(config)

add_test(NAME helper
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/helper_test.sh
          $<TARGET_FILE:gpushift-helper-test> $<TARGET_FILE:gpushift-cli>
          ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
