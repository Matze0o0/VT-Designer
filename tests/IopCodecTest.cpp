#include "TestPools.h"

#include "vtcore/IopCodec.h"
#include "vtcore/Schema.h"

#include <gtest/gtest.h>

using namespace vt;
using Bytes = std::vector<std::uint8_t>;

TEST(IopCodec, OutputStringRecordMatchesTableB22)
{
    VtObject text = VtObject::create(200, ObjectType::OutputString);
    text.set("fontAttributes", 100);
    text.data = test::latin1("Sprayer");

    const Bytes expected = {0xC8, 0x00, 0x0B,             // Object ID 200, type 11
                            0x78, 0x00, 0x18, 0x00,       // width 120, height 24
                            0x01,                         // background colour
                            0x64, 0x00,                   // font attributes 100
                            0x00,                         // options
                            0xFF, 0xFF,                   // variable reference NULL
                            0x00,                         // justification
                            0x07, 0x00,                   // length (2 bytes)
                            'S', 'p', 'r', 'a', 'y', 'e', 'r',
                            0x00};                        // number of macros
    EXPECT_EQ(encodeObject(text), expected);
}

TEST(IopCodec, ExtendedInputAttributesExampleFromB146)
{
    // Encoding example given in ISO 11783-6, B.14.6.
    const Bytes example = {0x34, 0x12, 0x26, 0x00, 0x02, 0x00, 0x02, 0x41, 0x00, 0x4F, 0x00,
                           0x61, 0x00, 0x6F, 0x00, 0x01, 0x01, 0x12, 0xAB, 0x1F, 0xAB};
    std::size_t offset = 0;
    const VtObject object = decodeObject(example, offset);

    EXPECT_EQ(offset, example.size());
    EXPECT_EQ(object.id, 0x1234);
    EXPECT_EQ(object.type, ObjectType::ExtendedInputAttributes);
    ASSERT_EQ(object.codePlanes.size(), 2u);
    EXPECT_EQ(object.codePlanes[0].plane, 0);
    ASSERT_EQ(object.codePlanes[0].ranges.size(), 2u);
    EXPECT_EQ(object.codePlanes[0].ranges[0], (CharacterRange{0x41, 0x4F}));
    EXPECT_EQ(object.codePlanes[0].ranges[1], (CharacterRange{0x61, 0x6F}));
    EXPECT_EQ(object.codePlanes[1].plane, 1);
    EXPECT_EQ(object.codePlanes[1].ranges[0], (CharacterRange{0xAB12, 0xAB1F}));

    EXPECT_EQ(encodeObject(object), example);
}

TEST(IopCodec, SixteenBitMacroReferenceExampleFrom46223)
{
    // Container referencing macro 7000 (0x1B58) on hide (event 4): FF 58 04 1B, counted as 2 groupings.
    VtObject container = VtObject::create(1, ObjectType::Container);
    container.macros.push_back({4, 7000});
    container.macros.push_back({4, 7});

    const Bytes bytes = encodeObject(container);
    const Bytes expectedTail = {0x00, 0x03, 0xFF, 0x58, 0x04, 0x1B, 0x04, 0x07};
    ASSERT_GE(bytes.size(), expectedTail.size());
    EXPECT_EQ(Bytes(bytes.end() - static_cast<std::ptrdiff_t>(expectedTail.size()), bytes.end()), expectedTail);

    std::size_t offset = 0;
    EXPECT_EQ(decodeObject(bytes, offset), container);
}

TEST(IopCodec, SignedPositionsAndValuesRoundTrip)
{
    VtObject mask = VtObject::create(1000, ObjectType::DataMask);
    mask.children.push_back({200, -15, -32768});
    VtObject number = VtObject::create(201, ObjectType::OutputNumber);
    number.set("offset", -123456);
    number.set("scale", 0.1);

    for (const VtObject& object : {mask, number}) {
        const Bytes bytes = encodeObject(object);
        std::size_t offset = 0;
        const VtObject decoded = decodeObject(bytes, offset);
        EXPECT_EQ(encodeObject(decoded), bytes);
    }

    std::size_t offset = 0;
    const Bytes maskBytes = encodeObject(mask);
    EXPECT_EQ(decodeObject(maskBytes, offset).children[0], (ChildRef{200, -15, -32768}));
    offset = 0;
    const Bytes numberBytes = encodeObject(number);
    EXPECT_FLOAT_EQ(static_cast<float>(decodeObject(numberBytes, offset).get("scale")), 0.1f);
}

