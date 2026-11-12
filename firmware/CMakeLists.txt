cmake_minimum_required(VERSION 3.20)
project(VirtualController LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(controller_logic src/controller.cpp)
target_include_directories(controller_logic PUBLIC include)

add_executable(virtual_controller src/main.cpp)
target_link_libraries(virtual_controller PRIVATE controller_logic)
if(WIN32)
    target_link_libraries(virtual_controller PRIVATE ws2_32)
endif()

enable_testing()

add_executable(controller_tests tests/controller_tests.cpp)
target_link_libraries(controller_tests PRIVATE controller_logic)
add_test(NAME controller_tests COMMAND controller_tests)

add_executable(serial_rx_tests tests/serial_rx_tests.cpp)
target_include_directories(serial_rx_tests PRIVATE include)
target_link_libraries(serial_rx_tests PRIVATE Threads::Threads)
add_test(NAME serial_rx_tests COMMAND serial_rx_tests)
