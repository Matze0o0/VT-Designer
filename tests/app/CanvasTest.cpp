#include "../TestPools.h"

#include "AttributeInfo.h"
#include "Document.h"
#include "EditorContext.h"
#include "MaskCanvas.h"
#include "ObjectPalette.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QSignalSpy>

#include <gtest/gtest.h>

#include <cmath>

using namespace vt;

namespace {

// Saves a picture of the widget when VTD_TEST_SCREENSHOTS points to a folder (for visual checks).
void saveScreenshot(QWidget& widget, const QString& name)
{
    const QString folder = qEnvironmentVariable("VTD_TEST_SCREENSHOTS");
    if (!folder.isEmpty()) {
        widget.grab().save(QDir(folder).filePath(name));
    }
}

QMimeData* typeMime(ObjectType type)
{
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(MaskCanvas::kTypeMime), QByteArray::number(static_cast<int>(type)));
    return mime;
}

} // namespace

TEST(MaskCanvas, DragShowsObjectPreviewAndDropReportsTarget)
{
    Document document(Project(test::minimalPool()));
    EditorContext context;
    context.setDocument(&document);
    MaskCanvas canvas(&context);
    canvas.resize(700, 600);
    canvas.show();
    canvas.setZoom(1.0);
    canvas.setShowGrid(false);
    canvas.centerOn(QPointF(240, 240));
    QApplication::processEvents();

    const QPoint viewPoint = canvas.mapFromScene(QPointF(150, 200));
    const QPointF mapped = canvas.mapToScene(viewPoint);
    const QPoint scenePoint(static_cast<int>(std::floor(mapped.x())), static_cast<int>(std::floor(mapped.y())));
    std::unique_ptr<QMimeData> mime(typeMime(ObjectType::OutputMeter));

    QDragEnterEvent enter(QPointF(viewPoint), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDragMoveEvent move(QPointF(viewPoint), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &move);
    EXPECT_TRUE(move.isAccepted());
    QApplication::processEvents();

    // The default meter (100 x 100, black border) is drawn half transparent at the cursor.
    const QImage shot = canvas.viewport()->grab().toImage();
    saveScreenshot(canvas, QStringLiteral("drag_preview.png"));
    const QPoint meterCenter = canvas.mapFromScene(QPointF(scenePoint) + QPointF(50, 50));
    const QPoint outside = canvas.mapFromScene(QPointF(scenePoint) + QPointF(130, 50));
    int darkInside = 0;
    for (int dx = -40; dx <= 40; ++dx) {
        if (qGray(shot.pixel(meterCenter + QPoint(dx, 0))) < 160) {
            ++darkInside;
        }
    }
    EXPECT_GT(darkInside, 2) << "needle and border of the preview are visible";
    EXPECT_GT(qGray(shot.pixel(outside)), 200) << "nothing is drawn next to the preview";

    QSignalSpy dropped(&canvas, &MaskCanvas::objectDropped);
    QDropEvent drop(QPointF(viewPoint), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
    ASSERT_EQ(dropped.count(), 1);
    EXPECT_EQ(dropped[0][0].value<ObjectType>(), ObjectType::OutputMeter);
    const auto target = dropped[0][1].value<std::optional<vtr::Placement>>();
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->object, 1000) << "the Data Mask under the cursor receives the meter";
    EXPECT_EQ(dropped[0][2].toPoint(), scenePoint);
}

TEST(ObjectPalette, ButtonsShowIconsAndExplainThemselvesInToolTips)
{
    Document document(Project(test::minimalPool(VtVersion::V3)));
    EditorContext context;
    context.setDocument(&document);
    ObjectPalette palette(&context);
    palette.resize(700, 120);
    palette.show();
    QApplication::processEvents();
    saveScreenshot(palette, QStringLiteral("palette.png"));

    const QList<PaletteButton*> buttons = palette.findChildren<PaletteButton*>();
    ASSERT_GT(buttons.size(), 40);
    for (const PaletteButton* button : buttons) {
        EXPECT_TRUE(button->text().isEmpty() || button->toolButtonStyle() == Qt::ToolButtonIconOnly);
        EXPECT_FALSE(button->icon().isNull());
        EXPECT_TRUE(button->toolTip().contains(ui::typeLabel(button->type()).toHtmlEscaped()));
        const bool needsNewerVersion = vt::schemaFor(button->type()).minVersion > VtVersion::V3;
        EXPECT_EQ(button->isEnabled(), !needsNewerVersion) << ui::typeLabel(button->type()).toStdString();
    }
}
