#include "vtcore/PictureCodec.h"

#include "vtcore/Palette.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace vt {

namespace {

int maxIndexFor(PictureFormat format)
{
    switch (format) {
    case PictureFormat::Monochrome: return 1;
    case PictureFormat::Colour4Bit: return 15;
    case PictureFormat::Colour8Bit: return kStandardColourCount - 1;
    }
    return 1;
}

void checkDimensions(int width, int height)
{
    if (width < 0 || height < 0 || width > 65535 || height > 65535) {
        throw std::invalid_argument("Picture dimensions out of range");
    }
}

} // namespace

int bitsPerPixel(PictureFormat format)
{
    switch (format) {
    case PictureFormat::Monochrome: return 1;
    case PictureFormat::Colour4Bit: return 4;
    case PictureFormat::Colour8Bit: return 8;
    }
    return 8;
}

std::size_t rowStride(int width, PictureFormat format)
{
    return (static_cast<std::size_t>(width) * static_cast<std::size_t>(bitsPerPixel(format)) + 7) / 8;
}

std::vector<std::uint8_t> packPixels(std::span<const std::uint8_t> indices, int width, int height,
                                     PictureFormat format)
{
    checkDimensions(width, height);
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (indices.size() < pixelCount) {
        throw std::invalid_argument("Not enough pixels for the picture size");
    }

    const int bpp = bitsPerPixel(format);
    const unsigned mask = (1u << bpp) - 1;
    const std::size_t stride = rowStride(width, format);
    std::vector<std::uint8_t> raw(stride * static_cast<std::size_t>(height), 0);

    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = raw.data() + stride * static_cast<std::size_t>(y);
        for (int x = 0; x < width; ++x) {
            const unsigned value = indices[static_cast<std::size_t>(y) * width + x] & mask;
            const std::size_t bit = static_cast<std::size_t>(x) * bpp;
            // Most significant bits hold the leftmost pixel.
            const int shift = 8 - bpp - static_cast<int>(bit % 8);
            row[bit / 8] = static_cast<std::uint8_t>(row[bit / 8] | (value << shift));
        }
    }
    return raw;
}

std::vector<std::uint8_t> unpackPixels(std::span<const std::uint8_t> raw, int width, int height,
                                       PictureFormat format)
{
    checkDimensions(width, height);
    const int bpp = bitsPerPixel(format);
    const unsigned mask = (1u << bpp) - 1;
    const std::size_t stride = rowStride(width, format);
    if (raw.size() < stride * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("Picture data is shorter than " + std::to_string(width) + "x" +
                                    std::to_string(height) + " pixels require");
    }

    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = raw.data() + stride * static_cast<std::size_t>(y);
        for (int x = 0; x < width; ++x) {
            const std::size_t bit = static_cast<std::size_t>(x) * bpp;
            const int shift = 8 - bpp - static_cast<int>(bit % 8);
            indices[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>((row[bit / 8] >> shift) & mask);
        }
    }
    return indices;
}

std::vector<std::uint8_t> rleEncode(std::span<const std::uint8_t> raw)
{
    std::vector<std::uint8_t> encoded;
    std::size_t i = 0;
    while (i < raw.size()) {
        const std::uint8_t value = raw[i];
        std::size_t run = 1;
        while (i + run < raw.size() && raw[i + run] == value && run < 255) {
            ++run;
        }
        encoded.push_back(static_cast<std::uint8_t>(run));
        encoded.push_back(value);
        i += run;
    }
    return encoded;
}

std::vector<std::uint8_t> rleDecode(std::span<const std::uint8_t> encoded)
{
    std::vector<std::uint8_t> raw;
    // A trailing single byte cannot form a pair and is ignored; a zero count skips the pair.
    for (std::size_t i = 0; i + 1 < encoded.size(); i += 2) {
        raw.insert(raw.end(), encoded[i], encoded[i + 1]);
    }
    return raw;
}

std::vector<std::uint8_t> pictureIndices(const VtObject& picture)
{
    const int width = static_cast<int>(picture.get("actualWidth"));
    const int height = static_cast<int>(picture.get("actualHeight"));
    const auto format = static_cast<PictureFormat>(static_cast<int>(picture.get("format")));
    const auto options = static_cast<std::uint8_t>(picture.get("options"));
    if ((options & kPictureOptionRunLength) != 0) {
        return unpackPixels(rleDecode(picture.data), width, height, format);
    }
    return unpackPixels(picture.data, width, height, format);
}

void setPictureFromRgba(VtObject& picture, std::span<const std::uint8_t> rgba, int width, int height,
                        PictureFormat format, bool dither)
{
    checkDimensions(width, height);
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgba.size() < pixelCount * 4) {
        throw std::invalid_argument("Not enough RGBA data for the picture size");
    }

    const auto transparencyIndex = static_cast<std::uint8_t>(picture.get("transparencyColour"));
    const int maxIndex = maxIndexFor(format);
    bool hasTransparentPixels = false;

    // Working copy of the colour channels so dithering can carry the error forward.
    std::vector<float> channels(pixelCount * 3);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            channels[i * 3 + c] = rgba[i * 4 + c];
        }
    }

    auto spread = [&](int x, int y, const float* error, float weight) {
        if (x < 0 || x >= width || y >= height) {
            return;
        }
        float* target = &channels[(static_cast<std::size_t>(y) * width + x) * 3];
        for (int c = 0; c < 3; ++c) {
            target[c] += error[c] * weight;
        }
    };

    std::vector<std::uint8_t> indices(pixelCount);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            if (rgba[i * 4 + 3] < 128) {
                indices[i] = transparencyIndex;
                hasTransparentPixels = true;
                continue;
            }
            const float* px = &channels[i * 3];
            auto clampByte = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
            const std::uint8_t index = nearestColourIndex(clampByte(px[0]), clampByte(px[1]), clampByte(px[2]), maxIndex);
            indices[i] = index;
            if (dither) {
                const std::uint32_t c = standardColour(index);
                const float error[3] = {px[0] - static_cast<float>((c >> 16) & 0xFF),
                                        px[1] - static_cast<float>((c >> 8) & 0xFF),
                                        px[2] - static_cast<float>(c & 0xFF)};
                spread(x + 1, y, error, 7.0f / 16);
                spread(x - 1, y + 1, error, 3.0f / 16);
                spread(x, y + 1, error, 5.0f / 16);
                spread(x + 1, y + 1, error, 1.0f / 16);
            }
        }
    }

    const std::vector<std::uint8_t> raw = packPixels(indices, width, height, format);
    std::vector<std::uint8_t> compressed = rleEncode(raw);

    auto options = static_cast<std::uint8_t>(picture.get("options"));
    options = static_cast<std::uint8_t>(options & ~(kPictureOptionRunLength | kPictureOptionTransparent));
    if (hasTransparentPixels) {
        options |= kPictureOptionTransparent;
    }
    if (compressed.size() < raw.size()) {
        options |= kPictureOptionRunLength;
        picture.data = std::move(compressed);
    } else {
        picture.data = raw;
    }

    picture.set("actualWidth", width);
    picture.set("actualHeight", height);
    picture.set("format", static_cast<int>(format));
    picture.set("options", options);
    if (picture.get("width") == 0) {
        picture.set("width", width);
    }
}

} // namespace vt
