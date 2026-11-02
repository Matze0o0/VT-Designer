#include "TestPools.h"

#include "vtcore/Exporters.h"
#include "vtcore/IopCodec.h"

#include <gtest/gtest.h>

using namespace vt;

namespace {

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(Exporters, ObjectSymbolFallsBackToTypeAndId)
{
    const ExportOptions options;
    EXPECT_EQ(objectSymbol(VtObject::create(5, ObjectType::DataMask, "dmMain"), options), "dmMain");
    EXPECT_EQ(objectSymbol(VtObject::create(5, ObjectType::DataMask), options), "DataMask_5");
    EXPECT_EQ(objectSymbol(VtObject::create(5, ObjectType::DataMask, "not valid"), options), "DataMask_5");

    ExportOptions prefixed;
    prefixed.symbolPrefix = "VT_";
    EXPECT_EQ(objectSymbol(VtObject::create(5, ObjectType::DataMask, "dmMain"), prefixed), "VT_dmMain");
}

TEST(Exporters, IdHeaderStyles)
{
    const ObjectPool pool = test::minimalPool();
    ExportOptions options;
    options.poolName = "sprayer";

    const std::string defines = exportIdHeader(pool, options);
    EXPECT_TRUE(contains(defines, "#ifndef SPRAYER_IDS_H"));
    EXPECT_TRUE(contains(defines, "#define dmMain     1000u /* Data Mask */"));
    EXPECT_TRUE(contains(defines, "#define ws         0u /* Working Set */"));
    EXPECT_LT(defines.find("ws "), defines.find("dmMain")) << "sorted by ID";

    options.headerStyle = HeaderStyle::Enum;
    EXPECT_TRUE(contains(exportIdHeader(pool, options), "    dmMain     = 1000, /* Data Mask */"));

    options.headerStyle = HeaderStyle::Constexpr;
    EXPECT_TRUE(contains(exportIdHeader(pool, options), "inline constexpr std::uint16_t dmMain     = 1000;"));
}

TEST(Exporters, PoolArray)
{
    ExportOptions options;
    options.poolName = "sprayer";
    const std::vector<std::uint8_t> iop = {0x00, 0x01, 0xAB};

    const std::string header = exportPoolArrayHeader(options);
    EXPECT_TRUE(contains(header, "extern const uint8_t sprayer_data[];"));
    EXPECT_TRUE(contains(header, "extern const uint32_t sprayer_size;"));

    const std::string source = exportPoolArraySource(iop, options);
    EXPECT_TRUE(contains(source, "#include \"sprayer_data.h\""));
    EXPECT_TRUE(contains(source, "const uint32_t sprayer_size = 3u;"));
    EXPECT_TRUE(contains(source, "    0x00, 0x01, 0xAB\n};"));
}

TEST(Exporters, PoolArrayWrapsLines)
{
    const std::vector<std::uint8_t> iop(17, 0x11);
    const std::string source = exportPoolArraySource(iop, ExportOptions{});
    EXPECT_TRUE(contains(source, "0x11, 0x11,\n    0x11\n};"));
}
