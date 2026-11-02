#include "MainWindow.h"
#include "SettingsDialog.h"

#include "vtcore/IopCodec.h"
#include "vtcore/ProjectFile.h"
#include "vtrender/VtRenderer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

namespace {

// Renders one view of a project into a PNG without opening a window (used for previews and tests).
int renderToFile(const QString& input, const QString& rootText, const QString& output, int scale)
{
    try {
        const std::filesystem::path path = input.toStdWString();
        vt::Project project = QFileInfo(input).suffix().compare(QStringLiteral("iop"), Qt::CaseInsensitive) == 0
                                  ? vt::Project(vt::decodePool(vt::readBinaryFile(path), vt::VtVersion::V4))
                                  : vt::loadProject(path);
        vt::ObjectId root = vt::kNullObjectId;
        bool numeric = false;
        const int id = rootText.toInt(&numeric);
        if (numeric) {
            root = static_cast<vt::ObjectId>(id);
        } else if (const vt::VtObject* named = project.pool.findByName(rootText.toStdString())) {
            root = named->id;
        } else if (const vt::VtObject* ws = project.pool.workingSet()) {
            root = static_cast<vt::ObjectId>(ws->get("activeMask"));
        }
        vtr::RenderSettings settings;
        settings.dataMaskSize = project.designer.dataMaskSize;
        settings.softKeyWidth = project.designer.softKeyWidth;
        settings.softKeyHeight = project.designer.softKeyHeight;
        settings.physicalSoftKeys = project.designer.physicalSoftKeys;
        settings.designMode = false;
        QImage image = vtr::Renderer(project.pool, settings).renderViewImage(root);
        if (scale > 1) {
            image = image.scaled(image.size() * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }
        return image.save(output) ? 0 : 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("VT-Designer"));
    QApplication::setApplicationName(QStringLiteral("VT-Designer"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QString language = SettingsDialog::languageSetting();
    if (language.isEmpty()) {
        language = QLocale::system().name().left(2);
    }
    QTranslator appTranslator;
    QTranslator qtTranslator;
    if (language == QLatin1String("de")) {
        if (appTranslator.load(QStringLiteral(":/i18n/vtdesigner_de.qm"))) {
            QApplication::installTranslator(&appTranslator);
        }
        if (qtTranslator.load(QLocale(QLocale::German), QStringLiteral("qtbase"), QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QApplication::installTranslator(&qtTranslator);
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Graphical editor for ISOBUS VT object pools"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Project (.vtp) or object pool (.iop) to open"));
    const QCommandLineOption renderOption(QStringLiteral("render"),
                                          QStringLiteral("Render the view <root> (ID or name) into --output and exit"),
                                          QStringLiteral("root"));
    const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("Image file for --render/--screenshot"),
                                          QStringLiteral("png"));
    const QCommandLineOption scaleOption(QStringLiteral("scale"), QStringLiteral("Integer zoom for --render"),
                                         QStringLiteral("factor"), QStringLiteral("1"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save a picture of the main window to --output and exit"));
    const QCommandLineOption selectOption(QStringLiteral("select"), QStringLiteral("Select the object with this ID"),
                                          QStringLiteral("id"));
    parser.addOptions({renderOption, outputOption, scaleOption, screenshotOption, selectOption});
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    if (parser.isSet(renderOption)) {
        if (files.isEmpty() || !parser.isSet(outputOption)) {
            std::fprintf(stderr, "--render needs a file and --output\n");
            return 2;
        }
        return renderToFile(files.first(), parser.value(renderOption), parser.value(outputOption),
                            parser.value(scaleOption).toInt());
    }

    MainWindow window;
    if (!files.isEmpty()) {
        window.openFile(files.first());
    }
    if (parser.isSet(selectOption)) {
        const auto id = static_cast<vt::ObjectId>(parser.value(selectOption).toInt());
        if (window.context()->pool().contains(id)) {
            window.context()->selectObject(id);
        }
    }
    window.show();

    if (parser.isSet(screenshotOption) && parser.isSet(outputOption)) {
        const QString output = parser.value(outputOption);
        QTimer::singleShot(800, &window, [&window, output] {
            const bool ok = window.grab().save(output);
            QApplication::exit(ok ? 0 : 2);
        });
    }
    return QApplication::exec();
}
