configure_file(org.gpushift.policy.in org.gpushift.policy @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gpushift.policy
        DESTINATION ${GPUSHIFT_POLKIT_ACTIONS_DIR})
