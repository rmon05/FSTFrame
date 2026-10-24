cmake_minimum_required(VERSION 3.20)
project(FSTFrame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fstframe src/read_csv.cpp)
target_include_directories(fstframe PUBLIC src)

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(read_csv_test tests/read_csv_test.cpp)
    target_link_libraries(read_csv_test PRIVATE fstframe GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(read_csv_test)
endif()
