if(MSVC)
    add_compile_options("/U_NDEBUG")
else()
    add_compile_options("-UNDEBUG")
endif()

set(ALL_TEST_TARGETS "")

function(add_test_suite TEST_NAME)
  add_executable(${TEST_NAME} ${TEST_NAME}.c)
  target_link_libraries(${TEST_NAME} PRIVATE thrd_ndl)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  set_tests_properties(${TEST_NAME} PROPERTIES LABELS ${TEST_NAME})
  set(ALL_TEST_TARGETS ${ALL_TEST_TARGETS} ${TEST_NAME} PARENT_SCOPE)

  add_custom_target(test-${TEST_NAME}
    COMMAND ${CMAKE_CTEST_COMMAND} -L ${TEST_NAME} --output-on-failure
    DEPENDS ${TEST_NAME}
    COMMENT "Running ${TEST_NAME} tests..."
  )
endfunction()

add_test_suite(mutex)
add_test_suite(cond)
add_test_suite(preempt)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${ALL_TEST_TARGETS}
    COMMENT "Running all tests..."
)

