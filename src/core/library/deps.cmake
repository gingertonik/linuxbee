# The library is stored in an SQLite file through Qt's SQL module.
find_package(Qt6 6.5 REQUIRED COMPONENTS Sql)
target_link_libraries(linuxbee_core PUBLIC Qt6::Sql)
