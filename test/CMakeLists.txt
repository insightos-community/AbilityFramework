function(add_unit_test test_file_basename)
  add_executable(${test_file_basename} ${test_file_basename}.cpp)
  target_link_libraries(${test_file_basename} AbilityFrameworkAux)
endfunction()

add_unit_test(test-cr)
add_unit_test(test-crd)
add_unit_test(test-resourcemgr)
add_unit_test(test-resmgr-msgbus)
add_unit_test(test-start-subprocess)

find_package(absl REQUIRED)

add_unit_test(test-msgbus)

add_unit_test(test-http-send-heartbeat)
