#include "TestPools.h"

#include "vtcore/PoolEditing.h"
#include "vtcore/ProjectFile.h"
#include "vtcore/Validator.h"

#include <gtest/gtest.h>

using namespace vt;

TEST(PoolEditing, UsersOfFindsAllReferenceKinds)
{
    ObjectPool pool = test::minimalPool();
    EXPECT_EQ(usersOf(pool, 100), (std::vector<ObjectId>{200}));                   // font attribute
    EXPECT_EQ(usersOf(pool, 200), (std::vector<ObjectId>{1000, 0}));               // children
    EXPECT_EQ(usersOf(pool, 1000), (std::vector<ObjectId>{0}));                    // active mask
    EXPECT_TRUE(usersOf(pool, kNullObjectId).empty());
}

TEST(PoolEditing, StripReferencesKeepsSoftKeyPositions)
{
    VtObject mask = VtObject::create(1, ObjectType::SoftKeyMask);
    mask.references = {10, 11, 12};
    EXPECT_TRUE(stripReferences(mask, 11));
    EXPECT_EQ(mask.references, (std::vector<ObjectId>{10, kNullObjectId, 12}));

    VtObject data = VtObject::create(2, ObjectType::DataMask);
    data.set("softKeyMask", 1);
    data.children = {{5, 0, 0}, {6, 1, 1}, {5, 2, 2}};
    data.macros = {{3, 5}};
    EXPECT_TRUE(stripReferences(data, 5));
    EXPECT_EQ(data.children, (std::vector<ChildRef>{{6, 1, 1}}));
    EXPECT_TRUE(data.macros.empty());
    EXPECT_TRUE(stripReferences(data, 1));
    EXPECT_EQ(data.get("softKeyMask"), kNullObjectId);
    EXPECT_FALSE(stripReferences(data, 99));
}

TEST(PoolEditing, DuplicateSubtreeCopiesContainedObjectsOnly)
{
    ObjectPool pool = test::minimalPool();
    VtObject& container = pool.add(VtObject::create(500, ObjectType::Container, "box"));
    container.children = {{200, 1, 2}};
    pool.find(1000)->children.push_back({500, 0, 0});

    const std::vector<VtObject> copies = duplicateSubtree(pool, 500);
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies[0].type, ObjectType::Container);
    EXPECT_EQ(copies[0].name, "box_copy");
    EXPECT_EQ(copies[1].type, ObjectType::OutputString);
    EXPECT_EQ(copies[1].name, "txtTitle_copy");
    EXPECT_FALSE(pool.contains(copies[0].id));
    EXPECT_FALSE(pool.contains(copies[1].id));
    EXPECT_NE(copies[0].id, copies[1].id);
    EXPECT_EQ(copies[0].children[0].id, copies[1].id) << "inner references point to the copies";
    EXPECT_EQ(copies[1].get("fontAttributes"), 100) << "shared attribute objects are not copied";

    for (VtObject copy : copies) {
        pool.add(std::move(copy));
    }
    pool.find(1000)->children.push_back({copies[0].id, 50, 50});
    EXPECT_FALSE(hasErrors(validatePool(pool)));
}

TEST(ObjectPool, InsertKeepsOrderAndIndex)
{
    ObjectPool pool = test::minimalPool();
    pool.insert(1, VtObject::create(7, ObjectType::NumberVariable));
    EXPECT_EQ(pool.objects()[1].id, 7);
    EXPECT_EQ(pool.indexOf(7), 1u);
    EXPECT_EQ(pool.indexOf(200), 2u);
    EXPECT_EQ(pool.find(200)->type, ObjectType::OutputString);
    EXPECT_FALSE(pool.indexOf(4711).has_value());
}

TEST(ObjectPool, SuggestIdKeepsMacrosInEightBitRange)
{
    ObjectPool pool = test::minimalPool();
    EXPECT_EQ(pool.suggestId(ObjectType::Macro), 1);
    EXPECT_EQ(pool.suggestId(ObjectType::DataMask), 256);
    pool.remove(0);
    EXPECT_EQ(pool.suggestId(ObjectType::WorkingSet), 0);
}

TEST(ProjectFile, DesignerSettingsRoundTrip)
{
    Project project{test::minimalPool()};
    project.designer.dataMaskSize = 240;
    project.designer.softKeyWidth = 60;
    project.designer.softKeyHeight = 32;
    project.designer.physicalSoftKeys = 10;
    EXPECT_EQ(projectFromJson(projectToJson(project)).designer, project.designer);
}
