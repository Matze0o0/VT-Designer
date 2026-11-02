#pragma once

#include "vtcore/VtObject.h"

#include <cstdint>
#include <span>
#include <vector>

namespace vt {

// Picture Graphic formats (B.12, "Format" attribute).
enum class PictureFormat : std::uint8_t {
    Monochrome = 0, // 1 bit per pixel, colour index 0 or 1
    Colour4Bit = 1, // 2 pixels per byte, colour index 0-15
    Colour8Bit = 2, // 1 pixel per byte, colour index 0-255
};

// Picture Graphic "Options" attribute bits.
inline constexpr std::uint8_t kPictureOptionTransparent = 0x01;
inline constexpr std::uint8_t kPictureOptionFlashing = 0x02;
inline constexpr std::uint8_t kPictureOptionRunLength = 0x04;

int bitsPerPixel(PictureFormat format);

// Bytes per pixel row; rows are padded with zero bits to whole bytes (B.12.2).
std::size_t rowStride(int width, PictureFormat format);

// Packs colour indices (one byte per pixel, row major) into the raw data format.
std::vector<std::uint8_t> packPixels(std::span<const std::uint8_t> indices, int width, int height,
                                     PictureFormat format);

// Unpacks raw data into one colour index per pixel. Throws std::invalid_argument if data is too short.
std::vector<std::uint8_t> unpackPixels(std::span<const std::uint8_t> raw, int width, int height,
                                       PictureFormat format);

// Run-length coding from B.12.2: pairs of (repeat count, value).
std::vector<std::uint8_t> rleEncode(std::span<const std::uint8_t> raw);
std::vector<std::uint8_t> rleDecode(std::span<const std::uint8_t> encoded);

// Decodes the colour indices of a Picture Graphic object (handles RLE).
std::vector<std::uint8_t> pictureIndices(const VtObject& picture);

// Converts an RGBA image (4 bytes per pixel, row major) into the given format using the standard
// palette and stores it in the Picture Graphic object: actual width/height, format, data, and the
// RLE option (set only when it makes the data smaller). Pixels with alpha < 128 become the object's
// transparency colour and switch on the transparent option. With dither, the quantisation error is
// spread to neighbouring pixels (Floyd-Steinberg), which suits photos better than flat graphics.
void setPictureFromRgba(VtObject& picture, std::span<const std::uint8_t> rgba, int width, int height,
                        PictureFormat format, bool dither = false);

} // namespace vt
