# The desktop's media controls are reached over D-Bus.
find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
target_link_libraries(linuxbee_ui PUBLIC Qt6::DBus)
