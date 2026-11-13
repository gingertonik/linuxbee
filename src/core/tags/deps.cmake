# TagLib reads and writes the tags. Version 2.0 or later.
find_package(TagLib 2.0 REQUIRED)
target_link_libraries(linuxbee_core PUBLIC TagLib::tag)
