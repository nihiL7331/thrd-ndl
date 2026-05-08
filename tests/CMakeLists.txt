if(MSVC)
    add_compile_options("/U_NDEBUG")
else()
    add_compile_options("-UNDEBUG")
endif()

set(ALL_TEST_TARGETS "")

function(add_test_suite CATEGORY_NAME)
  foreach(TEST_NAME IN LISTS ARGN)
    add_executable(${TEST_NAME} ${TEST_NAME}.c)
    target_link_libraries(${TEST_NAME} PRIVATE thrd_ndl)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS ${CATEGORY_NAME})
    set(ALL_TEST_TARGETS ${ALL_TEST_TARGETS} ${TEST_NAME} PARENT_SCOPE)
  endforeach()

  add_custom_target(test-${CATEGORY_NAME}
    COMMAND ${CMAKE_CTEST_COMMAND} -L ${CATEGORY_NAME} --output-on-failure
    DEPENDS ${ARGN}
    COMMENT "Running ${CATEGORY_NAME} tests..."
  )
endfunction()

add_test_suite(mutex mutex)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${ALL_TEST_TARGETS}
    COMMENT "Running all tests..."
)

