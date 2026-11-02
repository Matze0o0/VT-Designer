#include "vtrender/VtRenderer.h"

#include "vtrender/VtText.h"

#include "vtcore/Palette.h"
#include "vtcore/PictureCodec.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vtr {

using vt::ObjectId;
using vt::ObjectType;
using vt::VtObject;

namespace {

constexpr int kMaxDepth = 32;
constexpr int kSoftKeyGap = 6;     // between Data Mask and Soft Key column
constexpr int kSoftKeySpacing = 2; // between designators
const QColor kViewBackground(64, 64, 64);

struct ArcGeometry {
    double start;
    double sweep; // degrees, positive = anticlockwise (Qt and ISO 11783-6 use the same convention)
};

// Start/end angles are stored halved (0-180 => 0-360 degrees). Equal angles mean a closed arc.
ArcGeometry arcGeometry(double startAttr, double endAttr, bool clockwise)
{
    const double start = startAttr * 2;
    const double end = endAttr * 2;
    double sweep = 0;
    if (start == end) {
        sweep = 360;
    } else if (clockwise) {
        sweep = std::fmod(start - end + 360, 360);
    } else {
        sweep = std::fmod(end - start + 360, 360);
    }
    return {start, clockwise ? -sweep : sweep};
}

QPointF pointOnEllipse(const QRectF& r, double degrees)
{
    const double rad = degrees * std::numbers::pi / 180.0;
    return {r.center().x() + r.width() / 2 * std::cos(rad), r.center().y() - r.height() / 2 * std::sin(rad)};
}

double fraction(double value, double min, double max)
{
    if (max <= min) {
        return 0;
    }
    return std::clamp((value - min) / (max - min), 0.0, 1.0);
}

// Converts the 16-bit line art (MSB first, 1 = drawn) into a Qt dash pattern in pen-width units.
bool applyLineArt(QPen& pen, int art)
{
    if (art == 0) {
        return false;
    }
    if (art == 0xFFFF) {
        return true;
    }
    QList<qreal> runs;
    bool on = true;
    int start = 0;
    while (((art >> (15 - start)) & 1) == 0) {
        ++start; // the pattern must begin with a drawn run; rotate it and compensate with the offset
    }
    int length = 0;
    for (int i = 0; i < 16; ++i) {
        const bool bit = ((art >> (15 - ((start + i) % 16))) & 1) != 0;
        if (bit == on) {
            ++length;
        } else {
            runs << length;
            on = bit;
            length = 1;
        }
    }
    runs << length;
    if (runs.size() % 2 != 0) {
        runs << 0;
    }
    pen.setDashPattern(runs);
    pen.setDashOffset(16 - start);
    return true;
}

} // namespace

QColor vtColour(int index)
{
    return QColor::fromRgba(vt::standardColour(static_cast<std::uint8_t>(index & 0xFF)));
}

Renderer::Renderer(const vt::ObjectPool& pool, const RenderSettings& settings)
    : m_pool(pool)
    , m_settings(settings)
{
}

bool Renderer::isViewRoot(ObjectType type)
{
    switch (type) {
    case ObjectType::WorkingSet:
    case ObjectType::DataMask:
    case ObjectType::AlarmMask:
    case ObjectType::Container:
    case ObjectType::SoftKeyMask:
    case ObjectType::Key:
    case ObjectType::Button:
    case ObjectType::WindowMask:
    case ObjectType::Animation:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1:
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2: return true;
    default: return false;
    }
}

const VtObject* Renderer::find(ObjectId id) const
{
    return id == vt::kNullObjectId ? nullptr : m_pool.find(id);
}

double Renderer::attr(const VtObject& object, const char* name, double fallback) const
{
    const int index = object.schema().attributeIndex(name);
    if (index < 0 || static_cast<std::size_t>(index) >= object.attributes.size()) {
        return fallback;
    }
    return object.attributes[static_cast<std::size_t>(index)];
}

double Renderer::numericValue(const VtObject& object, const char* valueAttribute) const
{
    const auto varId = static_cast<ObjectId>(attr(object, "variableReference", vt::kNullObjectId));
    if (const VtObject* var = find(varId); var != nullptr && var->type == ObjectType::NumberVariable) {
        return attr(*var, "value");
    }
    return attr(object, valueAttribute);
}

QSize Renderer::designatorSize() const
{
    return {m_settings.softKeyWidth, m_settings.softKeyHeight};
}

QPoint Renderer::softKeyColumnOrigin() const
{
    return {m_settings.dataMaskSize + kSoftKeyGap, 0};
}

