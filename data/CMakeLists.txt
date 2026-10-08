configure_file(org.gpushift.policy.in org.gpushift.policy @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gpushift.policy
        DESTINATION ${GPUSHIFT_POLKIT_ACTIONS_DIR})

configure_file(gpushift-confirm.desktop.in gpushift-confirm.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpushift-confirm.desktop
        DESTINATION ${GPUSHIFT_AUTOSTART_DIR})

if(GPUSHIFT_SYSTEMD_UNIT_DIR)
  configure_file(gpushift-boot-check.service.in gpushift-boot-check.service @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpushift-boot-check.service
          DESTINATION ${GPUSHIFT_SYSTEMD_UNIT_DIR})
endif()
