FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
# Use the same runtime library as the rest of the project on MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(vtcore_tests
    TestPools.h
    SchemaTest.cpp
    IopCodecTest.cpp
    PictureCodecTest.cpp
    ValidatorTest.cpp
    ExportersTest.cpp
    ProjectFileTest.cpp
)
target_link_libraries(vtcore_tests PRIVATE vtcore GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vtcore_tests)