QSize Renderer::softKeyMaskSize(const VtObject& mask) const
{
    const int perColumn = std::max(1, m_settings.physicalSoftKeys);
    std::size_t used = mask.references.size();
    while (used > 0 && mask.references[used - 1] == vt::kNullObjectId) {
        --used; // trailing NULL entries do not reserve a position (4.5.3.5)
    }
    const int slotCount = std::max(static_cast<int>(used), perColumn);
    const int columns = (slotCount + perColumn - 1) / perColumn;
    const QSize key = designatorSize();
    return {columns * (key.width() + kSoftKeySpacing) - kSoftKeySpacing,
            perColumn * (key.height() + kSoftKeySpacing) - kSoftKeySpacing};
}

QRect Renderer::softKeySlot(int index, QPoint origin) const
{
    const int perColumn = std::max(1, m_settings.physicalSoftKeys);
    const QSize key = designatorSize();
    const int column = index / perColumn;
    const int row = index % perColumn;
    return {origin + QPoint(column * (key.width() + kSoftKeySpacing), row * (key.height() + kSoftKeySpacing)), key};
}

QSize Renderer::objectSize(ObjectId id) const
{
    return sizeOf(id, 0);
}

QSize Renderer::sizeOf(ObjectId id, int depth) const
{
    const VtObject* object = find(id);
    if (object == nullptr || depth > kMaxDepth) {
        return {};
    }
    const int width = static_cast<int>(attr(*object, "width"));
    const int maskSize = m_settings.dataMaskSize;
    switch (object->type) {
    case ObjectType::WorkingSet:
    case ObjectType::Key:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1:
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2:
    case ObjectType::AuxiliaryControlDesignatorType2: return designatorSize();
    case ObjectType::DataMask:
    case ObjectType::AlarmMask: return {maskSize, maskSize};
    case ObjectType::SoftKeyMask: return softKeyMaskSize(*object);
    case ObjectType::InputBoolean:
    case ObjectType::OutputMeter: return {width, width};
    case ObjectType::PictureGraphic: {
        const double actualWidth = attr(*object, "actualWidth");
        const double actualHeight = attr(*object, "actualHeight");
        const int height = actualWidth > 0 ? static_cast<int>(std::lround(actualHeight * width / actualWidth)) : 0;
        return {width, height};
    }
    case ObjectType::ObjectPointer:
        return sizeOf(static_cast<ObjectId>(attr(*object, "value", vt::kNullObjectId)), depth + 1);
    case ObjectType::ExternalObjectPointer:
        return sizeOf(static_cast<ObjectId>(attr(*object, "defaultObjectId", vt::kNullObjectId)), depth + 1);
    case ObjectType::WindowMask:
        return {static_cast<int>(attr(*object, "width", 1)) * maskSize / 2,
                static_cast<int>(attr(*object, "height", 1)) * maskSize / 6};
    case ObjectType::GraphicsContext:
        return {static_cast<int>(attr(*object, "viewportWidth")), static_cast<int>(attr(*object, "viewportHeight"))};
    default:
        if (object->hasAttribute("width") && object->hasAttribute("height")) {
            return {width, static_cast<int>(attr(*object, "height"))};
        }
        return {};
    }
}

QSize Renderer::viewSize(ObjectId root) const
{
    const VtObject* object = find(root);
    if (object == nullptr) {
        return {};
    }
    if (object->type == ObjectType::DataMask || object->type == ObjectType::AlarmMask) {
        QSize size(m_settings.dataMaskSize, m_settings.dataMaskSize);
        if (const VtObject* keys = find(static_cast<ObjectId>(attr(*object, "softKeyMask", vt::kNullObjectId)));
            keys != nullptr && keys->type == ObjectType::SoftKeyMask) {
            const QSize column = softKeyMaskSize(*keys);
            size = QSize(size.width() + kSoftKeyGap + column.width(), std::max(size.height(), column.height()));
        }
        return size;
    }
    return objectSize(root);
}

void Renderer::renderView(QPainter& painter, ObjectId root) const
{
    const VtObject* object = find(root);
    if (object == nullptr) {
        return;
    }
    if (object->type == ObjectType::SoftKeyMask) {
        drawSoftKeyMask(painter, *object, {0, 0});
        return;
    }
    draw(painter, *object, {0, 0}, 0);
    if (object->type == ObjectType::DataMask || object->type == ObjectType::AlarmMask) {
        if (const VtObject* keys = find(static_cast<ObjectId>(attr(*object, "softKeyMask", vt::kNullObjectId)));
            keys != nullptr && keys->type == ObjectType::SoftKeyMask) {
            drawSoftKeyMask(painter, *keys, softKeyColumnOrigin());
        }
    }
}

QImage Renderer::renderViewImage(ObjectId root) const
{
    const QSize size = viewSize(root).expandedTo(QSize(1, 1));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(kViewBackground);
    QPainter painter(&image);
    renderView(painter, root);
    return image;
}

