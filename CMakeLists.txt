cmake_minimum_required(VERSION 3.10)
project(CppColorLogger)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# ================================
# EXAMPLES
# ================================
file(GLOB_RECURSE EXAMPLE_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} examples/*/*.cpp)
set(EXAMPLE_TARGETS "")

foreach(example_file ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_file} NAME_WE)
    get_filename_component(example_dir ${example_file} DIRECTORY)

    add_executable(${example_name} ${example_file})
    set_target_properties(${example_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/${example_dir}
    )

    list(APPEND EXAMPLE_TARGETS ${example_name})
endforeach()

add_custom_target(examples DEPENDS ${EXAMPLE_TARGETS})

add_custom_target(run_all_samples
    COMMAND ${CMAKE_COMMAND} -E echo "Running all compiled examples..."
)

foreach(example_target ${EXAMPLE_TARGETS})
    add_custom_command(TARGET run_all_samples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running ${example_target}..."
        COMMAND ${CMAKE_SOURCE_DIR}/examples/${example_target}/${example_target}
    )
endforeach()

# ================================
# CLANG FORMATTER
# ================================
find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_FILES
        ${CMAKE_SOURCE_DIR}/examples/*.cpp
        ${CMAKE_SOURCE_DIR}/include/*.h
    )
    add_custom_target(clang_format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_FILES}
        COMMENT "Running clang-format on examples/ and include/"
    )
else()
    message(WARNING "clang-format not found. 'clang_format' target will be unavailable.")
endif()

# ================================
# UNIT TESTS
# ================================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    file(GLOB TEST_SOURCES tests/*.cpp)

    add_executable(logger_tests ${TEST_SOURCES})
    target_include_directories(logger_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(logger_tests GTest::gtest GTest::gtest_main pthread)

    add_test(NAME RunLoggerTests COMMAND logger_tests)

    add_custom_target(tests DEPENDS logger_tests)

    add_custom_target(run_tests
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/logger_tests --gtest_color=yes --gtest_output=verbose
        DEPENDS logger_tests
        COMMENT "Running all unit tests directly"
    )
endif()
