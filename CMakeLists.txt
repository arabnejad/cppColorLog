cmake_minimum_required(VERSION 3.14)
project(CppColorLogger VERSION 1.0.0 LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

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

option(CPPCOLORLOGGER_BUILD_EXAMPLES "Build cppColorLogger examples" OFF)
option(CPPCOLORLOGGER_BUILD_TESTS "Build cppColorLogger unit tests" OFF)

if(CPPCOLORLOGGER_BUILD_EXAMPLES)
    file(GLOB_RECURSE EXAMPLE_SOURCES CONFIGURE_DEPENDS
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        examples/*/*.cpp
    )

    set(EXAMPLE_TARGETS "")
    foreach(example_file IN LISTS EXAMPLE_SOURCES)
        get_filename_component(example_name ${example_file} NAME_WE)
        set(example_target cppcolorlogger_example_${example_name})
        add_executable(${example_target} ${example_file})
        set_target_properties(${example_target} PROPERTIES
            OUTPUT_NAME ${example_name}
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
        )
        target_link_libraries(${example_target} PRIVATE CppColorLogger::cppColorLogger)
        list(APPEND EXAMPLE_TARGETS ${example_target})
    endforeach()

    add_custom_target(cppcolorlogger_examples DEPENDS ${EXAMPLE_TARGETS})
    add_custom_target(cppcolorlogger_run_examples
        COMMAND ${CMAKE_COMMAND} -E echo "Running all examples..."
        DEPENDS ${EXAMPLE_TARGETS}
        USES_TERMINAL
    )

    foreach(example_target IN LISTS EXAMPLE_TARGETS)
        add_custom_command(TARGET cppcolorlogger_run_examples POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running ${example_target}..."
            COMMAND $<TARGET_FILE:${example_target}>
        )
    endforeach()
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    find_program(CLANG_FORMAT_EXE NAMES clang-format)
    if(CLANG_FORMAT_EXE)
        file(GLOB_RECURSE FORMAT_FILES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        )
        add_custom_target(cppcolorlogger_format
            COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_FILES}
            COMMENT "Formatting source files"
        )
    else()
        add_custom_target(cppcolorlogger_format
            COMMAND ${CMAKE_COMMAND} -E echo "clang-format was not found"
            COMMAND ${CMAKE_COMMAND} -E false
        )
    endif()
endif()

if(CPPCOLORLOGGER_BUILD_TESTS)
    include(CTest)
endif()

if(CPPCOLORLOGGER_BUILD_TESTS AND BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        find_package(GTest CONFIG QUIET)
    endif()

    if(NOT TARGET GTest::gtest_main)
        include(FetchContent)

        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()

        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(cppcolorlogger_tests
        tests/error_macro_compatibility.cpp
        tests/logger_from_another_translation_unit.cpp
        tests/test_logger.cpp
    )
    set_target_properties(cppcolorlogger_tests PROPERTIES
        OUTPUT_NAME logger_tests
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_link_libraries(cppcolorlogger_tests PRIVATE
        CppColorLogger::cppColorLogger
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(cppcolorlogger_tests)
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
