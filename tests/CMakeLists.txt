# Tests are found by name. Adding one needs no edit here.
#   tests/tst_*.cpp, tests/unit/tst_*.cpp, tests/locked/tst_*.cpp
#       one QtTest program each, named after the file
#   A test named tst_ui_* is also linked with linuxbee_ui (Qt Widgets) and
#   runs on the offscreen platform, so it needs no display.
#   tests/locked/cli_*.sh, tests/unit/cli_*.sh
#       shell tests, run as: sh <script> <path to linuxbeectl>
# Fixture files live in tests/data; its path is LINUXBEE_TEST_DATA.
file(GLOB LINUXBEE_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/tst_*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/locked/tst_*.cpp"
)
foreach(test_source IN LISTS LINUXBEE_TEST_SOURCES)
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE linuxbee_core Qt6::Test)
  # L-READFAIL 3: the tests may call the library's old answers, which are marked deprecated.
  target_compile_options(${test_name} PRIVATE -Wno-deprecated-declarations)
  if(test_name MATCHES "^tst_ui_")
    target_link_libraries(${test_name} PRIVATE linuxbee_ui)
  endif()
  target_compile_definitions(${test_name} PRIVATE
    LINUXBEE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

file(GLOB LINUXBEE_CLI_TESTS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/locked/cli_*.sh"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/cli_*.sh"
)
foreach(test_script IN LISTS LINUXBEE_CLI_TESTS)
  get_filename_component(test_name ${test_script} NAME_WE)
  add_test(NAME ${test_name} COMMAND sh ${test_script} $<TARGET_FILE:linuxbeectl>)
  # A shell test that could not do its work where it is run answers 77: skipped, and shown so.
  set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "LINUXBEE_TEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data;LINUXBEE_APP=$<TARGET_FILE:linuxbee>;QT_QPA_PLATFORM=offscreen")
endforeach()
