function(linuxbee_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE linuxbee_core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

linuxbee_add_test(tst_version tst_version.cpp)
linuxbee_add_test(tst_template locked/tst_template.cpp)
add_test(NAME cli_template
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/locked/cli_template.sh $<TARGET_FILE:linuxbeectl>)
