#include "vtrender/VtText.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace vtr {

namespace {

constexpr std::array<QSize, kFontSizeCount> kCells = {
    QSize(6, 8),    QSize(8, 8),    QSize(8, 12),   QSize(12, 16),  QSize(16, 16),
    QSize(16, 24),  QSize(24, 32),  QSize(32, 32),  QSize(32, 48),  QSize(48, 64),
    QSize(64, 64),  QSize(64, 96),  QSize(96, 128), QSize(128, 128), QSize(128, 192),
};

// Windows code pages of the ISO 8859 parts used by the VT font types (Annex L). 0 = Latin 1.
unsigned codePageFor(int fontType)
{
    switch (fontType) {
    case 1: return 28605; // ISO 8859-15
    case 2: return 28592; // ISO 8859-2
    case 4: return 28594; // ISO 8859-4
    case 5: return 28595; // ISO 8859-5
    case 7: return 28597; // ISO 8859-7
    default: return 0;
    }
}

QString decodeEightBit(const std::vector<std::uint8_t>& bytes, int fontType)
{
    const unsigned codePage = codePageFor(fontType);
#ifdef _WIN32
    if (codePage != 0 && !bytes.empty()) {
        const auto* input = reinterpret_cast<const char*>(bytes.data());
        const int length = static_cast<int>(bytes.size());
        const int needed = MultiByteToWideChar(codePage, 0, input, length, nullptr, 0);
        if (needed > 0) {
            std::wstring wide(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(codePage, 0, input, length, wide.data(), needed);
            return QString::fromStdWString(wide);
        }
    }
#else
    (void)codePage;
#endif
    return QString::fromLatin1(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

bool encodeEightBit(const QString& text, int fontType, std::vector<std::uint8_t>& out)
{
    const unsigned codePage = codePageFor(fontType);
    if (codePage == 0) {
        std::vector<std::uint8_t> bytes;
        for (QChar c : text) {
            if (c.unicode() > 0xFF) {
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>(c.unicode()));
        }
        out = std::move(bytes);
        return true;
    }
#ifdef _WIN32
    const std::wstring wide = text.toStdWString();
    if (wide.empty()) {
        out.clear();
        return true;
    }
    BOOL usedDefault = FALSE;
    const int length = static_cast<int>(wide.size());
    const int needed =
        WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), length, nullptr, 0, nullptr, &usedDefault);
    if (needed <= 0 || usedDefault) {
        return false;
    }
    std::string bytes(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), length, bytes.data(), needed, nullptr,
                        &usedDefault);
    out.assign(bytes.begin(), bytes.end());
    return !usedDefault;
#else
    return false;
#endif
}

struct FontCache {
    QFont font;
    qreal advance = 1;
    qreal ascent = 1;
    qreal height = 1;
};

const FontCache& cachedFont(int style)
{
    static std::array<FontCache, 4> cache;
    static std::array<bool, 4> ready{};
    const int key = ((style & FontBold) != 0 ? 1 : 0) | ((style & FontItalic) != 0 ? 2 : 0);
    if (!ready[static_cast<std::size_t>(key)]) {
        FontCache& entry = cache[static_cast<std::size_t>(key)];
        entry.font = QFont(QStringLiteral("Consolas"));
        entry.font.setStyleHint(QFont::Monospace);
        entry.font.setFixedPitch(true);
        entry.font.setStyleStrategy(QFont::NoAntialias);
        entry.font.setPixelSize(100);
        entry.font.setBold((key & 1) != 0);
        entry.font.setItalic((key & 2) != 0);
        const QFontMetricsF metrics(entry.font);
        entry.advance = metrics.horizontalAdvance(QLatin1Char('M'));
        entry.ascent = metrics.ascent();
        entry.height = metrics.ascent() + metrics.descent();
        ready[static_cast<std::size_t>(key)] = true;
    }
    return cache[static_cast<std::size_t>(key)];
}

QString trimLeft(const QString& s)
{
    qsizetype i = 0;
    while (i < s.size() && s[i] == QLatin1Char(' ')) {
        ++i;
    }
    return s.mid(i);
}

QString trimRight(const QString& s)
{
    qsizetype n = s.size();
    while (n > 0 && s[n - 1] == QLatin1Char(' ')) {
        --n;
    }
    return s.left(n);
}

// Greedy word wrap of one paragraph into lines of at most `columns` characters.
QStringList wrapParagraph(const QString& paragraph, int columns, bool wrapOnHyphen)
{
    QStringList lines;
    QString rest = paragraph;
    bool first = true;
    while (true) {
        if (!first) {
            rest = trimLeft(rest); // leading spaces of continuation lines are removed
        }
        if (rest.size() <= columns) {
            lines << rest;
            break;
        }
        qsizetype breakAt = -1;
        for (qsizetype i = std::min<qsizetype>(columns, rest.size() - 1); i > 0; --i) {
            if (rest[i] == QLatin1Char(' ')) {
                breakAt = i;
                break;
            }
            if (wrapOnHyphen && rest[i - 1] == QLatin1Char('-') && i <= columns) {
                breakAt = i;
                break;
            }
        }
        if (breakAt <= 0) {
            breakAt = columns; // word longer than the field: break inside the word
        }
        lines << rest.left(breakAt);
        rest = rest.mid(breakAt);
        first = false;
        if (rest.isEmpty()) {
            break;
        }
    }
    return lines;
}

} // namespace

