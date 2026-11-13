add_library(linuxbee_core STATIC
  version.cpp
  version.h
)
target_include_directories(linuxbee_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(linuxbee_core PRIVATE LINUXBEE_VERSION="${PROJECT_VERSION}")
target_link_libraries(linuxbee_core PUBLIC Qt6::Core)
