// Test runner for the editor: widget tests need a QApplication, which runs off screen here.

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#ifdef VTD_QT_PLATFORM_PLUGINS
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM_PLUGIN_PATH")) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", VTD_QT_PLATFORM_PLUGINS);
    }
#endif
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
