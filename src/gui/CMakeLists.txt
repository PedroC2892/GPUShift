find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets LinguistTools)

add_executable(gpushift-gui main.cpp mainwindow.cpp mainwindow.h)
set_target_properties(gpushift-gui PROPERTIES AUTOMOC ON)
target_link_libraries(gpushift-gui PRIVATE gpushift Qt6::Widgets)
target_compile_definitions(gpushift-gui PRIVATE
  GPUSHIFT_TRANSLATIONS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/gpushift/translations"
  QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
install(TARGETS gpushift-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# .qm files are installed (not embedded) so distributions can ship them as data.
qt_add_translations(gpushift-gui
  TS_FILES ${PROJECT_SOURCE_DIR}/translations/gpushift_pt_PT.ts
  QM_FILES_OUTPUT_VARIABLE gpushift_qm_files)
install(FILES ${gpushift_qm_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/gpushift/translations)
