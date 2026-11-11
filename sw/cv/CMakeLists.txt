cmake_minimum_required(VERSION 3.20)
project(portal_cv LANGUAGES CXX)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(BUILD_SERVICE "Build the camera and HTTP service" ON)

add_library(portal_tracker src/tracker.cpp)
target_include_directories(portal_tracker PUBLIC include)
target_compile_features(portal_tracker PUBLIC cxx_std_17)
set_target_properties(portal_tracker PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(portal_tracker PRIVATE /W4)
else()
    target_compile_options(portal_tracker PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tracker_tests tests/tracker_tests.cpp)
    target_link_libraries(tracker_tests PRIVATE portal_tracker)
    add_test(NAME tracker_tests COMMAND tracker_tests)
endif()

if(BUILD_SERVICE)
    find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect dnn)
    find_package(Threads REQUIRED)
    include(FetchContent)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(httplib
        URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/4f3f9ef19be83ae97a5d9a059432dc00e445b7ab
        URL_HASH SHA256=415e8d638f6e2390f51565d4fdc86577dc0ef09f56da7ea74d7731d9da8349a6
    )
    FetchContent_Declare(json
        URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
        URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
    )
    FetchContent_MakeAvailable(httplib json)
    add_executable(portal_cv src/service.cpp)
    target_include_directories(portal_cv PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(portal_cv PRIVATE portal_tracker httplib::httplib nlohmann_json::nlohmann_json Threads::Threads ${OpenCV_LIBS})
    set_target_properties(portal_cv PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(portal_cv PRIVATE /W4)
    else()
        target_compile_options(portal_cv PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
