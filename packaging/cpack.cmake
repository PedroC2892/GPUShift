# Packages: configure with -DCMAKE_INSTALL_PREFIX=/usr on the target
# distribution, build, then run cpack. Two packages are produced:
# gpushift (CLI, helper, boot check, polkit, docs) and gpushift-gui.

set(CPACK_PACKAGE_NAME gpushift)
set(CPACK_PACKAGE_VENDOR "GPUShift")
set(CPACK_PACKAGE_CONTACT "Pedro Cruz <pedroafonsocruz2020@gmail.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_STRIP_FILES ON)
set(CPACK_COMPONENTS_ALL core)
if(GPUSHIFT_BUILD_GUI)
  list(APPEND CPACK_COMPONENTS_ALL gui)
endif()
set(_gpushift_pkg ${CMAKE_BINARY_DIR}/packaging)

# Per-package copyright and changelog, as Debian policy requires.
foreach(pkg gpushift gpushift-gui)
  set(_comp core)
  if(pkg STREQUAL "gpushift-gui")
    set(_comp gui)
    if(NOT GPUSHIFT_BUILD_GUI)
      continue()
    endif()
  endif()
  add_custom_command(OUTPUT ${_gpushift_pkg}/${pkg}/changelog.Debian.gz
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_gpushift_pkg}/${pkg}
    COMMAND gzip -9nc ${PROJECT_SOURCE_DIR}/packaging/debian/changelog > ${_gpushift_pkg}/${pkg}/changelog.Debian.gz
    DEPENDS ${PROJECT_SOURCE_DIR}/packaging/debian/changelog VERBATIM)
  add_custom_target(changelog-${pkg} ALL DEPENDS ${_gpushift_pkg}/${pkg}/changelog.Debian.gz)
  install(FILES ${PROJECT_SOURCE_DIR}/packaging/debian/copyright
                ${_gpushift_pkg}/${pkg}/changelog.Debian.gz
          DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${pkg} COMPONENT ${_comp})
endforeach()

foreach(script postinst prerm)
  configure_file(${PROJECT_SOURCE_DIR}/packaging/debian/${script}.in ${_gpushift_pkg}/deb/${script} @ONLY)
endforeach()
file(COPY ${PROJECT_SOURCE_DIR}/packaging/debian/postrm DESTINATION ${_gpushift_pkg}/deb)
configure_file(${PROJECT_SOURCE_DIR}/packaging/debian/conffiles.in ${_gpushift_pkg}/deb/conffiles @ONLY)
configure_file(${PROJECT_SOURCE_DIR}/packaging/rpm/preun.sh.in ${_gpushift_pkg}/rpm/preun.sh @ONLY)

# Debian / Ubuntu
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Pedro Cruz <pedroafonsocruz2020@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_CORE_PACKAGE_NAME gpushift)
set(CPACK_DEBIAN_CORE_PACKAGE_DEPENDS "libpci3, pci.ids, pkexec, polkitd")
# The first line is the synopsis. An empty global summary stops CPack from
# prepending the project description to each component description.
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "")
set(CPACK_DEBIAN_CORE_DESCRIPTION "GPU mode manager for Linux laptops
GPUShift detects the GPUs of the system, shows detailed information about
them and switches between the GPU modes the hardware supports: Integrated,
Hybrid (PRIME render offload) and Dedicated (firmware MUX). It only uses
kernel interfaces (sysfs, modprobe.d, udev) and polkit. Unconfirmed mode
changes are reverted automatically at boot.

This package contains the command line tool, the privileged helper and
the boot check service.")
set(CPACK_DEBIAN_CORE_PACKAGE_CONTROL_EXTRA
  ${_gpushift_pkg}/deb/postinst ${_gpushift_pkg}/deb/prerm ${_gpushift_pkg}/deb/postrm
  ${_gpushift_pkg}/deb/conffiles)
set(CPACK_DEBIAN_CORE_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_DEBIAN_GUI_PACKAGE_NAME gpushift-gui)
set(CPACK_DEBIAN_GUI_PACKAGE_DEPENDS
  "gpushift (= ${PROJECT_VERSION}-1), libqt6widgets6 | libqt6widgets6t64")
set(CPACK_DEBIAN_GUI_DESCRIPTION "GPU mode manager for Linux laptops (graphical interface)
Qt 6 interface for GPUShift: one card per GPU, the available GPU modes
with Apply and Reset, conflict warnings and the confirmation dialog shown
after a mode change.")

# Fedora / openSUSE
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Base")
set(CPACK_RPM_CORE_PACKAGE_NAME gpushift)
set(CPACK_RPM_CORE_PACKAGE_SUMMARY "GPU mode manager for Linux laptops")
set(CPACK_RPM_CORE_PACKAGE_REQUIRES "polkit, hwdata")
set(CPACK_RPM_CORE_POST_INSTALL_SCRIPT_FILE ${PROJECT_SOURCE_DIR}/packaging/rpm/post.sh)
set(CPACK_RPM_CORE_PRE_UNINSTALL_SCRIPT_FILE ${_gpushift_pkg}/rpm/preun.sh)
set(CPACK_RPM_CORE_POST_UNINSTALL_SCRIPT_FILE ${PROJECT_SOURCE_DIR}/packaging/rpm/postun.sh)
set(CPACK_RPM_GUI_PACKAGE_NAME gpushift-gui)
set(CPACK_RPM_GUI_PACKAGE_SUMMARY "GPU mode manager for Linux laptops (graphical interface)")
set(CPACK_RPM_GUI_PACKAGE_REQUIRES "gpushift = ${PROJECT_VERSION}")
set(CPACK_RPM_USER_FILELIST "%config(noreplace) ${GPUSHIFT_AUTOSTART_DIR}/gpushift-confirm.desktop")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /etc/xdg /etc/xdg/autostart /usr/libexec /usr/lib/systemd /usr/lib/systemd/system
  /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
  /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
  /usr/share/polkit-1 /usr/share/polkit-1/actions /usr/share/man /usr/share/man/man1)

include(CPack)
cpack_add_component(core DISPLAY_NAME "GPUShift" REQUIRED)
cpack_add_component(gui DISPLAY_NAME "GPUShift GUI" DEPENDS core)
