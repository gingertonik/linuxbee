# The device sink plays through Qt's audio output classes.
find_package(Qt6 6.5 REQUIRED COMPONENTS Multimedia)
target_link_libraries(linuxbee_ui PUBLIC Qt6::Multimedia)
