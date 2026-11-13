# Every .cpp, .h and .qrc under this directory is part of linuxbee_ui, the
# Qt Widgets half of the application. Adding a file needs no edit here. A
# part that needs another Qt module or an outside library puts a deps.cmake
# next to its sources (see src/ui/README.md).
file(GLOB_RECURSE LINUXBEE_UI_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)
add_library(linuxbee_ui STATIC ${LINUXBEE_UI_SOURCES})
target_include_directories(linuxbee_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(linuxbee_ui PUBLIC linuxbee_core Qt6::Widgets)

file(GLOB LINUXBEE_UI_DEPS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/deps.cmake")
foreach(deps_file IN LISTS LINUXBEE_UI_DEPS)
  include(${deps_file})
endforeach()