void Renderer::drawObject(QPainter& painter, ObjectId id, QPoint pos) const
{
    if (const VtObject* object = find(id)) {
        draw(painter, *object, pos, 0);
    }
}

void Renderer::drawChildren(QPainter& painter, const VtObject& object, QPoint pos, int depth) const
{
    for (const vt::ChildRef& child : object.children) {
        if (const VtObject* c = find(child.id)) {
            draw(painter, *c, pos + QPoint(child.x, child.y), depth + 1);
        }
    }
}

void Renderer::drawSoftKeyMask(QPainter& painter, const VtObject& mask, QPoint origin) const
{
    const QSize size = softKeyMaskSize(mask);
    const int perColumn = std::max(1, m_settings.physicalSoftKeys);
    const int slotCount = (size.width() + kSoftKeySpacing) / (designatorSize().width() + kSoftKeySpacing) * perColumn;
    const QColor background = vtColour(static_cast<int>(attr(mask, "backgroundColour", 1)));
    for (int i = 0; i < slotCount; ++i) {
        const QRect slot = softKeySlot(i, origin);
        painter.fillRect(slot, background);
        if (static_cast<std::size_t>(i) < mask.references.size()) {
            if (const VtObject* key = find(mask.references[static_cast<std::size_t>(i)])) {
                painter.save();
                painter.setClipRect(slot, Qt::IntersectClip);
                draw(painter, *key, slot.topLeft(), 1);
                painter.restore();
            }
        }
    }
}

void Renderer::draw(QPainter& painter, const VtObject& object, QPoint pos, int depth) const
{
    if (depth > kMaxDepth) {
        return;
    }
    const QRect rect(pos, objectSize(object.id));
    const auto colourAttr = [&](const char* name, double fallback = 0) {
        return vtColour(static_cast<int>(attr(object, name, fallback)));
    };

    painter.save();
    switch (object.type) {
    case ObjectType::WorkingSet:
    case ObjectType::DataMask:
    case ObjectType::AlarmMask:
    case ObjectType::Key:
    case ObjectType::WindowMask:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1:
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2:
        painter.fillRect(rect, colourAttr("backgroundColour", 1));
        painter.setClipRect(rect, Qt::IntersectClip);
        drawChildren(painter, object, pos, depth);
        break;

    case ObjectType::Container:
        if (attr(object, "hidden") != 0) {
            if (!m_settings.designMode) {
                break;
            }
            painter.setOpacity(painter.opacity() * 0.35);
        }
        painter.setClipRect(rect, Qt::IntersectClip);
        drawChildren(painter, object, pos, depth);
        break;

    case ObjectType::Button: {
        const int options = static_cast<int>(attr(object, "options"));
        if ((options & 0x08) == 0) {
            painter.fillRect(rect, colourAttr("backgroundColour", 7));
        }
        painter.save();
        painter.setClipRect(rect, Qt::IntersectClip);
        drawChildren(painter, object, pos, depth);
        painter.restore();
        if ((options & 0x24) == 0) {
            const QColor border = colourAttr("borderColour");
            const int b = std::min({2, rect.width() / 2, rect.height() / 2});
            painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), b), border);
            painter.fillRect(QRect(rect.left(), rect.bottom() - b + 1, rect.width(), b), border);
            painter.fillRect(QRect(rect.left(), rect.top(), b, rect.height()), border);
            painter.fillRect(QRect(rect.right() - b + 1, rect.top(), b, rect.height()), border);
        }
        if ((options & 0x10) != 0) {
            painter.fillRect(rect, QColor(128, 128, 128, 110));
        }
        break;
    }

    case ObjectType::InputBoolean: {
        painter.fillRect(rect, colourAttr("backgroundColour", 1));
        if (numericValue(object) != 0) {
            QColor tick(Qt::black);
            if (const VtObject* font = find(static_cast<ObjectId>(attr(object, "foregroundColour", vt::kNullObjectId)))) {
                tick = vtColour(static_cast<int>(attr(*font, "fontColour")));
            }
            QPen pen(tick, std::max(1, rect.width() / 8), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
            painter.setPen(pen);
            const QRectF r = QRectF(rect).adjusted(rect.width() * 0.2, rect.height() * 0.2, -rect.width() * 0.2,
                                                   -rect.height() * 0.2);
            painter.drawPolyline(QPolygonF({QPointF(r.left(), r.center().y()), QPointF(r.center().x(), r.bottom()),
                                            QPointF(r.right(), r.top())}));
        }
        if (attr(object, "enabled", 1) == 0) {
            painter.fillRect(rect, QColor(128, 128, 128, 110));
        }
        break;
    }

    case ObjectType::InputString:
    case ObjectType::OutputString: {
        int fontType = 0;
        if (const VtObject* font = find(static_cast<ObjectId>(attr(object, "fontAttributes", vt::kNullObjectId)))) {
            fontType = static_cast<int>(attr(*font, "fontType"));
        }
        const VtObject* variable = find(static_cast<ObjectId>(attr(object, "variableReference", vt::kNullObjectId)));
        const std::vector<std::uint8_t>& bytes =
            (variable != nullptr && variable->type == ObjectType::StringVariable) ? variable->data : object.data;
        drawTextField(painter, object, rect, decodeVtString(bytes, fontType));
        break;
    }

    case ObjectType::InputNumber:
    case ObjectType::OutputNumber:
        drawNumberField(painter, object, rect);
        break;

    case ObjectType::InputList:
    case ObjectType::OutputList: {
        const auto index = static_cast<std::size_t>(numericValue(object));
        if (index < object.references.size()) {
            if (const VtObject* item = find(object.references[index])) {
                painter.setClipRect(rect, Qt::IntersectClip);
                draw(painter, *item, pos, depth + 1);
            }
        }
        break;
    }

    case ObjectType::OutputLine:
    case ObjectType::OutputRectangle:
    case ObjectType::OutputEllipse:
    case ObjectType::OutputPolygon:
        drawShape(painter, object, rect);
        break;

    case ObjectType::OutputMeter: drawMeter(painter, object, rect); break;
    case ObjectType::OutputLinearBarGraph: drawLinearBar(painter, object, rect); break;
    case ObjectType::OutputArchedBarGraph: drawArchedBar(painter, object, rect); break;
    case ObjectType::PictureGraphic: drawPicture(painter, object, rect); break;

    case ObjectType::ObjectPointer:
    case ObjectType::ExternalObjectPointer: {
        const char* target = object.type == ObjectType::ObjectPointer ? "value" : "defaultObjectId";
        const auto id = static_cast<ObjectId>(attr(object, target, vt::kNullObjectId));
        if (const VtObject* pointed = find(id); pointed != nullptr && id != object.id) {
            draw(painter, *pointed, pos, depth + 1);
        }
        break;
    }

    case ObjectType::Animation: {
        const auto index = static_cast<std::size_t>(attr(object, "value"));
        if (index < object.children.size()) {
            const vt::ChildRef& child = object.children[index];
            if (const VtObject* c = find(child.id)) {
                painter.setClipRect(rect, Qt::IntersectClip);
                draw(painter, *c, pos + QPoint(child.x, child.y), depth + 1);
            }
        }
        break;
    }

    case ObjectType::GraphicsContext:
        painter.fillRect(rect, colourAttr("backgroundColour", 1));
        break;

    case ObjectType::AuxiliaryControlDesignatorType2:
        painter.fillRect(rect, QColor(220, 220, 220));
        painter.setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
        painter.drawRect(rect.adjusted(0, 0, -1, -1));
        break;

    default:
        break;
    }
    painter.restore();
}