QSize fontCellSize(int fontSizeIndex)
{
    if (fontSizeIndex < 0 || fontSizeIndex >= kFontSizeCount) {
        return kCells[0];
    }
    return kCells[static_cast<std::size_t>(fontSizeIndex)];
}

bool isWideString(const std::vector<std::uint8_t>& bytes)
{
    return bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes[0] == 0xFF && bytes[1] == 0xFE;
}

QString decodeVtString(const std::vector<std::uint8_t>& bytes, int fontType)
{
    if (isWideString(bytes)) {
        QString text;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            text.append(QChar(static_cast<char16_t>(bytes[i] | (bytes[i + 1] << 8))));
        }
        return text;
    }
    return decodeEightBit(bytes, fontType);
}

bool encodeVtString(const QString& text, int fontType, bool allowWide, std::vector<std::uint8_t>& out)
{
    if (encodeEightBit(text, fontType, out)) {
        return true;
    }
    if (!allowWide) {
        return false;
    }
    std::vector<std::uint8_t> wide = {0xFF, 0xFE};
    for (QChar c : text) {
        wide.push_back(static_cast<std::uint8_t>(c.unicode() & 0xFF));
        wide.push_back(static_cast<std::uint8_t>(c.unicode() >> 8));
    }
    out = std::move(wide);
    return true;
}

QString formatVtNumber(double raw, double offset, double scale, int decimals, bool exponential, bool truncate)
{
    double value = (raw + offset) * scale;
    decimals = std::clamp(decimals, 0, 7);
    if (truncate && !exponential) {
        const double factor = std::pow(10.0, decimals);
        value = std::trunc(value * factor) / factor;
    }
    if (exponential) {
        return QString::number(value, 'E', decimals);
    }
    QString text = QString::number(value, 'f', decimals);
    if (text.startsWith(QLatin1Char('-')) && text.mid(1).toDouble() == 0.0) {
        text.remove(0, 1); // no "-0.0"
    }
    return text;
}

QStringList layoutVtText(const QString& text, int columns, bool autoWrap, bool wrapOnHyphen, int horizontal)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QStringLiteral("\n\r"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList lines;
    for (const QString& paragraph : normalized.split(QLatin1Char('\n'))) {
        if (autoWrap && columns > 0) {
            lines << wrapParagraph(paragraph, columns, wrapOnHyphen);
        } else {
            lines << paragraph;
        }
    }

    for (QString& line : lines) {
        if (horizontal == 1) {
            line = trimRight(trimLeft(line));
        } else if (horizontal == 2) {
            line = trimRight(line);
        }
    }
    return lines;
}

void drawCellText(QPainter& painter, const QString& text, QPoint topLeft, QSize cell, int style, const QColor& colour)
{
    if (text.isEmpty() || cell.isEmpty()) {
        return;
    }
    const FontCache& f = cachedFont(style);
    QFont font = f.font;
    font.setUnderline((style & FontUnderlined) != 0);
    font.setStrikeOut((style & FontCrossedOut) != 0);

    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    painter.translate(topLeft);
    painter.scale(cell.width() / f.advance, cell.height() / f.height);
    painter.setFont(font);
    painter.setPen(colour);
    for (qsizetype i = 0; i < text.size(); ++i) {
        painter.drawText(QPointF(static_cast<qreal>(i) * f.advance, f.ascent), QString(text[i]));
    }
    painter.restore();
}

} // namespace vtr