// Builds one object of every type with every list filled and every attribute away from its default.
ObjectPool everyTypePool()
{
    ObjectPool pool(VtVersion::V5);
    for (const ObjectSchema& schema : allSchemas()) {
        VtObject object = VtObject::create(static_cast<ObjectId>(1000 + static_cast<int>(schema.type)), schema.type);
        for (std::size_t i = 0; i < schema.attributes.size(); ++i) {
            const AttributeDef& def = schema.attributes[i];
            switch (def.type) {
            case FieldType::Float32: object.attributes[i] = 0.25; break;
            case FieldType::Int16:
            case FieldType::Int32: object.attributes[i] = def.minValue; break;
            default: object.attributes[i] = def.maxValue; break;
            }
        }
        if (schema.hasList(ListKind::Children)) object.children = {{1, -5, 7}, {2, 300, -400}};
        if (schema.hasList(ListKind::References)) object.references = {3, kNullObjectId};
        if (schema.hasList(ListKind::Macros)) object.macros = {{1, 5}, {25, 4000}};
        if (schema.hasList(ListKind::Points)) object.points = {{0, 0}, {10, 0}, {5, 9}};
        if (schema.hasList(ListKind::Data)) object.data = {1, 2, 3, 250};
        if (schema.hasList(ListKind::Languages)) object.languages = {"de", "fr"};
        if (schema.hasList(ListKind::Labels)) object.labels = {{4, 5, 1, kNullObjectId}};
        if (schema.hasList(ListKind::CodePlanes)) object.codePlanes = {{0, {{0x41, 0x5A}}}, {2, {}}};
        pool.add(std::move(object));
    }
    return pool;
}

TEST(IopCodec, EveryObjectTypeRoundTripsByteIdentical)
{
    const ObjectPool pool = everyTypePool();
    const Bytes bytes = encodePool(pool);
    const ObjectPool decoded = decodePool(bytes, VtVersion::V5);

    ASSERT_EQ(decoded.size(), pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i) {
        EXPECT_EQ(decoded.objects()[i], pool.objects()[i]) << objectTypeName(pool.objects()[i].type);
    }
    EXPECT_EQ(encodePool(decoded), bytes);
}

TEST(IopCodec, MinimalPoolRoundTrip)
{
    const ObjectPool pool = test::minimalPool();
    const Bytes bytes = encodePool(pool);
    const ObjectPool decoded = decodePool(bytes, pool.version());
    EXPECT_EQ(encodePool(decoded), bytes);
    EXPECT_EQ(decoded.find(0)->languages, (std::vector<std::string>{"de", "en"}));
}

TEST(IopCodec, RejectsTruncatedData)
{
    Bytes bytes = encodePool(test::minimalPool());
    bytes.pop_back();
    EXPECT_THROW(decodePool(bytes, VtVersion::V4), IopDecodeError);
}

TEST(IopCodec, RejectsUnknownObjectType)
{
    const Bytes bytes = {0x01, 0x00, 0x2E, 0x00}; // type 46 is a VT version 6 object
    try {
        decodePool(bytes, VtVersion::V4);
        FAIL() << "expected IopDecodeError";
    } catch (const IopDecodeError& e) {
        EXPECT_EQ(e.offset(), 3u);
    }
}

TEST(IopCodec, RejectsDuplicateIds)
{
    const Bytes record = encodeObject(VtObject::create(7, ObjectType::NumberVariable));
    Bytes bytes = record;
    bytes.insert(bytes.end(), record.begin(), record.end());
    EXPECT_THROW(decodePool(bytes, VtVersion::V4), IopDecodeError);
}

TEST(IopCodec, EncodingFailsForOversizedLists)
{
    VtObject input = VtObject::create(1, ObjectType::InputString);
    input.data.assign(256, 'x'); // Input String length is a single byte
    EXPECT_THROW(encodeObject(input), IopEncodeError);

    VtObject font = VtObject::create(2, ObjectType::FontAttributes);
    font.set("fontColour", 256);
    EXPECT_THROW(encodeObject(font), IopEncodeError);
}