void Renderer::drawTextField(QPainter& painter, const VtObject& object, QRect rect, const QString& text) const
{
    QColor colour(Qt::black);
    QSize cell = fontCellSize(0);
    int style = 0;
    if (const VtObject* font = find(static_cast<ObjectId>(attr(object, "fontAttributes", vt::kNullObjectId)))) {
        colour = vtColour(static_cast<int>(attr(*font, "fontColour")));
        cell = fontCellSize(static_cast<int>(attr(*font, "fontSize")));
        style = static_cast<int>(attr(*font, "fontStyle"));
    }

    const bool isString = object.type == ObjectType::InputString || object.type == ObjectType::OutputString;
    const int options = static_cast<int>(attr(object, "options"));
    const bool transparent = (options & 0x01) != 0;
    const QColor background = vtColour(static_cast<int>(attr(object, "backgroundColour", 1)));
    if (!transparent) {
        painter.fillRect(rect, background);
    }

    const int justification = static_cast<int>(attr(object, "justification"));
    const int horizontal = justification & 0x03;
    const int vertical = (justification >> 2) & 0x03;
    const int columns = cell.width() > 0 ? rect.width() / cell.width() : 0;
    const QStringList lines = layoutVtText(text, columns, isString && (options & 0x02) != 0,
                                           isString && (options & 0x04) != 0, horizontal);

    const int total = static_cast<int>(lines.size()) * cell.height();
    int y = rect.top();
    if (vertical == 1) {
        y += (rect.height() - total) / 2;
    } else if (vertical == 2) {
        y += rect.height() - total;
    }

    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    for (const QString& line : lines) {
        const int width = static_cast<int>(line.size()) * cell.width();
        int x = rect.left();
        if (horizontal == 1) {
            x += (rect.width() - width) / 2;
        } else if (horizontal == 2) {
            x += rect.width() - width;
        }
        QColor textColour = colour;
        if ((style & FontInverted) != 0) {
            painter.fillRect(QRect(x, y, width, cell.height()), colour);
            textColour = transparent ? vtColour(1) : background;
        }
        drawCellText(painter, line, {x, y}, cell, style, textColour);
        y += cell.height();
    }
    painter.restore();

    const bool disabled = object.type == ObjectType::InputString ? attr(object, "enabled", 1) == 0
                          : object.type == ObjectType::InputNumber
                              ? (static_cast<int>(attr(object, "options2", 1)) & 0x01) == 0
                              : false;
    if (disabled) {
        painter.fillRect(rect, QColor(128, 128, 128, 110));
    }
}

