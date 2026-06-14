cmake_minimum_required(VERSION 3.16)
project(AIVisionServerDealer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(OpenCV REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(LIB_SOURCES
    src/common/Logger.cpp
    src/common/Response.cpp
    src/data/ConfigManager.cpp
    src/core/ImageBuffer.cpp
    src/core/CoreModule.cpp
    src/api1/DetectionDealer.cpp
    src/api1/AdminServer.cpp
    src/api2/ImageSubscriber.cpp
    src/api2/ResultPublisher.cpp
)

add_library(ai_vision_lib STATIC ${LIB_SOURCES})
target_include_directories(ai_vision_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(ai_vision_lib PUBLIC
    ${ZMQ_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
    pthread
)
target_compile_options(ai_vision_lib PRIVATE -Wall -Wextra)

add_executable(ai_vision_server_dealer src/main.cpp)
target_link_libraries(ai_vision_server_dealer PRIVATE ai_vision_lib)

enable_testing()
find_package(GTest REQUIRED)
add_executable(ai_vision_tests
    tests/test_image_buffer.cpp
)
target_link_libraries(ai_vision_tests PRIVATE ai_vision_lib GTest::gtest GTest::gtest_main)
add_test(NAME ai_vision_tests COMMAND ai_vision_tests)
