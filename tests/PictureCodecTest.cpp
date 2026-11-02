#include "vtcore/Palette.h"
#include "vtcore/PictureCodec.h"

#include <gtest/gtest.h>

using namespace vt;
using Bytes = std::vector<std::uint8_t>;

TEST(PictureCodec, RunLengthExampleFromB122)
{
    const Bytes raw = {0, 0, 0, 0, 0, 0, 3, 3, 3, 1, 1, 2};
    const Bytes encoded = {6, 0, 3, 3, 2, 1, 1, 2};
    EXPECT_EQ(rleEncode(raw), encoded);
    EXPECT_EQ(rleDecode(encoded), raw);
}

TEST(PictureCodec, RunLengthSplitsLongRunsAndSkipsZeroCounts)
{
    const Bytes raw(300, 7);
    const Bytes encoded = rleEncode(raw);
    EXPECT_EQ(encoded, (Bytes{255, 7, 45, 7}));
    EXPECT_EQ(rleDecode(encoded), raw);
    EXPECT_EQ(rleDecode(Bytes{0, 9, 2, 5}), (Bytes{5, 5}));
}

TEST(PictureCodec, MonochromePaddingExampleFromB122)
{
    // 10 pixels wide, 2 lines, all white: each line is FF C0.
    const Bytes indices(20, 1);
    const Bytes raw = packPixels(indices, 10, 2, PictureFormat::Monochrome);
    EXPECT_EQ(raw, (Bytes{0xFF, 0xC0, 0xFF, 0xC0}));
    EXPECT_EQ(unpackPixels(raw, 10, 2, PictureFormat::Monochrome), indices);
}

TEST(PictureCodec, FourBitOddWidthPadsLowNibble)
{
    const Bytes indices = {1, 2, 3, 4, 5, 6};
    const Bytes raw = packPixels(indices, 3, 2, PictureFormat::Colour4Bit);
    EXPECT_EQ(raw, (Bytes{0x12, 0x30, 0x45, 0x60}));
    EXPECT_EQ(unpackPixels(raw, 3, 2, PictureFormat::Colour4Bit), indices);
}

TEST(PictureCodec, UnpackRejectsShortData)
{
    EXPECT_THROW(unpackPixels(Bytes{0xFF}, 10, 2, PictureFormat::Monochrome), std::invalid_argument);
}

TEST(PictureCodec, SetPictureFromRgbaQuantizesAndCompresses)
{
    // 8x2 image: first row red, second row fully transparent.
    Bytes rgba;
    for (int i = 0; i < 8; ++i) {
        rgba.insert(rgba.end(), {255, 0, 0, 255});
    }
    for (int i = 0; i < 8; ++i) {
        rgba.insert(rgba.end(), {0, 0, 0, 0});
    }

    VtObject picture = VtObject::create(1, ObjectType::PictureGraphic);
    picture.set("transparencyColour", 1);
    setPictureFromRgba(picture, rgba, 8, 2, PictureFormat::Colour8Bit);

    EXPECT_EQ(picture.get("actualWidth"), 8);
    EXPECT_EQ(picture.get("actualHeight"), 2);
    EXPECT_EQ(picture.get("width"), 8);
    const auto options = static_cast<std::uint8_t>(picture.get("options"));
    EXPECT_NE(options & kPictureOptionTransparent, 0);
    EXPECT_NE(options & kPictureOptionRunLength, 0);
    EXPECT_EQ(picture.data, (Bytes{8, 12, 8, 1}));

    const Bytes indices = pictureIndices(picture);
    EXPECT_EQ(indices[0], 12); // red
    EXPECT_EQ(indices[15], 1); // transparency colour
}

TEST(Palette, MatchesTableA4)
{
    EXPECT_EQ(standardColour(0), 0xFF000000u);
    EXPECT_EQ(standardColour(1), 0xFFFFFFFFu);
    EXPECT_EQ(standardColour(7), 0xFFCCCCCCu);
    EXPECT_EQ(standardColour(15), 0xFF000099u);
    EXPECT_EQ(standardColour(16), 0xFF000000u);
    EXPECT_EQ(standardColour(52), 0xFF330000u);
    EXPECT_EQ(standardColour(64), 0xFF336600u);
    EXPECT_EQ(standardColour(192), 0xFFCCFF66u);
    EXPECT_EQ(standardColour(231), 0xFFFFFFFFu);
}

TEST(Palette, NearestColourRespectsFormatLimit)
{
    EXPECT_EQ(nearestColourIndex(250, 10, 10, 15), 12);
    EXPECT_EQ(nearestColourIndex(200, 200, 200, 1), 1);
    EXPECT_EQ(nearestColourIndex(40, 40, 40, 1), 0);
    EXPECT_EQ(nearestColourIndex(0x33, 0x66, 0x00, 231), 64);
}
