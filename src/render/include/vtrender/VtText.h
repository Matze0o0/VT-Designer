#pragma once

#include <QColor>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

class QPainter;

namespace vtr {

// Font Attributes "Font style" bits (ISO 11783-6, B.14.2).
enum FontStyle : int {
    FontBold = 0x01,
    FontCrossedOut = 0x02,
    FontUnderlined = 0x04,
    FontItalic = 0x08,
    FontInverted = 0x10,
    FontFlashingInverted = 0x20,
    FontFlashingHidden = 0x40,
    FontProportional = 0x80,
};

inline constexpr int kFontSizeCount = 15;

// Character cell of a non-proportional VT font, e.g. index 2 -> 8 x 12 pixels.
QSize fontCellSize(int fontSizeIndex);

// Strings starting with the byte order mark FF FE are WideStrings (UTF-16 little endian).
bool isWideString(const std::vector<std::uint8_t>& bytes);

// Decodes a VT string. 8-bit strings use the character set selected by the font type.
QString decodeVtString(const std::vector<std::uint8_t>& bytes, int fontType);

// Encodes text for a VT string. If a character is missing from the 8-bit character set the result is a
// WideString when allowWide is true; otherwise false is returned and out is left unchanged.
bool encodeVtString(const QString& text, int fontType, bool allowWide, std::vector<std::uint8_t>& out);

// Display text of a numeric field: ((raw + offset) * scale) with the given decimals.
QString formatVtNumber(double raw, double offset, double scale, int decimals, bool exponential, bool truncate);

// Splits text into display lines following ISO 11783-6 4.6.19 (line breaks, auto-wrap, trimming
// for the horizontal justification 0 = left, 1 = middle, 2 = right).
QStringList layoutVtText(const QString& text, int columns, bool autoWrap, bool wrapOnHyphen, int horizontal);

// Draws characters into fixed cells of the given size using a scaled monospace outline font.
void drawCellText(QPainter& painter, const QString& text, QPoint topLeft, QSize cell, int style, const QColor& colour);

} // namespace vtr
