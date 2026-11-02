#include "vtcore/Palette.h"

#include <algorithm>
#include <array>

namespace vt {

namespace {

constexpr std::uint32_t argb(std::uint32_t r, std::uint32_t g, std::uint32_t b)
{
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

constexpr std::array<std::uint32_t, 256> buildPalette()
{
    std::array<std::uint32_t, 256> palette{};

    // Indices 0-15: named colours.
    constexpr std::array<std::uint32_t, 16> named = {
        argb(0x00, 0x00, 0x00), // 0 black
        argb(0xFF, 0xFF, 0xFF), // 1 white
        argb(0x00, 0x99, 0x00), // 2 green
        argb(0x00, 0x99, 0x99), // 3 teal
        argb(0x99, 0x00, 0x00), // 4 maroon
        argb(0x99, 0x00, 0x99), // 5 purple
        argb(0x99, 0x99, 0x00), // 6 olive
        argb(0xCC, 0xCC, 0xCC), // 7 silver
        argb(0x99, 0x99, 0x99), // 8 grey
        argb(0x00, 0x00, 0xFF), // 9 blue
        argb(0x00, 0xFF, 0x00), // 10 lime
        argb(0x00, 0xFF, 0xFF), // 11 cyan
        argb(0xFF, 0x00, 0x00), // 12 red
        argb(0xFF, 0x00, 0xFF), // 13 magenta
        argb(0xFF, 0xFF, 0x00), // 14 yellow
        argb(0x00, 0x00, 0x99), // 15 navy
    };
    for (std::size_t i = 0; i < named.size(); ++i) {
        palette[i] = named[i];
    }

    // Indices 16-231: 6x6x6 colour cube with the levels 00, 33, 66, 99, CC, FF (blue varies fastest).
    std::size_t index = 16;
    for (std::uint32_t r = 0; r < 6; ++r) {
        for (std::uint32_t g = 0; g < 6; ++g) {
            for (std::uint32_t b = 0; b < 6; ++b) {
                palette[index++] = argb(r * 0x33, g * 0x33, b * 0x33);
            }
        }
    }

    // Indices 232-255 are proprietary; black is used as stand-in.
    for (; index < palette.size(); ++index) {
        palette[index] = argb(0, 0, 0);
    }
    return palette;
}

constexpr std::array<std::uint32_t, 256> kPalette = buildPalette();

} // namespace

std::uint32_t standardColour(std::uint8_t index)
{
    return kPalette[index];
}

std::uint8_t nearestColourIndex(std::uint8_t r, std::uint8_t g, std::uint8_t b, int maxIndex)
{
    const int last = std::clamp(maxIndex, 0, kStandardColourCount - 1);
    int best = 0;
    int bestDistance = 1 << 30;
    for (int i = 0; i <= last; ++i) {
        const std::uint32_t c = kPalette[static_cast<std::size_t>(i)];
        const int dr = static_cast<int>((c >> 16) & 0xFF) - r;
        const int dg = static_cast<int>((c >> 8) & 0xFF) - g;
        const int db = static_cast<int>(c & 0xFF) - b;
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return static_cast<std::uint8_t>(best);
}

} // namespace vt
