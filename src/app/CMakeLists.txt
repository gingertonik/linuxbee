# The application itself: a thin main() over linuxbee_ui.
file(GLOB LINUXBEE_APP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
add_executable(linuxbee ${LINUXBEE_APP_SOURCES})
target_link_libraries(linuxbee PRIVATE linuxbee_ui)
