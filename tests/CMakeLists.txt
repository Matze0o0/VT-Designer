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
    PoolEditingTest.cpp
)
target_link_libraries(vtcore_tests PRIVATE vtcore GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vtcore_tests)

# Editor logic that does not need a window (document, undo, object factory).
if(TARGET Qt6::Widgets)
    set(APP_DIR ${CMAKE_SOURCE_DIR}/src/app)
    add_executable(vtdesigner_tests
        app/main.cpp
        app/DocumentTest.cpp
        app/CanvasTest.cpp
        ${APP_DIR}/AttributeInfo.cpp ${APP_DIR}/AttributeInfo.h
        ${APP_DIR}/Document.cpp ${APP_DIR}/Document.h
        ${APP_DIR}/EditorContext.cpp ${APP_DIR}/EditorContext.h
        ${APP_DIR}/FlowLayout.cpp ${APP_DIR}/FlowLayout.h
        ${APP_DIR}/MaskCanvas.cpp ${APP_DIR}/MaskCanvas.h
        ${APP_DIR}/ObjectFactory.cpp ${APP_DIR}/ObjectFactory.h
        ${APP_DIR}/ObjectIcons.cpp ${APP_DIR}/ObjectIcons.h
        ${APP_DIR}/ObjectPalette.cpp ${APP_DIR}/ObjectPalette.h
    )
    target_include_directories(vtdesigner_tests PRIVATE ${APP_DIR})
    target_link_libraries(vtdesigner_tests PRIVATE vtcore vtrender Qt6::Widgets Qt6::Test GTest::gtest)
    # The editor tests use the offscreen platform plugin of the Qt installation, which a deployed
    # copy of the Qt runtime next to the executables does not contain.
    target_compile_definitions(vtdesigner_tests PRIVATE
        VTD_QT_PLATFORM_PLUGINS="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms")
    gtest_discover_tests(vtdesigner_tests)
endif()
