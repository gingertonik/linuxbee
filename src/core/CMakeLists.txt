# Every .cpp and .h under this directory is part of linuxbee_core. Adding a
# file needs no edit here. A subsystem that needs an outside library puts a
# deps.cmake next to its sources (see src/core/README.md).
file(GLOB_RECURSE LINUXBEE_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
add_library(linuxbee_core STATIC ${LINUXBEE_CORE_SOURCES})
target_include_directories(linuxbee_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(linuxbee_core PRIVATE LINUXBEE_VERSION="${PROJECT_VERSION}")
target_link_libraries(linuxbee_core PUBLIC Qt6::Core)

file(GLOB LINUXBEE_CORE_DEPS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/deps.cmake")
foreach(deps_file IN LISTS LINUXBEE_CORE_DEPS)
  include(${deps_file})
endforeach()
