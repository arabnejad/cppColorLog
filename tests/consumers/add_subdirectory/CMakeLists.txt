cmake_minimum_required(VERSION 3.14)
project(CppColorLoggerSubdirectoryConsumer LANGUAGES CXX)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." cppColorLogger)

if(TARGET cppcolorlogger_tests OR TARGET cppcolorlogger_examples)
    message(FATAL_ERROR "cppColorLogger enabled developer targets in its parent project")
endif()

add_executable(add_subdirectory_consumer main.cpp)
set_target_properties(add_subdirectory_consumer PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_link_libraries(add_subdirectory_consumer PRIVATE CppColorLogger::cppColorLogger)
