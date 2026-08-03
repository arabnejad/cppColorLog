cmake_minimum_required(VERSION 3.24)
project(CppColorLogger VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(cppColorLogger INTERFACE)
add_library(CppColorLogger::cppColorLogger ALIAS cppColorLogger)

target_compile_features(cppColorLogger INTERFACE cxx_std_11)
target_include_directories(cppColorLogger INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(Threads REQUIRED)
target_link_libraries(cppColorLogger INTERFACE Threads::Threads)

option(BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})

if(BUILD_EXAMPLES)
    file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        examples/*/*.cpp
    )

    set(EXAMPLE_TARGETS "")
    foreach(example_file IN LISTS EXAMPLE_SOURCES)
        get_filename_component(example_name ${example_file} NAME_WE)
        add_executable(${example_name} ${example_file})
        target_link_libraries(${example_name} PRIVATE CppColorLogger::cppColorLogger)
        list(APPEND EXAMPLE_TARGETS ${example_name})
    endforeach()

    add_custom_target(examples DEPENDS ${EXAMPLE_TARGETS})
    add_custom_target(run_all_samples
        COMMAND ${CMAKE_COMMAND} -E echo "Running all examples..."
        DEPENDS ${EXAMPLE_TARGETS}
        USES_TERMINAL
    )

    foreach(example_target IN LISTS EXAMPLE_TARGETS)
        add_custom_command(TARGET run_all_samples POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running ${example_target}..."
            COMMAND $<TARGET_FILE:${example_target}>
        )
    endforeach()
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_FILES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )
    add_custom_target(clang_format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_FILES}
        COMMENT "Formatting source files"
    )
else()
    add_custom_target(clang_format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format was not found"
        COMMAND ${CMAKE_COMMAND} -E false
    )
endif()

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(logger_tests
        tests/multitu_a.cpp
        tests/multitu_b.cpp
        tests/test_logger.cpp
    )
    target_link_libraries(logger_tests PRIVATE
        CppColorLogger::cppColorLogger
        GTest::gtest_main
    )

    add_test(NAME logger_tests COMMAND logger_tests)
    add_custom_target(tests DEPENDS logger_tests)
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS logger_tests
        USES_TERMINAL
    )
endif()

install(TARGETS cppColorLogger EXPORT CppColorLoggerTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/CppColorLoggerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CppColorLoggerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CppColorLogger
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CppColorLoggerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT CppColorLoggerTargets
    NAMESPACE CppColorLogger::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CppColorLogger
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CppColorLoggerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CppColorLoggerConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CppColorLogger
)
