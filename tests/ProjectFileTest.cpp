#include "TestPools.h"

#include "vtcore/IopCodec.h"
#include "vtcore/ProjectFile.h"

#include <gtest/gtest.h>

using namespace vt;

TEST(ProjectFile, RoundTripKeepsPoolNamesAndOptions)
{
    Project project{test::minimalPool(VtVersion::V5)};
    project.exportOptions.poolName = "sprayer";
    project.exportOptions.symbolPrefix = "VT_";
    project.exportOptions.headerStyle = HeaderStyle::Enum;

    VtObject& number = project.pool.add(VtObject::create(300, ObjectType::OutputNumber, "numSpeed"));
    number.set("scale", 0.1);
    number.set("offset", -40);

    VtObject& picture = project.pool.add(VtObject::create(301, ObjectType::PictureGraphic));
    picture.data = {0x00, 0xFF, 0x10, 0x80};

    VtObject& macro = project.pool.add(VtObject::create(302, ObjectType::Macro));
    macro.data = {0xA8, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    project.pool.find(1000)->macros.push_back({1, 302});

    const std::string json = projectToJson(project);
    EXPECT_NE(json.find("\"scale\": 0.1"), std::string::npos) << "floats are written in shortest form";
    EXPECT_NE(json.find("\"text\": \"Sprayer\""), std::string::npos);

    const Project loaded = projectFromJson(json);
    EXPECT_EQ(loaded.pool.version(), VtVersion::V5);
    EXPECT_EQ(loaded.exportOptions.poolName, "sprayer");
    EXPECT_EQ(loaded.exportOptions.symbolPrefix, "VT_");
    EXPECT_EQ(loaded.exportOptions.headerStyle, HeaderStyle::Enum);
    ASSERT_EQ(loaded.pool.size(), project.pool.size());
    for (std::size_t i = 0; i < project.pool.size(); ++i) {
        EXPECT_EQ(loaded.pool.objects()[i], project.pool.objects()[i]);
    }
    EXPECT_EQ(encodePool(loaded.pool), encodePool(project.pool));
}

TEST(ProjectFile, EightBitTextKeepsEveryByte)
{
    Project project{test::minimalPool()};
    project.pool.find(200)->data = {'D', 0xFC, 'n', 'g', 'e', 'r', 0xA4}; // "Dünger" + byte 0xA4

    const Project loaded = projectFromJson(projectToJson(project));
    EXPECT_EQ(loaded.pool.find(200)->data, project.pool.find(200)->data);
}

TEST(ProjectFile, WideStringsAreStoredAsUnicodeText)
{
    Project project{test::minimalPool()};
    // WideString "Ж€" with byte order mark, UTF-16 little endian.
    project.pool.find(200)->data = {0xFF, 0xFE, 0x16, 0x04, 0xAC, 0x20};

    const std::string json = projectToJson(project);
    EXPECT_NE(json.find("\"wideText\": \"Ж€\""), std::string::npos) << json;
    EXPECT_EQ(projectFromJson(json).pool.find(200)->data, project.pool.find(200)->data);
}

TEST(ProjectFile, RejectsBrokenInput)
{
    EXPECT_THROW(projectFromJson("{"), ProjectError);
    EXPECT_THROW(projectFromJson(R"({"format":"something-else"})"), ProjectError);
    EXPECT_THROW(projectFromJson(R"({"format":"vt-designer-project","vtVersion":9})"), ProjectError);
    EXPECT_THROW(projectFromJson(
                     R"({"format":"vt-designer-project","vtVersion":4,"objects":[{"id":1,"type":"Nope"}]})"),
                 ProjectError);
    EXPECT_THROW(projectFromJson(R"({"format":"vt-designer-project","vtVersion":4,
                                     "objects":[{"id":1,"type":"DataMask","attributes":{"colour":3}}]})"),
                 ProjectError);
    EXPECT_THROW(projectFromJson(R"({"format":"vt-designer-project","vtVersion":4,
                                     "objects":[{"id":1,"type":"DataMask"},{"id":1,"type":"DataMask"}]})"),
                 ProjectError);
}
