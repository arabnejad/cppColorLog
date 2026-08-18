cmake_minimum_required(VERSION 3.14)
project(CppColorLoggerInstalledConsumer LANGUAGES CXX)

find_package(CppColorLogger CONFIG REQUIRED)

add_executable(installed_package_consumer main.cpp)
set_target_properties(installed_package_consumer PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_link_libraries(installed_package_consumer PRIVATE CppColorLogger::cppColorLogger)