void Renderer::drawNumberField(QPainter& painter, const VtObject& object, QRect rect) const
{
    const int options = static_cast<int>(attr(object, "options"));
    const double raw = numericValue(object);
    const double offset = attr(object, "offset");
    const double scale = attr(object, "scale", 1);
    QString text = formatVtNumber(raw, offset, scale, static_cast<int>(attr(object, "numberOfDecimals")),
                                  attr(object, "format") != 0, (options & 0x08) != 0);

    if ((options & 0x04) != 0 && (raw + offset) * scale == 0) {
        text.clear(); // display zero as blank
    } else if ((options & 0x02) != 0) {
        int columns = 0;
        if (const VtObject* font = find(static_cast<ObjectId>(attr(object, "fontAttributes", vt::kNullObjectId)))) {
            const QSize cell = fontCellSize(static_cast<int>(attr(*font, "fontSize")));
            columns = cell.width() > 0 ? rect.width() / cell.width() : 0;
        }
        const bool negative = text.startsWith(QLatin1Char('-'));
        QString digits = negative ? text.mid(1) : text;
        while (digits.size() + (negative ? 1 : 0) < columns) {
            digits.prepend(QLatin1Char('0'));
        }
        text = negative ? QLatin1Char('-') + digits : digits;
    }
    drawTextField(painter, object, rect, text);
}

void Renderer::drawShape(QPainter& painter, const VtObject& object, QRect rect) const
{
    // Line attributes
    QColor lineColour(Qt::black);
    int lineWidth = 0;
    int lineArt = 0xFFFF;
    if (const VtObject* line = find(static_cast<ObjectId>(attr(object, "lineAttributes", vt::kNullObjectId)))) {
        lineColour = vtColour(static_cast<int>(attr(*line, "lineColour")));
        lineWidth = static_cast<int>(attr(*line, "lineWidth"));
        lineArt = static_cast<int>(attr(*line, "lineArt"));
    }
    QPen pen(lineColour, std::max(1, lineWidth), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    const bool drawLine = lineWidth > 0 && applyLineArt(pen, lineArt);

    // Fill attributes
    QBrush brush(Qt::NoBrush);
    if (const VtObject* fill = find(static_cast<ObjectId>(attr(object, "fillAttributes", vt::kNullObjectId)))) {
        switch (static_cast<int>(attr(*fill, "fillType"))) {
        case 1: brush = QBrush(lineColour); break;
        case 2: brush = QBrush(vtColour(static_cast<int>(attr(*fill, "fillColour")))); break;
        case 3:
            if (const VtObject* pattern = find(static_cast<ObjectId>(attr(*fill, "fillPattern", vt::kNullObjectId)))) {
                const QImage image = pictureImage(*pattern);
                if (!image.isNull()) {
                    brush = QBrush(image);
                    brush.setTransform(QTransform::fromTranslate(rect.left(), rect.top()));
                }
            }
            break;
        default: break;
        }
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setClipRect(rect.adjusted(0, 0, 1, 1), Qt::IntersectClip);
    const double half = lineWidth / 2.0;
    const QRectF inner = QRectF(rect).adjusted(half, half, -half, -half);

    switch (object.type) {
    case ObjectType::OutputLine:
        if (drawLine) {
            painter.setPen(pen);
            const bool rising = attr(object, "lineDirection") != 0;
            const QPointF a(rect.left() + half, rising ? rect.bottom() - half + 1 : rect.top() + half);
            const QPointF b(rect.right() - half + 1, rising ? rect.top() + half : rect.bottom() - half + 1);
            painter.drawLine(a, b);
        }
        break;

    case ObjectType::OutputRectangle: {
        painter.fillRect(rect, brush);
        if (!drawLine) {
            break;
        }
        const int suppress = static_cast<int>(attr(object, "lineSuppression"));
        if (lineArt == 0xFFFF) {
            // Solid borders as bands inside the rectangle are pixel exact.
            const int w = std::min({lineWidth, rect.width(), rect.height()});
            if ((suppress & 0x01) == 0) painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), w), lineColour);
            if ((suppress & 0x02) == 0) painter.fillRect(QRect(rect.right() - w + 1, rect.top(), w, rect.height()), lineColour);
            if ((suppress & 0x04) == 0) painter.fillRect(QRect(rect.left(), rect.bottom() - w + 1, rect.width(), w), lineColour);
            if ((suppress & 0x08) == 0) painter.fillRect(QRect(rect.left(), rect.top(), w, rect.height()), lineColour);
        } else {
            painter.setPen(pen);
            if ((suppress & 0x01) == 0) painter.drawLine(inner.topLeft(), inner.topRight());
            if ((suppress & 0x02) == 0) painter.drawLine(inner.topRight(), inner.bottomRight());
            if ((suppress & 0x04) == 0) painter.drawLine(inner.bottomLeft(), inner.bottomRight());
            if ((suppress & 0x08) == 0) painter.drawLine(inner.topLeft(), inner.bottomLeft());
        }
        break;
    }

    case ObjectType::OutputEllipse: {
        const int type = static_cast<int>(attr(object, "ellipseType"));
        const ArcGeometry arc = arcGeometry(attr(object, "startAngle"), attr(object, "endAngle"), false);
        const bool full = type == 0 || std::abs(arc.sweep) >= 360;
        painter.setPen(drawLine ? pen : QPen(Qt::NoPen));
        painter.setBrush(type == 1 && !full ? QBrush(Qt::NoBrush) : brush);
        const int start = static_cast<int>(arc.start * 16);
        const int span = static_cast<int>(arc.sweep * 16);
        if (full) {
            painter.drawEllipse(inner);
        } else if (type == 1) {
            painter.drawArc(inner, start, span);
        } else if (type == 2) {
            painter.drawChord(inner, start, span);
        } else {
            painter.drawPie(inner, start, span);
        }
        break;
    }

    case ObjectType::OutputPolygon: {
        QPolygonF polygon;
        for (const vt::Point& p : object.points) {
            polygon << QPointF(rect.left() + p.x + 0.5, rect.top() + p.y + 0.5);
        }
        painter.setPen(drawLine ? pen : QPen(Qt::NoPen));
        if (static_cast<int>(attr(object, "polygonType")) == 3) {
            painter.drawPolyline(polygon);
        } else {
            painter.setBrush(brush);
            painter.drawPolygon(polygon, Qt::WindingFill);
        }
        break;
    }

    default:
        break;
    }
    painter.restore();
}

