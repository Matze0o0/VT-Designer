#include "../TestPools.h"

#include "Document.h"
#include "ObjectFactory.h"

#include "vtcore/PoolEditing.h"
#include "vtcore/Validator.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace vt;

TEST(Document, CommitUndoRedo)
{
    Document document(Project(test::minimalPool()));
    QSignalSpy changed(&document, &Document::poolChanged);
    EXPECT_FALSE(document.isModified());

    VtObject text = *document.pool().find(200);
    text.set("width", 99);
    document.commit(QStringLiteral("Resize"), {text});
    EXPECT_EQ(document.pool().find(200)->get("width"), 99);
    EXPECT_TRUE(document.isModified());
    EXPECT_EQ(changed.count(), 1);

    document.undoStack()->undo();
    EXPECT_EQ(document.pool().find(200)->get("width"), 120);
    EXPECT_FALSE(document.isModified());
    document.undoStack()->redo();
    EXPECT_EQ(document.pool().find(200)->get("width"), 99);
}

TEST(Document, MergeKeyCombinesDragSteps)
{
    Document document(Project(test::minimalPool()));
    VtObject mask = *document.pool().find(1000);
    for (int x = 11; x <= 15; ++x) {
        mask.children[0].x = static_cast<std::int16_t>(x);
        document.commit(QStringLiteral("Move"), {mask}, {}, 4711);
    }
    EXPECT_EQ(document.undoStack()->count(), 1);
    document.undoStack()->undo();
    EXPECT_EQ(document.pool().find(1000)->children[0].x, 10);
}

TEST(Document, AddAndRemoveRestoreOrder)
{
    Document document(Project(test::minimalPool()));
    const std::vector<ObjectId> before = [&] {
        std::vector<ObjectId> ids;
        for (const VtObject& o : document.pool().objects()) ids.push_back(o.id);
        return ids;
    }();

    document.removeObjects({100}, QStringLiteral("Delete font"));
    EXPECT_FALSE(document.pool().contains(100));
    EXPECT_EQ(document.pool().find(200)->get("fontAttributes"), kNullObjectId) << "references are stripped";

    document.undoStack()->undo();
    std::vector<ObjectId> after;
    for (const VtObject& o : document.pool().objects()) after.push_back(o.id);
    EXPECT_EQ(after, before) << "the object returns to its original position";
    EXPECT_EQ(document.pool().find(200)->get("fontAttributes"), 100);
}

TEST(Document, RemovePlacementDropsOrphansButKeepsSharedObjects)
{
    Project project(test::minimalPool());
    VtObject& box = project.pool.add(VtObject::create(500, ObjectType::Container));
    VtObject& label = project.pool.add(VtObject::create(501, ObjectType::OutputString));
    label.set("fontAttributes", 100);
    project.pool.find(500)->children.push_back({501, 0, 0});
    project.pool.find(1000)->children.push_back({500, 5, 5});
    (void)box;
    Document document(std::move(project));

    document.removePlacement(1000, 1, QStringLiteral("Remove container"));
    EXPECT_FALSE(document.pool().contains(500));
    EXPECT_FALSE(document.pool().contains(501));
    EXPECT_TRUE(document.pool().contains(100)) << "fonts are shared and stay";
    EXPECT_TRUE(document.pool().contains(200)) << "still used by the Working Set designator";
    EXPECT_EQ(document.pool().find(1000)->children.size(), 1u);

    document.undoStack()->undo();
    EXPECT_TRUE(document.pool().contains(500));
    EXPECT_TRUE(document.pool().contains(501));
    EXPECT_EQ(document.pool().find(1000)->children.size(), 2u);
}

TEST(Document, RenumberUpdatesReferences)
{
    Document document(Project(test::minimalPool()));
    EXPECT_FALSE(document.renumber(200, 100)) << "target ID in use";
    EXPECT_TRUE(document.renumber(200, 4000));
    EXPECT_FALSE(document.pool().contains(200));
    EXPECT_EQ(document.pool().find(1000)->children[0].id, 4000);
    EXPECT_EQ(document.pool().find(0)->children[0].id, 4000);
    EXPECT_FALSE(hasErrors(validatePool(document.pool())));

    document.undoStack()->undo();
    EXPECT_TRUE(document.pool().contains(200));
    EXPECT_EQ(document.pool().find(1000)->children[0].id, 200);
}

TEST(ObjectFactory, ReusesOrCreatesRequiredObjects)
{
    ObjectPool empty(VtVersion::V4);
    std::vector<VtObject> extra;
    const VtObject text = ui::createObject(empty, ObjectType::OutputString, extra);
    ASSERT_EQ(extra.size(), 1u);
    EXPECT_EQ(extra[0].type, ObjectType::FontAttributes);
    EXPECT_EQ(text.get("fontAttributes"), extra[0].id);
    EXPECT_NE(text.id, extra[0].id);

    const ObjectPool pool = test::minimalPool();
    extra.clear();
    const VtObject line = ui::createObject(pool, ObjectType::OutputLine, extra);
    ASSERT_EQ(extra.size(), 1u) << "no Line Attributes yet";
    extra.clear();
    const VtObject number = ui::createObject(pool, ObjectType::OutputNumber, extra);
    EXPECT_TRUE(extra.empty()) << "the existing font is reused";
    EXPECT_EQ(number.get("fontAttributes"), 100);
    (void)line;
}

TEST(ObjectFactory, NewWorkingSetGivesValidPool)
{
    ObjectPool pool(VtVersion::V4);
    std::vector<VtObject> extra;
    VtObject ws = ui::createObject(pool, ObjectType::WorkingSet, extra);
    for (VtObject& o : extra) {
        pool.add(std::move(o));
    }
    pool.add(std::move(ws));
    const auto diagnostics = validatePool(pool);
    EXPECT_FALSE(hasErrors(diagnostics));
    EXPECT_EQ(pool.workingSet()->id, 0);
}

TEST(ObjectFactory, DropTargetPicksDeepestAllowedContainer)
{
    std::vector<vtr::Placement> layout = {
        {1000, kNullObjectId, -1, QRect(0, 0, 480, 480), 0, false},
        {500, 1000, 0, QRect(10, 10, 100, 100), 1, true},
        {200, 500, 0, QRect(20, 20, 50, 20), 2, true},
    };
    ObjectPool pool = test::minimalPool();
    pool.add(VtObject::create(500, ObjectType::Container));
    const auto target = ui::dropTarget(pool, layout, QPoint(25, 25), ObjectType::OutputRectangle);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->object, 500) << "Output String cannot contain objects, the container can";
    const auto buttonTarget = ui::dropTarget(pool, layout, QPoint(300, 300), ObjectType::Button);
    ASSERT_TRUE(buttonTarget.has_value());
    EXPECT_EQ(buttonTarget->object, 1000);
}
