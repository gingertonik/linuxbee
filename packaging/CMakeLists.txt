# What `cmake --install` puts where. Paths follow GNUInstallDirs, so a
# distribution can move them with the usual variables.
include(GNUInstallDirs)

install(TARGETS linuxbee linuxbeectl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES io.github.gingertonik.linuxbee.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES io.github.gingertonik.linuxbee.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES io.github.gingertonik.linuxbee.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/linuxbee)