void Renderer::drawMeter(QPainter& painter, const VtObject& object, QRect rect) const
{
    const int options = static_cast<int>(attr(object, "options"));
    const ArcGeometry arc = arcGeometry(attr(object, "startAngle"), attr(object, "endAngle"), (options & 0x08) != 0);
    const QRectF outer = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    const QRectF arcRect = outer.adjusted(outer.width() * 0.08, outer.height() * 0.08, -outer.width() * 0.08,
                                          -outer.height() * 0.08);
    const QColor arcColour = vtColour(static_cast<int>(attr(object, "arcAndTickColour")));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if ((options & 0x02) != 0) {
        painter.setPen(QPen(vtColour(static_cast<int>(attr(object, "borderColour"))), 1));
        painter.drawEllipse(outer);
    }
    if ((options & 0x01) != 0) {
        painter.setPen(QPen(arcColour, 1));
        painter.drawArc(arcRect, static_cast<int>(arc.start * 16), static_cast<int>(arc.sweep * 16));
    }
    if ((options & 0x04) != 0) {
        const int ticks = static_cast<int>(attr(object, "numberOfTicks"));
        painter.setPen(QPen(arcColour, 1));
        const QRectF tickInner = arcRect.adjusted(arcRect.width() * 0.08, arcRect.height() * 0.08,
                                                  -arcRect.width() * 0.08, -arcRect.height() * 0.08);
        for (int i = 0; i < ticks; ++i) {
            const double f = ticks == 1 ? 0.5 : static_cast<double>(i) / (ticks - 1);
            const double angle = arc.start + f * arc.sweep;
            painter.drawLine(pointOnEllipse(tickInner, angle), pointOnEllipse(arcRect, angle));
        }
    }
    const double f = fraction(numericValue(object), attr(object, "minValue"), attr(object, "maxValue"));
    const double angle = arc.start + f * arc.sweep;
    painter.setPen(QPen(vtColour(static_cast<int>(attr(object, "needleColour"))), std::max(1.0, rect.width() / 40.0)));
    painter.drawLine(outer.center(), pointOnEllipse(arcRect.adjusted(2, 2, -2, -2), angle));
    painter.restore();
}

