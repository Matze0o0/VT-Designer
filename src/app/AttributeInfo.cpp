#include "AttributeInfo.h"

#include "vtrender/VtRenderer.h"
#include "vtrender/VtText.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>

#include <array>
#include <cstring>

namespace ui {

namespace {

using T = vt::ObjectType;

class AttributeInfo {
    Q_DECLARE_TR_FUNCTIONS(AttributeInfo)
};

bool named(const vt::AttributeDef& attribute, const char* name)
{
    return attribute.name == name;
}

bool isColourAttribute(const vt::AttributeDef& attribute)
{
    const std::string_view n = attribute.name;
    return attribute.type == vt::FieldType::UInt8 && (n.find("Colour") != std::string_view::npos || n == "colour");
}

} // namespace

QString typeLabel(vt::ObjectType type)
{
    const vt::ObjectSchema* schema = vt::findSchema(type);
    if (schema == nullptr) {
        return AttributeInfo::tr("Unknown object");
    }
    return QCoreApplication::translate("ObjectType", std::string(schema->label).c_str());
}

QString attributeLabel(const vt::AttributeDef& attribute)
{
    return QCoreApplication::translate("Attribute", std::string(attribute.label).c_str());
}

QString objectDisplayName(const vt::VtObject& object)
{
    if (!object.name.empty()) {
        return QStringLiteral("%1 (%2)").arg(QString::fromStdString(object.name)).arg(object.id);
    }
    return QStringLiteral("%1 %2").arg(typeLabel(object.type)).arg(object.id);
}

EditorKind editorKind(vt::ObjectType type, const vt::AttributeDef& attribute)
{
    if (attribute.type == vt::FieldType::ObjectRef) {
        return EditorKind::ObjectRef;
    }
    if (attribute.type == vt::FieldType::Float32) {
        return EditorKind::Float;
    }
    if (!flagBits(type, attribute).empty()) {
        return EditorKind::Flags;
    }
    if (!enumValues(type, attribute).empty()) {
        return EditorKind::Enum;
    }
    if (isColourAttribute(attribute)) {
        return EditorKind::Colour;
    }
    if (attribute.minValue == 0 && attribute.maxValue == 1) {
        return EditorKind::Boolean;
    }
    return EditorKind::Integer;
}

std::vector<NamedValue> enumValues(vt::ObjectType type, const vt::AttributeDef& a)
{
    if (named(a, "justification")) {
        const std::array<const char*, 3> h = {QT_TRANSLATE_NOOP("AttributeInfo", "Left"),
                                              QT_TRANSLATE_NOOP("AttributeInfo", "Middle"),
                                              QT_TRANSLATE_NOOP("AttributeInfo", "Right")};
        const std::array<const char*, 3> v = {QT_TRANSLATE_NOOP("AttributeInfo", "Top"),
                                              QT_TRANSLATE_NOOP("AttributeInfo", "Middle"),
                                              QT_TRANSLATE_NOOP("AttributeInfo", "Bottom")};
        std::vector<NamedValue> values;
        for (int vi = 0; vi < 3; ++vi) {
            for (int hi = 0; hi < 3; ++hi) {
                values.push_back({hi | (vi << 2), QStringLiteral("%1 / %2").arg(AttributeInfo::tr(h[hi]), AttributeInfo::tr(v[vi]))});
            }
        }
        return values;
    }
    if (named(a, "fontSize")) {
        std::vector<NamedValue> values;
        for (int i = 0; i < vtr::kFontSizeCount; ++i) {
            const QSize cell = vtr::fontCellSize(i);
            values.push_back({i, QStringLiteral("%1 × %2").arg(cell.width()).arg(cell.height())});
        }
        return values;
    }
    if (named(a, "fontType")) {
        return {{0, QStringLiteral("ISO 8859-1 (Latin 1)")}, {1, QStringLiteral("ISO 8859-15 (Latin 9)")},
                {2, QStringLiteral("ISO 8859-2 (Latin 2)")}, {4, QStringLiteral("ISO 8859-4 (Latin 4)")},
                {5, AttributeInfo::tr("ISO 8859-5 (Cyrillic)")},          {7, AttributeInfo::tr("ISO 8859-7 (Greek)")},
                {255, AttributeInfo::tr("Proprietary")}};
    }
    if (named(a, "lineDirection")) {
        return {{0, AttributeInfo::tr("Top left to bottom right")}, {1, AttributeInfo::tr("Bottom left to top right")}};
    }
    if (named(a, "ellipseType")) {
        return {{0, AttributeInfo::tr("Closed ellipse")}, {1, AttributeInfo::tr("Open (arc)")}, {2, AttributeInfo::tr("Closed segment")}, {3, AttributeInfo::tr("Closed section")}};
    }
    if (named(a, "polygonType")) {
        return {{0, AttributeInfo::tr("Convex")}, {1, AttributeInfo::tr("Non-convex")}, {2, AttributeInfo::tr("Complex")}, {3, AttributeInfo::tr("Open")}};
    }
    if (named(a, "fillType")) {
        return {{0, AttributeInfo::tr("No fill")}, {1, AttributeInfo::tr("Line colour")}, {2, AttributeInfo::tr("Fill colour")}, {3, AttributeInfo::tr("Pattern")}};
    }
    if (named(a, "priority")) {
        return {{0, AttributeInfo::tr("High")}, {1, AttributeInfo::tr("Medium")}, {2, AttributeInfo::tr("Low")}};
    }
    if (named(a, "acousticSignal")) {
        return {{0, AttributeInfo::tr("Highest priority")}, {1, AttributeInfo::tr("Medium priority")}, {2, AttributeInfo::tr("Lowest priority")}, {3, AttributeInfo::tr("None")}};
    }
    if (named(a, "format") && (type == T::PictureGraphic || type == T::GraphicsContext)) {
        return {{0, AttributeInfo::tr("Monochrome")}, {1, AttributeInfo::tr("16 colours")}, {2, AttributeInfo::tr("256 colours")}};
    }
    if (named(a, "format")) {
        return {{0, AttributeInfo::tr("Fixed (####.nn)")}, {1, AttributeInfo::tr("Exponential (###.nnE±##)")}};
    }
    if (named(a, "validationType")) {
        return {{0, AttributeInfo::tr("Listed characters are valid")}, {1, AttributeInfo::tr("Listed characters are invalid")}};
    }
    if (named(a, "functionType")) {
        return {{0, AttributeInfo::tr("Latching boolean")}, {1, AttributeInfo::tr("Analog")}, {2, AttributeInfo::tr("Non-latching boolean")}};
    }
    return {};
}

std::vector<NamedValue> flagBits(vt::ObjectType type, const vt::AttributeDef& a)
{
    if (named(a, "fontStyle")) {
        return {{0x01, AttributeInfo::tr("Bold")},     {0x02, AttributeInfo::tr("Crossed out")},       {0x04, AttributeInfo::tr("Underlined")},
                {0x08, AttributeInfo::tr("Italic")},   {0x10, AttributeInfo::tr("Inverted")},          {0x20, AttributeInfo::tr("Flashing inverted")},
                {0x40, AttributeInfo::tr("Flashing hidden")}};
    }
    if (named(a, "lineSuppression")) {
        return {{0x01, AttributeInfo::tr("Suppress top")}, {0x02, AttributeInfo::tr("Suppress right")}, {0x04, AttributeInfo::tr("Suppress bottom")},
                {0x08, AttributeInfo::tr("Suppress left")}};
    }
    if (named(a, "options2") || (named(a, "options") && type == T::InputList)) {
        return {{0x01, AttributeInfo::tr("Enabled")}, {0x02, AttributeInfo::tr("Real time editing")}};
    }
    if (!named(a, "options")) {
        return {};
    }
    switch (type) {
    case T::Button:
        return {{0x01, AttributeInfo::tr("Latchable")},      {0x02, AttributeInfo::tr("Latched")},  {0x04, AttributeInfo::tr("Suppress border")},
                {0x08, AttributeInfo::tr("Transparent background")}, {0x10, AttributeInfo::tr("Disabled")}, {0x20, AttributeInfo::tr("No border")}};
    case T::InputString:
    case T::OutputString:
        return {{0x01, AttributeInfo::tr("Transparent")}, {0x02, AttributeInfo::tr("Auto-wrap")}, {0x04, AttributeInfo::tr("Wrap on hyphen")}};
    case T::InputNumber:
    case T::OutputNumber:
        return {{0x01, AttributeInfo::tr("Transparent")}, {0x02, AttributeInfo::tr("Leading zeros")}, {0x04, AttributeInfo::tr("Zero as blank")},
                {0x08, AttributeInfo::tr("Truncate")}};
    case T::OutputMeter:
        return {{0x01, AttributeInfo::tr("Draw arc")}, {0x02, AttributeInfo::tr("Draw border")}, {0x04, AttributeInfo::tr("Draw ticks")}, {0x08, AttributeInfo::tr("Clockwise")}};
    case T::OutputLinearBarGraph:
        return {{0x01, AttributeInfo::tr("Draw border")}, {0x02, AttributeInfo::tr("Draw target line")}, {0x04, AttributeInfo::tr("Draw ticks")},
                {0x08, AttributeInfo::tr("Line instead of bar")}, {0x10, AttributeInfo::tr("Horizontal")}, {0x20, AttributeInfo::tr("Grows right/up")}};
    case T::OutputArchedBarGraph:
        return {{0x01, AttributeInfo::tr("Draw border")}, {0x02, AttributeInfo::tr("Draw target line")}, {0x08, AttributeInfo::tr("Line instead of bar")},
                {0x10, AttributeInfo::tr("Clockwise")}};
    case T::PictureGraphic:
        return {{0x01, AttributeInfo::tr("Transparent")}, {0x02, AttributeInfo::tr("Flashing")}, {0x04, AttributeInfo::tr("Run-length encoded")}};
    case T::WindowMask:
    case T::KeyGroup:
        return {{0x01, AttributeInfo::tr("Available")}, {0x02, AttributeInfo::tr("Transparent")}};
    case T::GraphicsContext:
        return {{0x01, AttributeInfo::tr("Transparent")}, {0x02, AttributeInfo::tr("Use foreground/background colours")}};
    case T::ExternalObjectDefinition:
    case T::ExternalReferenceName:
        return {{0x01, AttributeInfo::tr("Enabled")}};
    default:
        return {};
    }
}

QString colourName(int index)
{
    static const std::array<const char*, 16> names = {
        QT_TRANSLATE_NOOP("AttributeInfo", "Black"),  QT_TRANSLATE_NOOP("AttributeInfo", "White"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Green"),  QT_TRANSLATE_NOOP("AttributeInfo", "Teal"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Maroon"), QT_TRANSLATE_NOOP("AttributeInfo", "Purple"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Olive"),  QT_TRANSLATE_NOOP("AttributeInfo", "Silver"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Grey"),   QT_TRANSLATE_NOOP("AttributeInfo", "Blue"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Lime"),   QT_TRANSLATE_NOOP("AttributeInfo", "Cyan"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Red"),    QT_TRANSLATE_NOOP("AttributeInfo", "Magenta"),
        QT_TRANSLATE_NOOP("AttributeInfo", "Yellow"), QT_TRANSLATE_NOOP("AttributeInfo", "Navy")};
    if (index >= 0 && index < 16) {
        return QStringLiteral("%1 %2").arg(index).arg(AttributeInfo::tr(names[static_cast<std::size_t>(index)]));
    }
    if (index >= 232) {
        return QStringLiteral("%1 %2").arg(index).arg(AttributeInfo::tr("(proprietary)"));
    }
    return QStringLiteral("%1 #%2").arg(index).arg(vtr::vtColour(index).name().mid(1).toUpper());
}

QIcon colourIcon(int index)
{
    static std::array<QIcon, 256> cache;
    QIcon& icon = cache[static_cast<std::size_t>(index & 0xFF)];
    if (icon.isNull()) {
        QPixmap pixmap(14, 14);
        pixmap.fill(vtr::vtColour(index));
        QPainter painter(&pixmap);
        painter.setPen(Qt::gray);
        painter.drawRect(0, 0, 13, 13);
        icon = QIcon(pixmap);
    }
    return icon;
}

std::vector<NamedValue> macroEvents()
{
    static const std::array<const char*, 28> names = {
        QT_TRANSLATE_NOOP("AttributeInfo", "On activate"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On deactivate"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On show"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On hide"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On enable"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On disable"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change active mask"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change soft key mask"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change attribute"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change background colour"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change font attributes"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change line attributes"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change fill attributes"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change child location"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change size"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change value"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change priority"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change end point"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On input field selection"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On input field deselection"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On ESC"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On entry of a value"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On entry of a new value"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On key press"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On key release"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On change child position"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On pointing event press"),
        QT_TRANSLATE_NOOP("AttributeInfo", "On pointing event release")};
    std::vector<NamedValue> events;
    for (std::size_t i = 0; i < names.size(); ++i) {
        events.push_back({static_cast<int>(i) + 1, QStringLiteral("%1 %2").arg(i + 1).arg(AttributeInfo::tr(names[i]))});
    }
    return events;
}

QString eventName(int event)
{
    for (const NamedValue& e : macroEvents()) {
        if (e.value == event) {
            return e.name;
        }
    }
    return QString::number(event);
}

} // namespace ui
