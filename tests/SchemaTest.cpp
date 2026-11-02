#include "vtcore/Schema.h"
#include "vtcore/VtObject.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace vt;

TEST(Schema, CoversEveryObjectTypeUpToVersion5)
{
    ASSERT_EQ(allSchemas().size(), static_cast<std::size_t>(kObjectTypeCount));
    for (int type = 0; type < kObjectTypeCount; ++type) {
        const ObjectSchema* schema = findSchema(static_cast<std::uint8_t>(type));
        ASSERT_NE(schema, nullptr) << "type " << type;
        EXPECT_EQ(static_cast<int>(schema->type), type);
    }
    EXPECT_EQ(findSchema(std::uint8_t{45}), nullptr) << "VT version 6 objects are not supported yet";
    EXPECT_EQ(findSchema(std::uint8_t{240}), nullptr);
}

TEST(Schema, NamesAreUniqueAndResolvable)
{
    std::set<std::string> names;
    for (const ObjectSchema& schema : allSchemas()) {
        EXPECT_TRUE(names.insert(std::string(schema.name)).second) << schema.name;
        EXPECT_EQ(objectTypeFromName(schema.name), schema.type);

        std::set<std::string> attributes;
        for (const AttributeDef& attribute : schema.attributes) {
            EXPECT_TRUE(attributes.insert(std::string(attribute.name)).second)
                << schema.name << "." << attribute.name;
            EXPECT_GE(attribute.defaultValue, attribute.minValue) << schema.name << "." << attribute.name;
            EXPECT_LE(attribute.defaultValue, attribute.maxValue) << schema.name << "." << attribute.name;
        }
    }
}

TEST(Schema, EveryListHasExactlyOneCountBeforeIt)
{
    for (const ObjectSchema& schema : allSchemas()) {
        std::set<ListKind> counted;
        std::set<ListKind> listed;
        for (const RecordItem& item : schema.layout) {
            if (item.kind == RecordItem::Kind::Count) {
                EXPECT_TRUE(counted.insert(item.list).second) << schema.name;
                EXPECT_TRUE(item.countSize == 1 || item.countSize == 2 || item.countSize == 4) << schema.name;
            } else if (item.kind == RecordItem::Kind::List) {
                EXPECT_TRUE(counted.count(item.list) == 1) << schema.name << " lists before counting";
                EXPECT_TRUE(listed.insert(item.list).second) << schema.name;
            }
        }
        EXPECT_EQ(counted, listed) << schema.name;
    }
}

TEST(Schema, MinimumVersionsFollowTableA1)
{
    EXPECT_EQ(schemaFor(ObjectType::DataMask).minVersion, VtVersion::V3);
    EXPECT_EQ(schemaFor(ObjectType::WindowMask).minVersion, VtVersion::V4);
    EXPECT_EQ(schemaFor(ObjectType::OutputList).minVersion, VtVersion::V4);
    EXPECT_EQ(schemaFor(ObjectType::Animation).minVersion, VtVersion::V5);
    EXPECT_EQ(schemaFor(ObjectType::ExternalObjectPointer).minVersion, VtVersion::V5);
}

TEST(VtObject, CreateUsesSchemaDefaultsAndNamedAccess)
{
    VtObject number = VtObject::create(5, ObjectType::OutputNumber, "speed");
    EXPECT_EQ(number.get("scale"), 1.0);
    EXPECT_EQ(number.get("variableReference"), kNullObjectId);

    number.set("numberOfDecimals", 2);
    EXPECT_EQ(number.get("numberOfDecimals"), 2);
    EXPECT_THROW(number.get("doesNotExist"), std::out_of_range);
}
