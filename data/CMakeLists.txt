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

install(FILES ${PROJECT_SOURCE_DIR}/docs/RECOVERY.md ${PROJECT_SOURCE_DIR}/docs/RECOVERY.pt_PT.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Man pages are installed compressed, as Debian and Fedora expect.
function(gpushift_install_man page component)
  set(out ${CMAKE_BINARY_DIR}/${page}.gz)
  add_custom_command(OUTPUT ${out}
    COMMAND gzip -9nc ${PROJECT_SOURCE_DIR}/data/${page} > ${out}
    DEPENDS ${PROJECT_SOURCE_DIR}/data/${page} VERBATIM)
  add_custom_target(man-${page} ALL DEPENDS ${out})
  install(FILES ${out} DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT ${component})
endfunction()
gpushift_install_man(gpushift.1 core)