void Renderer::drawLinearBar(QPainter& painter, const VtObject& object, QRect rect) const
{
    const int options = static_cast<int>(attr(object, "options"));
    const bool horizontal = (options & 0x10) != 0;
    const bool positive = (options & 0x20) != 0;
    const QColor colour = vtColour(static_cast<int>(attr(object, "colour")));
    const double min = attr(object, "minValue");
    const double max = attr(object, "maxValue");
    const double f = fraction(numericValue(object), min, max);

    // Position along the axis for a fraction, measured from the start edge of the growth direction.
    auto barRect = [&](double fr) {
        const int length = static_cast<int>(std::lround((horizontal ? rect.width() : rect.height()) * fr));
        if (horizontal) {
            return positive ? QRect(rect.left(), rect.top(), length, rect.height())
                            : QRect(rect.right() - length + 1, rect.top(), length, rect.height());
        }
        return positive ? QRect(rect.left(), rect.bottom() - length + 1, rect.width(), length)
                        : QRect(rect.left(), rect.top(), rect.width(), length);
    };
    auto lineAt = [&](double fr, const QColor& c) {
        const QRect r = barRect(fr);
        if (horizontal) {
            const int x = positive ? r.right() : r.left();
            painter.fillRect(QRect(std::clamp(x, rect.left(), rect.right()), rect.top(), 1, rect.height()), c);
        } else {
            const int y = positive ? r.top() : r.bottom();
            painter.fillRect(QRect(rect.left(), std::clamp(y, rect.top(), rect.bottom()), rect.width(), 1), c);
        }
    };

    if ((options & 0x08) != 0) {
        lineAt(f, colour);
    } else {
        painter.fillRect(barRect(f), colour);
    }
    if ((options & 0x04) != 0) {
        const int ticks = static_cast<int>(attr(object, "numberOfTicks"));
        const int tickLength = std::max(2, (horizontal ? rect.height() : rect.width()) / 4);
        for (int i = 0; i < ticks; ++i) {
            const double fr = ticks == 1 ? 0.5 : static_cast<double>(i) / (ticks - 1);
            const QRect r = barRect(fr);
            if (horizontal) {
                const int x = std::clamp(positive ? r.right() : r.left(), rect.left(), rect.right());
                painter.fillRect(QRect(x, rect.top(), 1, tickLength), colour);
            } else {
                const int y = std::clamp(positive ? r.top() : r.bottom(), rect.top(), rect.bottom());
                painter.fillRect(QRect(rect.left(), y, tickLength, 1), colour);
            }
        }
    }
    if ((options & 0x02) != 0) {
        const auto targetVar =
            find(static_cast<ObjectId>(attr(object, "targetValueVariableReference", vt::kNullObjectId)));
        const double target = targetVar != nullptr ? attr(*targetVar, "value") : attr(object, "targetValue");
        lineAt(fraction(target, min, max), vtColour(static_cast<int>(attr(object, "targetLineColour"))));
    }
    if ((options & 0x01) != 0) {
        painter.save();
        painter.setPen(QPen(colour, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect.adjusted(0, 0, -1, -1));
        painter.restore();
    }
}

void Renderer::drawArchedBar(QPainter& painter, const VtObject& object, QRect rect) const
{
    const int options = static_cast<int>(attr(object, "options"));
    const ArcGeometry arc = arcGeometry(attr(object, "startAngle"), attr(object, "endAngle"), (options & 0x10) != 0);
    const QColor colour = vtColour(static_cast<int>(attr(object, "colour")));
    const double barWidth = attr(object, "barGraphWidth");
    const QRectF outer = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    const QRectF inner = outer.adjusted(barWidth, barWidth, -barWidth, -barWidth);
    const double min = attr(object, "minValue");
    const double max = attr(object, "maxValue");

    auto band = [&](double sweep) {
        QPainterPath path;
        path.arcMoveTo(outer, arc.start);
        path.arcTo(outer, arc.start, sweep);
        path.arcTo(inner, arc.start + sweep, -sweep);
        path.closeSubpath();
        return path;
    };
    auto radial = [&](double fr, const QColor& c) {
        const double angle = arc.start + fr * arc.sweep;
        painter.setPen(QPen(c, 1));
        painter.drawLine(pointOnEllipse(inner, angle), pointOnEllipse(outer, angle));
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double f = fraction(numericValue(object), min, max);
    if ((options & 0x08) != 0) {
        radial(f, colour);
    } else {
        painter.fillPath(band(f * arc.sweep), colour);
    }
    if ((options & 0x02) != 0) {
        const auto targetVar =
            find(static_cast<ObjectId>(attr(object, "targetValueVariableReference", vt::kNullObjectId)));
        const double target = targetVar != nullptr ? attr(*targetVar, "value") : attr(object, "targetValue");
        radial(fraction(target, min, max), vtColour(static_cast<int>(attr(object, "targetLineColour"))));
    }
    if ((options & 0x01) != 0) {
        painter.setPen(QPen(colour, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(band(arc.sweep));
    }
    painter.restore();
}

QImage Renderer::pictureImage(const VtObject& picture) const
{
    std::vector<std::uint8_t> indices;
    try {
        indices = vt::pictureIndices(picture);
    } catch (const std::exception&) {
        return {};
    }
    const int width = static_cast<int>(attr(picture, "actualWidth"));
    const int height = static_cast<int>(attr(picture, "actualHeight"));
    if (width <= 0 || height <= 0) {
        return {};
    }
    const bool transparent = (static_cast<int>(attr(picture, "options")) & vt::kPictureOptionTransparent) != 0;
    const auto transparency = static_cast<std::uint8_t>(attr(picture, "transparencyColour"));
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::uint8_t index = indices[static_cast<std::size_t>(y) * width + x];
            line[x] = (transparent && index == transparency) ? qRgba(0, 0, 0, 0) : vt::standardColour(index);
        }
    }
    return image;
}

void Renderer::drawPicture(QPainter& painter, const VtObject& object, QRect rect) const
{
    const QImage image = pictureImage(object);
    if (image.isNull() || rect.isEmpty()) {
        return;
    }
    painter.drawImage(rect.topLeft(), image.scaled(rect.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

void Renderer::layout(std::vector<Placement>& out, const VtObject& object, ObjectId parent, int index, QPoint pos,
                      int depth, bool movable) const
{
    out.push_back({object.id, parent, index, QRect(pos, objectSize(object.id)), depth, movable});
    if (depth > kMaxDepth) {
        return;
    }
    switch (object.type) {
    case ObjectType::WorkingSet:
    case ObjectType::DataMask:
    case ObjectType::AlarmMask:
    case ObjectType::Container:
    case ObjectType::Key:
    case ObjectType::Button:
    case ObjectType::WindowMask:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1:
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2:
        for (std::size_t i = 0; i < object.children.size(); ++i) {
            const vt::ChildRef& child = object.children[i];
            if (const VtObject* c = find(child.id)) {
                layout(out, *c, object.id, static_cast<int>(i), pos + QPoint(child.x, child.y), depth + 1, true);
            }
        }
        break;
    case ObjectType::Animation: {
        const auto i = static_cast<std::size_t>(attr(object, "value"));
        if (i < object.children.size()) {
            if (const VtObject* c = find(object.children[i].id)) {
                layout(out, *c, object.id, static_cast<int>(i),
                       pos + QPoint(object.children[i].x, object.children[i].y), depth + 1, true);
            }
        }
        break;
    }
    case ObjectType::InputList:
    case ObjectType::OutputList: {
        const auto i = static_cast<std::size_t>(numericValue(object));
        if (i < object.references.size()) {
            if (const VtObject* item = find(object.references[i])) {
                layout(out, *item, object.id, static_cast<int>(i), pos, depth + 1, false);
            }
        }
        break;
    }
    default:
        break;
    }
}

void Renderer::layoutSoftKeyMask(std::vector<Placement>& out, const VtObject& mask, QPoint origin) const
{
    out.push_back({mask.id, vt::kNullObjectId, -1, QRect(origin, softKeyMaskSize(mask)), 0, false});
    for (std::size_t i = 0; i < mask.references.size(); ++i) {
        if (const VtObject* key = find(mask.references[i])) {
            const QRect slot = softKeySlot(static_cast<int>(i), origin);
            const std::size_t first = out.size();
            layout(out, *key, mask.id, static_cast<int>(i), slot.topLeft(), 1, false);
            out[first].rect = slot; // pointers to keys take the designator size as well
        }
    }
}

std::vector<Placement> Renderer::layoutView(ObjectId root) const
{
    std::vector<Placement> out;
    const VtObject* object = find(root);
    if (object == nullptr) {
        return out;
    }
    if (object->type == ObjectType::SoftKeyMask) {
        layoutSoftKeyMask(out, *object, {0, 0});
        return out;
    }
    layout(out, *object, vt::kNullObjectId, -1, {0, 0}, 0, false);
    if (object->type == ObjectType::DataMask || object->type == ObjectType::AlarmMask) {
        if (const VtObject* keys = find(static_cast<ObjectId>(attr(*object, "softKeyMask", vt::kNullObjectId)));
            keys != nullptr && keys->type == ObjectType::SoftKeyMask) {
            layoutSoftKeyMask(out, *keys, softKeyColumnOrigin());
        }
    }
    return out;
}

} // namespace vtr
