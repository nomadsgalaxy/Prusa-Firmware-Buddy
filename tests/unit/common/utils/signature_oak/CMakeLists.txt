# Signature Oak tests

# Include paths needed for tests
set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/common/utils ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/common ${CMAKE_SOURCE_DIR}/tests/stubs # Stub headers for missing
                                                                       # dependencies
    )

# Icon parity test — Python + Pillow (checks that icons with orange pixels have brass overrides)
add_test(NAME icon_parity_tests
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/icon_parity_tests.py"
                 "${CMAKE_SOURCE_DIR}"
         )
