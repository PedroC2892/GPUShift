find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(gpushift-gui main.cpp mainwindow.cpp mainwindow.h)
set_target_properties(gpushift-gui PROPERTIES AUTOMOC ON)
target_link_libraries(gpushift-gui PRIVATE gpushift Qt6::Widgets)
target_compile_definitions(gpushift-gui PRIVATE
  GPUSHIFT_TRANSLATIONS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/gpushift/translations"
  QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
install(TARGETS gpushift-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
