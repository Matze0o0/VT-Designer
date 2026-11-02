#pragma once

#include <cstdint>

namespace vt {

// Number of colour indices in the standard palette that have a defined colour. Indices 232-255
// are proprietary to the VT and are treated as black.
inline constexpr int kStandardColourCount = 232;

// Standard VT colour palette (ISO 11783-6, Table A.4) as 0xAARRGGBB.
std::uint32_t standardColour(std::uint8_t index);

// Closest standard palette colour (squared RGB distance) among the indices 0..maxIndex.
// Use maxIndex 1 for monochrome, 15 for 4-bit and 231 for 8-bit pictures.
std::uint8_t nearestColourIndex(std::uint8_t r, std::uint8_t g, std::uint8_t b, int maxIndex);

} // namespace vt
