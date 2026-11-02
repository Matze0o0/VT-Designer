#include "TestPools.h"

#include "vtcore/Hierarchy.h"
#include "vtcore/Validator.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace vt;

namespace {

bool hasMessage(const std::vector<Diagnostic>& diagnostics, Severity severity, const std::string& part)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) {
        return d.severity == severity && d.message.find(part) != std::string::npos;
    });
}

std::string dump(const std::vector<Diagnostic>& diagnostics)
{
    std::string text;
    for (const Diagnostic& d : diagnostics) {
        text += (d.severity == Severity::Error ? "E: " : "W: ") + d.message + "\n";
    }
    return text;
}

} // namespace

TEST(Hierarchy, SamplesFromTableA2)
{
    EXPECT_EQ(minVersionForChild(ObjectType::DataMask, ObjectType::Button), 2);
    EXPECT_EQ(minVersionForChild(ObjectType::AlarmMask, ObjectType::Button), 0);
    EXPECT_EQ(minVersionForChild(ObjectType::SoftKeyMask, ObjectType::Key), 2);
    EXPECT_EQ(minVersionForChild(ObjectType::SoftKeyMask, ObjectType::ObjectPointer), 2);
    EXPECT_EQ(minVersionForChild(ObjectType::DataMask, ObjectType::OutputList), 4);
    EXPECT_EQ(minVersionForChild(ObjectType::Key, ObjectType::OutputMeter), 4);
    EXPECT_EQ(minVersionForChild(ObjectType::DataMask, ObjectType::Animation), 5);
    EXPECT_EQ(minVersionForChild(ObjectType::WindowMask, ObjectType::AuxiliaryFunctionType2), 6);
    EXPECT_EQ(minVersionForChild(ObjectType::DataMask, ObjectType::WorkingSet), 3);
    EXPECT_EQ(minVersionForChild(ObjectType::OutputRectangle, ObjectType::OutputString), 0);
    EXPECT_EQ(minVersionForLabel(ObjectType::DataMask), 4);
    EXPECT_EQ(minVersionForLabel(ObjectType::Macro), 5);
    EXPECT_EQ(minVersionForLabelGraphic(ObjectType::PictureGraphic), 4);
    EXPECT_EQ(minVersionForLabelGraphic(ObjectType::Button), 0);
}

TEST(Validator, MinimalPoolIsClean)
{
    const std::vector<Diagnostic> diagnostics = validatePool(test::minimalPool());
    EXPECT_TRUE(diagnostics.empty()) << dump(diagnostics);
}

TEST(Validator, RequiresExactlyOneWorkingSet)
{
    ObjectPool pool = test::minimalPool();
    pool.remove(0);
    EXPECT_TRUE(hasMessage(validatePool(pool), Severity::Error, "no Working Set"));
}

TEST(Validator, ReportsForbiddenChild)
{
    ObjectPool pool = test::minimalPool();
    pool.add(VtObject::create(300, ObjectType::Button));
    VtObject& alarm = pool.add(VtObject::create(1100, ObjectType::AlarmMask));
    alarm.children.push_back({300, 0, 0});
    EXPECT_TRUE(hasMessage(validatePool(pool), Severity::Error, "cannot contain Button 300"));
}

TEST(Validator, ReportsChildThatNeedsNewerVersion)
{
    ObjectPool pool = test::minimalPool(VtVersion::V4);
    pool.add(VtObject::create(400, ObjectType::Animation));
    pool.find(1000)->children.push_back({400, 0, 0});
    const auto diagnostics = validatePool(pool);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "Animation 400 requires VT version 5")) << dump(diagnostics);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "only from VT version 5")) << dump(diagnostics);
}

TEST(Validator, ChecksObjectReferences)
{
    ObjectPool pool = test::minimalPool();
    pool.find(200)->set("fontAttributes", 4711);
    pool.find(1000)->set("softKeyMask", 200);
    const auto diagnostics = validatePool(pool);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "references missing object 4711")) << dump(diagnostics);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "must not reference Output String 200")) << dump(diagnostics);
}

TEST(Validator, ChecksAttributeRanges)
{
    ObjectPool pool = test::minimalPool();
    pool.find(100)->set("fontSize", 15);
    EXPECT_TRUE(hasMessage(validatePool(pool), Severity::Error, "allowed range is 0 to 14"));
}

TEST(Validator, SixteenBitMacroIdsNeedVersion5)
{
    ObjectPool pool = test::minimalPool(VtVersion::V4);
    pool.add(VtObject::create(300, ObjectType::Macro));
    pool.find(1000)->macros.push_back({3, 300});
    auto diagnostics = validatePool(pool);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "macro IDs above 255 require VT version 5"));
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "uses a 16-bit macro ID"));

    pool.setVersion(VtVersion::V5);
    diagnostics = validatePool(pool);
    EXPECT_FALSE(hasErrors(diagnostics)) << dump(diagnostics);
}

TEST(Validator, DetectsCircularContainment)
{
    ObjectPool pool = test::minimalPool();
    VtObject& a = pool.add(VtObject::create(500, ObjectType::Container));
    a.children.push_back({501, 0, 0});
    VtObject& b = pool.add(VtObject::create(501, ObjectType::Container));
    b.children.push_back({500, 0, 0});
    pool.find(1000)->children.push_back({500, 0, 0});
    EXPECT_TRUE(hasMessage(validatePool(pool), Severity::Error, "circular object hierarchy"));
}

TEST(Validator, ObjectPointerTargetMustFitParent)
{
    ObjectPool pool = test::minimalPool();
    pool.add(VtObject::create(300, ObjectType::Button));
    VtObject& pointer = pool.add(VtObject::create(301, ObjectType::ObjectPointer));
    pointer.set("value", 300);
    VtObject& alarm = pool.add(VtObject::create(1100, ObjectType::AlarmMask));
    alarm.children.push_back({301, 0, 0});
    const auto diagnostics = validatePool(pool);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "via Object Pointer 301")) << dump(diagnostics);
}

TEST(Validator, ReportsDuplicateNamesAndUnusedObjects)
{
    ObjectPool pool = test::minimalPool();
    pool.add(VtObject::create(600, ObjectType::NumberVariable, "fontNormal"));
    const auto diagnostics = validatePool(pool);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Error, "Name 'fontNormal' is used by")) << dump(diagnostics);
    EXPECT_TRUE(hasMessage(diagnostics, Severity::Warning, "is not used by any other object")) << dump(diagnostics);
}

TEST(Validator, ChecksPictureDataLength)
{
    ObjectPool pool = test::minimalPool();
    VtObject& picture = pool.add(VtObject::create(700, ObjectType::PictureGraphic));
    picture.set("actualWidth", 10);
    picture.set("actualHeight", 10);
    picture.set("format", 2);
    picture.data.assign(50, 0);
    pool.find(1000)->children.push_back({700, 0, 0});
    EXPECT_TRUE(hasMessage(validatePool(pool), Severity::Error, "bitmap data is shorter"));
}
