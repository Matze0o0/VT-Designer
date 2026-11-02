qt_add_executable(vtdesigner
    main.cpp
    AttributeInfo.h AttributeInfo.cpp
    Document.h Document.cpp
    EditorContext.h EditorContext.cpp
    ObjectFactory.h ObjectFactory.cpp
    MaskCanvas.h MaskCanvas.cpp
    PoolTree.h PoolTree.cpp
    PropertyEditor.h PropertyEditor.cpp
    ListEditor.h ListEditor.cpp
    Toolbox.h Toolbox.cpp
    ProblemsView.h ProblemsView.cpp
    SettingsDialog.h SettingsDialog.cpp
    MainWindow.h MainWindow.cpp
    SchemaStrings.cpp
)

target_link_libraries(vtdesigner PRIVATE vtcore vtrender Qt6::Widgets)

set_target_properties(vtdesigner PROPERTIES
    WIN32_EXECUTABLE ON
    OUTPUT_NAME "VT-Designer")

qt_add_translations(vtdesigner
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/vtdesigner_de.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete)

# Copies the Qt runtime next to the executable so it also starts outside the IDE.
find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:vtdesigner>"
        DEPENDS vtdesigner
        COMMENT "Deploying Qt runtime next to VT-Designer.exe")
endif()
