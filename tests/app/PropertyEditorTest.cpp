#include "../TestPools.h"

#include "Document.h"
#include "EditorContext.h"
#include "PropertyEditor.h"

#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>

#include <gtest/gtest.h>

using namespace vt;

namespace {

// Pool with two rectangles sharing Line Attributes 110, and a second Line Attributes object 111.
Project rectanglePool()
{
    Project project(test::minimalPool());
    VtObject& line = project.pool.add(VtObject::create(110, ObjectType::LineAttributes, "lineThin"));
    line.set("lineWidth", 1);
    project.pool.add(VtObject::create(111, ObjectType::LineAttributes, "lineThick")).set("lineWidth", 4);
    VtObject& a = project.pool.add(VtObject::create(400, ObjectType::OutputRectangle, "rectA"));
    a.set("lineAttributes", 110);
    VtObject& b = project.pool.add(VtObject::create(401, ObjectType::OutputRectangle, "rectB"));
    b.set("lineAttributes", 110);
    project.pool.find(1000)->children.push_back({400, 10, 10});
    project.pool.find(1000)->children.push_back({401, 100, 10});
    return project;
}

// Row with the given label below the row `parentLabel` (inline editors are children of the reference row).
QTreeWidgetItem* findRow(QTreeWidget* tree, const QString& parentLabel, const QString& label)
{
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->text(0) == label && item->parent() != nullptr && item->parent()->text(0) == parentLabel) {
            return item;
        }
        ++it;
    }
    return nullptr;
}

} // namespace

TEST(PropertyEditor, EditsReferencedLineAttributesInPlace)
{
    Document document(rectanglePool());
    EditorContext context;
    context.setDocument(&document);
    PropertyEditor editor(&context);
    editor.resize(400, 800);
    editor.show();
    context.select({400, 1000, 1});
    QApplication::processEvents();

    auto* tree = editor.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    if (const QString folder = qEnvironmentVariable("VTD_TEST_SCREENSHOTS"); !folder.isEmpty()) {
        editor.grab().save(folder + QStringLiteral("/property_editor_inline.png"));
    }
    QTreeWidgetItem* width = findRow(tree, QStringLiteral("Line attributes"), QStringLiteral("Line width"));
    ASSERT_NE(width, nullptr) << "line width is shown below the reference";
    auto* spin = qobject_cast<QSpinBox*>(tree->itemWidget(width, 1));
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->value(), 1);

    spin->setValue(3);
    EXPECT_EQ(document.pool().find(110)->get("lineWidth"), 3) << "the referenced object is changed";
    EXPECT_EQ(document.pool().find(400)->get("lineAttributes"), 110);

    // The line pattern is edited with one square per brush spot.
    QTreeWidgetItem* art = findRow(tree, QStringLiteral("Line attributes"), QStringLiteral("Line art"));
    ASSERT_NE(art, nullptr);
    const QList<QToolButton*> spots = tree->itemWidget(art, 1)->findChildren<QToolButton*>();
    ASSERT_EQ(spots.size(), 16);
    spots[1]->setChecked(false); // second spot from the left = bit 14
    EXPECT_EQ(document.pool().find(110)->get("lineArt"), 0xBFFF);

    // Pointing the rectangle at the other line attributes replaces the inline editors.
    VtObject rect = *document.pool().find(400);
    rect.set("lineAttributes", 111);
    document.commit(QStringLiteral("Change line attributes"), {rect});
    QApplication::processEvents();
    width = findRow(tree, QStringLiteral("Line attributes"), QStringLiteral("Line width"));
    ASSERT_NE(width, nullptr);
    spin = qobject_cast<QSpinBox*>(tree->itemWidget(width, 1));
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->value(), 4);
}

TEST(PropertyEditor, OwnCopySplitsSharedAttributeObject)
{
    Document document(rectanglePool());
    EditorContext context;
    context.setDocument(&document);
    PropertyEditor editor(&context);
    editor.resize(400, 800);
    editor.show();
    context.select({400, 1000, 1});
    QApplication::processEvents();

    QPushButton* ownCopy = nullptr;
    for (QPushButton* button : editor.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Own copy")) {
            ownCopy = button;
        }
    }
    ASSERT_NE(ownCopy, nullptr) << "the shared line attributes offer a copy";
    ownCopy->click();
    QApplication::processEvents();

    const auto copyId = static_cast<ObjectId>(document.pool().find(400)->get("lineAttributes"));
    EXPECT_NE(copyId, 110);
    ASSERT_TRUE(document.pool().contains(copyId));
    EXPECT_EQ(document.pool().find(copyId)->name, "lineThin_copy");
    EXPECT_EQ(document.pool().find(401)->get("lineAttributes"), 110) << "the other rectangle keeps the original";

    document.undoStack()->undo();
    EXPECT_EQ(document.pool().find(400)->get("lineAttributes"), 110);
    EXPECT_FALSE(document.pool().contains(copyId));
}
