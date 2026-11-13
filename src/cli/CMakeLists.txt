# Every .cpp in this directory is part of linuxbeectl. A new command is a new
# cmd_<name>.cpp that registers itself (see command.h); nothing here changes.
file(GLOB LINUXBEE_CLI_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
add_executable(linuxbeectl ${LINUXBEE_CLI_SOURCES})
target_link_libraries(linuxbeectl PRIVATE linuxbee_core)
