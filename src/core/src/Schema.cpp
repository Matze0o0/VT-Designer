#include "vtcore/Schema.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace vt {

namespace {

using T = ObjectType;
using L = ListKind;

constexpr double kMaxU8 = 255.0;
constexpr double kMaxU16 = 65535.0;
constexpr double kMaxU32 = 4294967295.0;

// Builds one ObjectSchema in record order. Attribute arguments follow the column order of the
// Annex B tables: name, AID, default value and (optional) range.
class Builder {
public:
    Builder(ObjectType type, std::string_view name, std::string_view label, VtVersion minVersion)
    {
        m_schema.type = type;
        m_schema.name = name;
        m_schema.label = label;
        m_schema.minVersion = minVersion;
    }

    Builder& u8(std::string_view name, std::string_view label, int aid, double def, double min = 0,
                double max = kMaxU8)
    {
        return attribute(name, label, FieldType::UInt8, aid, def, min, max);
    }

    Builder& u16(std::string_view name, std::string_view label, int aid, double def, double min = 0,
                 double max = kMaxU16)
    {
        return attribute(name, label, FieldType::UInt16, aid, def, min, max);
    }

    Builder& u32(std::string_view name, std::string_view label, int aid, double def, double min = 0,
                 double max = kMaxU32)
    {
        return attribute(name, label, FieldType::UInt32, aid, def, min, max);
    }

    Builder& i16(std::string_view name, std::string_view label, int aid, double def, double min = -32768.0,
                 double max = 32767.0)
    {
        return attribute(name, label, FieldType::Int16, aid, def, min, max);
    }

    Builder& i32(std::string_view name, std::string_view label, int aid, double def)
    {
        return attribute(name, label, FieldType::Int32, aid, def, -2147483648.0, 2147483647.0);
    }

    Builder& f32(std::string_view name, std::string_view label, int aid, double def, double min = -3.4e38,
                 double max = 3.4e38)
    {
        return attribute(name, label, FieldType::Float32, aid, def, min, max);
    }

    Builder& colour(std::string_view name, std::string_view label, int aid, double def)
    {
        return u8(name, label, aid, def);
    }

    Builder& boolean(std::string_view name, std::string_view label, int aid, double def)
    {
        return u8(name, label, aid, def, 0, 1);
    }

    Builder& ref(std::string_view name, std::string_view label, int aid, std::initializer_list<ObjectType> types,
                 bool nullAllowed)
    {
        attribute(name, label, FieldType::ObjectRef, aid, nullAllowed ? kNullObjectId : 0, 0, kMaxU16);
        m_schema.attributes.back().refTypes.assign(types.begin(), types.end());
        m_schema.attributes.back().nullAllowed = nullAllowed;
        return *this;
    }

    Builder& count(ListKind list, int bytes = 1)
    {
        m_schema.layout.push_back({RecordItem::Kind::Count, 0, list, static_cast<std::uint8_t>(bytes)});
        return *this;
    }

    Builder& list(ListKind list)
    {
        m_schema.layout.push_back({RecordItem::Kind::List, 0, list, 0});
        return *this;
    }

    Builder& macros() { return count(L::Macros).list(L::Macros); }

    Builder& childrenAndMacros()
    {
        return count(L::Children).count(L::Macros).list(L::Children).list(L::Macros);
    }

    Builder& referencesAndMacros()
    {
        return count(L::References).count(L::Macros).list(L::References).list(L::Macros);
    }

    ObjectSchema build() { return std::move(m_schema); }

private:
    Builder& attribute(std::string_view name, std::string_view label, FieldType type, int aid, double def,
                       double min, double max)
    {
        m_schema.layout.push_back(
            {RecordItem::Kind::Attribute, static_cast<std::uint8_t>(m_schema.attributes.size()), {}, 0});
        m_schema.attributes.push_back({name, label, type, min, max, def, aid, {}, true});
        return *this;
    }

    ObjectSchema m_schema;
};

// Object types that may appear as the displayed content of a variable reference etc.
const std::initializer_list<ObjectType> kFontAttributes = {T::FontAttributes};
const std::initializer_list<ObjectType> kNumberVariable = {T::NumberVariable};
const std::initializer_list<ObjectType> kStringVariable = {T::StringVariable};
const std::initializer_list<ObjectType> kLineAttributes = {T::LineAttributes};
const std::initializer_list<ObjectType> kFillAttributes = {T::FillAttributes};
const std::initializer_list<ObjectType> kOutputStringOrPointer = {T::OutputString, T::ObjectPointer};

std::vector<ObjectSchema> buildSchemas()
{
    std::vector<ObjectSchema> s;

    // B.1
    s.push_back(Builder(T::WorkingSet, "WorkingSet", "Working Set", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .boolean("selectable", "Selectable", 2, 1)
                    .ref("activeMask", "Active mask", 3, {T::DataMask, T::AlarmMask}, false)
                    .count(L::Children)
                    .count(L::Macros)
                    .count(L::Languages)
                    .list(L::Children)
                    .list(L::Macros)
                    .list(L::Languages)
                    .build());

    // B.2
    s.push_back(Builder(T::DataMask, "DataMask", "Data Mask", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .ref("softKeyMask", "Soft Key Mask", 2, {T::SoftKeyMask}, true)
                    .childrenAndMacros()
                    .build());

    // B.3
    s.push_back(Builder(T::AlarmMask, "AlarmMask", "Alarm Mask", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .ref("softKeyMask", "Soft Key Mask", 2, {T::SoftKeyMask}, true)
                    .u8("priority", "Priority", 3, 1, 0, 2)
                    .u8("acousticSignal", "Acoustic signal", 4, 1, 0, 3)
                    .childrenAndMacros()
                    .build());

    // B.4
    s.push_back(Builder(T::Container, "Container", "Container", VtVersion::V3)
                    .u16("width", "Width", 1, 100)
                    .u16("height", "Height", 2, 100)
                    .boolean("hidden", "Hidden", 3, 0)
                    .childrenAndMacros()
                    .build());

    // B.5
    s.push_back(Builder(T::SoftKeyMask, "SoftKeyMask", "Soft Key Mask", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .referencesAndMacros()
                    .build());

    // B.6
    s.push_back(Builder(T::Key, "Key", "Key", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .u8("keyCode", "Key code", 2, 1, 1, kMaxU8)
                    .childrenAndMacros()
                    .build());

    // B.7
    s.push_back(Builder(T::Button, "Button", "Button", VtVersion::V3)
                    .u16("width", "Width", 1, 80)
                    .u16("height", "Height", 2, 40)
                    .colour("backgroundColour", "Background colour", 3, 7)
                    .colour("borderColour", "Border colour", 4, 0)
                    .u8("keyCode", "Key code", 5, 0)
                    .u8("options", "Options", 6, 0, 0, 63)
                    .childrenAndMacros()
                    .build());

    // B.8.2
    s.push_back(Builder(T::InputBoolean, "InputBoolean", "Input Boolean", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .u16("width", "Width", 2, 20)
                    .ref("foregroundColour", "Foreground colour", 3, kFontAttributes, false)
                    .ref("variableReference", "Variable reference", 4, kNumberVariable, true)
                    .u8("value", "Value", 5, 0)
                    .boolean("enabled", "Enabled", 6, 1)
                    .macros()
                    .build());

    // B.8.3
    s.push_back(Builder(T::InputString, "InputString", "Input String", VtVersion::V3)
                    .u16("width", "Width", 1, 120)
                    .u16("height", "Height", 2, 24)
                    .colour("backgroundColour", "Background colour", 3, 1)
                    .ref("fontAttributes", "Font attributes", 4, kFontAttributes, false)
                    .ref("inputAttributes", "Input attributes", 5, {T::InputAttributes, T::ExtendedInputAttributes},
                         true)
                    .u8("options", "Options", 6, 0, 0, 7)
                    .ref("variableReference", "Variable reference", 7, kStringVariable, true)
                    .u8("justification", "Justification", 8, 0, 0, 15)
                    .count(L::Data)
                    .list(L::Data)
                    .boolean("enabled", "Enabled", 9, 1)
                    .macros()
                    .build());

    // B.8.4
    s.push_back(Builder(T::InputNumber, "InputNumber", "Input Number", VtVersion::V3)
                    .u16("width", "Width", 1, 80)
                    .u16("height", "Height", 2, 24)
                    .colour("backgroundColour", "Background colour", 3, 1)
                    .ref("fontAttributes", "Font attributes", 4, kFontAttributes, false)
                    .u8("options", "Options", 5, 0, 0, 15)
                    .ref("variableReference", "Variable reference", 6, kNumberVariable, true)
                    .u32("value", "Value", 14, 0)
                    .u32("minValue", "Min value", 7, 0)
                    .u32("maxValue", "Max value", 8, 100)
                    .i32("offset", "Offset", 9, 0)
                    .f32("scale", "Scale", 10, 1.0)
                    .u8("numberOfDecimals", "Number of decimals", 11, 0, 0, 7)
                    .boolean("format", "Format", 12, 0)
                    .u8("justification", "Justification", 13, 0, 0, 15)
                    .u8("options2", "Options 2", 15, 1, 0, 3)
                    .macros()
                    .build());

    // B.8.5
    s.push_back(Builder(T::InputList, "InputList", "Input List", VtVersion::V3)
                    .u16("width", "Width", 1, 120)
                    .u16("height", "Height", 2, 24)
                    .ref("variableReference", "Variable reference", 3, kNumberVariable, true)
                    .u8("value", "Value", 4, 0)
                    .count(L::References)
                    .u8("options", "Options", 5, 1, 0, 3)
                    .count(L::Macros)
                    .list(L::References)
                    .list(L::Macros)
                    .build());

    // B.9.2
    s.push_back(Builder(T::OutputString, "OutputString", "Output String", VtVersion::V3)
                    .u16("width", "Width", 1, 120)
                    .u16("height", "Height", 2, 24)
                    .colour("backgroundColour", "Background colour", 3, 1)
                    .ref("fontAttributes", "Font attributes", 4, kFontAttributes, false)
                    .u8("options", "Options", 5, 0, 0, 7)
                    .ref("variableReference", "Variable reference", 6, kStringVariable, true)
                    .u8("justification", "Justification", 7, 0, 0, 15)
                    .count(L::Data, 2)
                    .list(L::Data)
                    .macros()
                    .build());

    // B.9.3
    s.push_back(Builder(T::OutputNumber, "OutputNumber", "Output Number", VtVersion::V3)
                    .u16("width", "Width", 1, 80)
                    .u16("height", "Height", 2, 24)
                    .colour("backgroundColour", "Background colour", 3, 1)
                    .ref("fontAttributes", "Font attributes", 4, kFontAttributes, false)
                    .u8("options", "Options", 5, 0, 0, 15)
                    .ref("variableReference", "Variable reference", 6, kNumberVariable, true)
                    .u32("value", "Value", 12, 0)
                    .i32("offset", "Offset", 7, 0)
                    .f32("scale", "Scale", 8, 1.0)
                    .u8("numberOfDecimals", "Number of decimals", 9, 0, 0, 7)
                    .boolean("format", "Format", 10, 0)
                    .u8("justification", "Justification", 11, 0, 0, 15)
                    .macros()
                    .build());

    // B.10.2
    s.push_back(Builder(T::OutputLine, "OutputLine", "Output Line", VtVersion::V3)
                    .ref("lineAttributes", "Line attributes", 1, kLineAttributes, false)
                    .u16("width", "Width", 2, 50)
                    .u16("height", "Height", 3, 50)
                    .u8("lineDirection", "Line direction", 4, 0, 0, 1)
                    .macros()
                    .build());

    // B.10.3
    s.push_back(Builder(T::OutputRectangle, "OutputRectangle", "Output Rectangle", VtVersion::V3)
                    .ref("lineAttributes", "Line attributes", 1, kLineAttributes, false)
                    .u16("width", "Width", 2, 60)
                    .u16("height", "Height", 3, 40)
                    .u8("lineSuppression", "Line suppression", 4, 0, 0, 15)
                    .ref("fillAttributes", "Fill attributes", 5, kFillAttributes, true)
                    .macros()
                    .build());

    // B.10.4
    s.push_back(Builder(T::OutputEllipse, "OutputEllipse", "Output Ellipse", VtVersion::V3)
                    .ref("lineAttributes", "Line attributes", 1, kLineAttributes, false)
                    .u16("width", "Width", 2, 60)
                    .u16("height", "Height", 3, 40)
                    .u8("ellipseType", "Ellipse type", 4, 0, 0, 3)
                    .u8("startAngle", "Start angle", 5, 0, 0, 180)
                    .u8("endAngle", "End angle", 6, 0, 0, 180)
                    .ref("fillAttributes", "Fill attributes", 7, kFillAttributes, true)
                    .macros()
                    .build());

    // B.10.5
    s.push_back(Builder(T::OutputPolygon, "OutputPolygon", "Output Polygon", VtVersion::V3)
                    .u16("width", "Width", 1, 60)
                    .u16("height", "Height", 2, 40)
                    .ref("lineAttributes", "Line attributes", 3, kLineAttributes, false)
                    .ref("fillAttributes", "Fill attributes", 4, kFillAttributes, true)
                    .u8("polygonType", "Polygon type", 5, 0, 0, 3)
                    .count(L::Points)
                    .count(L::Macros)
                    .list(L::Points)
                    .list(L::Macros)
                    .build());

    // B.11.2
    s.push_back(Builder(T::OutputMeter, "OutputMeter", "Output Meter", VtVersion::V3)
                    .u16("width", "Width", 1, 100)
                    .colour("needleColour", "Needle colour", 2, 0)
                    .colour("borderColour", "Border colour", 3, 0)
                    .colour("arcAndTickColour", "Arc and tick colour", 4, 0)
                    .u8("options", "Options", 5, 15, 0, 15)
                    .u8("numberOfTicks", "Number of ticks", 6, 5)
                    .u8("startAngle", "Start angle", 7, 112, 0, 180)
                    .u8("endAngle", "End angle", 8, 158, 0, 180)
                    .u16("minValue", "Min value", 9, 0)
                    .u16("maxValue", "Max value", 10, 100)
                    .ref("variableReference", "Variable reference", 11, kNumberVariable, true)
                    .u16("value", "Value", 12, 0)
                    .macros()
                    .build());

    // B.11.3
    s.push_back(Builder(T::OutputLinearBarGraph, "OutputLinearBarGraph", "Output Linear Bar Graph", VtVersion::V3)
                    .u16("width", "Width", 1, 20)
                    .u16("height", "Height", 2, 100)
                    .colour("colour", "Colour", 3, 2)
                    .colour("targetLineColour", "Target line colour", 4, 0)
                    .u8("options", "Options", 5, 33, 0, 63) // border, grows upwards
                    .u8("numberOfTicks", "Number of ticks", 6, 0)
                    .u16("minValue", "Min value", 7, 0)
                    .u16("maxValue", "Max value", 8, 100)
                    .ref("variableReference", "Variable reference", 9, kNumberVariable, true)
                    .u16("value", "Value", 12, 0)
                    .ref("targetValueVariableReference", "Target value variable reference", 10, kNumberVariable,
                         true)
                    .u16("targetValue", "Target value", 11, 0)
                    .macros()
                    .build());

    // B.11.4
    s.push_back(Builder(T::OutputArchedBarGraph, "OutputArchedBarGraph", "Output Arched Bar Graph", VtVersion::V3)
                    .u16("width", "Width", 1, 100)
                    .u16("height", "Height", 2, 100)
                    .colour("colour", "Colour", 3, 2)
                    .colour("targetLineColour", "Target line colour", 4, 0)
                    .u8("options", "Options", 5, 1, 0, 31)
                    .u8("startAngle", "Start angle", 6, 112, 0, 180)
                    .u8("endAngle", "End angle", 7, 158, 0, 180)
                    .u16("barGraphWidth", "Bar graph width", 8, 20)
                    .u16("minValue", "Min value", 9, 0)
                    .u16("maxValue", "Max value", 10, 100)
                    .ref("variableReference", "Variable reference", 11, kNumberVariable, true)
                    .u16("value", "Value", 14, 0)
                    .ref("targetValueVariableReference", "Target value variable reference", 12, kNumberVariable,
                         true)
                    .u16("targetValue", "Target value", 13, 0)
                    .macros()
                    .build());

    // B.12
    s.push_back(Builder(T::PictureGraphic, "PictureGraphic", "Picture Graphic", VtVersion::V3)
                    .u16("width", "Width", 1, 0)
                    .u16("actualWidth", "Actual width", 4, 0)
                    .u16("actualHeight", "Actual height", 5, 0)
                    .u8("format", "Format", 6, 2, 0, 2)
                    .u8("options", "Options", 2, 0, 0, 7)
                    .colour("transparencyColour", "Transparency colour", 3, 0)
                    .count(L::Data, 4)
                    .count(L::Macros)
                    .list(L::Data)
                    .list(L::Macros)
                    .build());

    // B.13.2
    s.push_back(Builder(T::NumberVariable, "NumberVariable", "Number Variable", VtVersion::V3)
                    .u32("value", "Value", 1, 0)
                    .build());

    // B.13.3
    s.push_back(Builder(T::StringVariable, "StringVariable", "String Variable", VtVersion::V3)
                    .count(L::Data, 2)
                    .list(L::Data)
                    .build());

    // B.14.2
    s.push_back(Builder(T::FontAttributes, "FontAttributes", "Font Attributes", VtVersion::V3)
                    .colour("fontColour", "Font colour", 1, 0)
                    .u8("fontSize", "Font size", 2, 2, 0, 14)
                    .u8("fontType", "Font type", 3, 0)
                    .u8("fontStyle", "Font style", 4, 0)
                    .macros()
                    .build());

    // B.14.3
    s.push_back(Builder(T::LineAttributes, "LineAttributes", "Line Attributes", VtVersion::V3)
                    .colour("lineColour", "Line colour", 1, 0)
                    .u8("lineWidth", "Line width", 2, 1)
                    .u16("lineArt", "Line art", 3, 0xFFFF)
                    .macros()
                    .build());

    // B.14.4
    s.push_back(Builder(T::FillAttributes, "FillAttributes", "Fill Attributes", VtVersion::V3)
                    .u8("fillType", "Fill type", 1, 2, 0, 3)
                    .colour("fillColour", "Fill colour", 2, 1)
                    .ref("fillPattern", "Fill pattern", 3, {T::PictureGraphic}, true)
                    .macros()
                    .build());

    // B.14.5
    s.push_back(Builder(T::InputAttributes, "InputAttributes", "Input Attributes", VtVersion::V3)
                    .u8("validationType", "Validation type", 1, 0, 0, 1)
                    .count(L::Data)
                    .list(L::Data)
                    .macros()
                    .build());

    // B.15
    s.push_back(Builder(T::ObjectPointer, "ObjectPointer", "Object Pointer", VtVersion::V3)
                    .ref("value", "Value", 1, {}, true)
                    .build());

    // B.16
    s.push_back(Builder(T::Macro, "Macro", "Macro", VtVersion::V3)
                    .count(L::Data, 2)
                    .list(L::Data)
                    .build());

    // J.4.2
    s.push_back(Builder(T::AuxiliaryFunctionType1, "AuxiliaryFunctionType1", "Auxiliary Function Type 1",
                        VtVersion::V3)
                    .colour("backgroundColour", "Background colour", -1, 1)
                    .u8("functionType", "Function type", -1, 0, 0, 2)
                    .count(L::Children)
                    .list(L::Children)
                    .build());

    // J.4.4
    s.push_back(Builder(T::AuxiliaryInputType1, "AuxiliaryInputType1", "Auxiliary Input Type 1", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", -1, 1)
                    .u8("functionType", "Function type", -1, 0, 0, 2)
                    .u8("inputId", "Input ID", -1, 0, 0, 250)
                    .count(L::Children)
                    .list(L::Children)
                    .build());

    // J.4.3
    s.push_back(Builder(T::AuxiliaryFunctionType2, "AuxiliaryFunctionType2", "Auxiliary Function Type 2",
                        VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .u8("functionAttributes", "Function attributes", 2, 0)
                    .count(L::Children)
                    .list(L::Children)
                    .build());

    // J.4.5
    s.push_back(Builder(T::AuxiliaryInputType2, "AuxiliaryInputType2", "Auxiliary Input Type 2", VtVersion::V3)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .u8("functionAttributes", "Function attributes", 2, 0)
                    .count(L::Children)
                    .list(L::Children)
                    .build());

    // J.4.7
    s.push_back(Builder(T::AuxiliaryControlDesignatorType2, "AuxiliaryControlDesignatorType2",
                        "Auxiliary Control Designator Type 2 Object Pointer", VtVersion::V3)
                    .u8("pointerType", "Pointer type", 1, 0, 0, 3)
                    .ref("value", "Value", 2, {T::AuxiliaryFunctionType2, T::AuxiliaryInputType2}, true)
                    .build());

    // B.19
    s.push_back(Builder(T::WindowMask, "WindowMask", "Window Mask", VtVersion::V4)
                    .u8("width", "Width (cells)", -1, 1, 1, 2)
                    .u8("height", "Height (cells)", -1, 1, 1, 6)
                    .u8("windowType", "Window type", -1, 0, 0, 18)
                    .colour("backgroundColour", "Background colour", 1, 1)
                    .u8("options", "Options", 2, 1, 0, 3)
                    .ref("name", "Name", 3, kOutputStringOrPointer, false)
                    .ref("windowTitle", "Window title", -1, kOutputStringOrPointer, true)
                    .ref("windowIcon", "Window icon", -1, {}, true)
                    .count(L::References)
                    .count(L::Children)
                    .count(L::Macros)
                    .list(L::References)
                    .list(L::Children)
                    .list(L::Macros)
                    .build());

    // B.20
    s.push_back(Builder(T::KeyGroup, "KeyGroup", "Key Group", VtVersion::V4)
                    .u8("options", "Options", 1, 1, 0, 3)
                    .ref("name", "Name", 2, kOutputStringOrPointer, false)
                    .ref("keyGroupIcon", "Key group icon", -1, {}, true)
                    .referencesAndMacros()
                    .build());

    // B.18
    s.push_back(Builder(T::GraphicsContext, "GraphicsContext", "Graphics Context", VtVersion::V4)
                    .u16("viewportWidth", "Viewport width", 1, 100, 0, 32767)
                    .u16("viewportHeight", "Viewport height", 2, 100, 0, 32767)
                    .i16("viewportX", "Viewport X", 3, 0)
                    .i16("viewportY", "Viewport Y", 4, 0)
                    .u16("canvasWidth", "Canvas width", 5, 100, 0, 32767)
                    .u16("canvasHeight", "Canvas height", 6, 100, 0, 32767)
                    .f32("viewportZoom", "Viewport zoom", 7, 1.0, -32.0, 32.0)
                    .i16("graphicsCursorX", "Graphics cursor X", 8, 0)
                    .i16("graphicsCursorY", "Graphics cursor Y", 9, 0)
                    .colour("foregroundColour", "Foreground colour", 10, 0)
                    .colour("backgroundColour", "Background colour", 11, 1)
                    .ref("fontAttributes", "Font attributes", 12, kFontAttributes, true)
                    .ref("lineAttributes", "Line attributes", 13, kLineAttributes, true)
                    .ref("fillAttributes", "Fill attributes", 14, kFillAttributes, true)
                    .u8("format", "Format", 15, 2, 0, 2)
                    .u8("options", "Options", 16, 0, 0, 3)
                    .colour("transparencyColour", "Transparency colour", 17, 0)
                    .build());

    // B.9.4
    s.push_back(Builder(T::OutputList, "OutputList", "Output List", VtVersion::V4)
                    .u16("width", "Width", 1, 120)
                    .u16("height", "Height", 2, 24)
                    .ref("variableReference", "Variable reference", 3, kNumberVariable, true)
                    .u8("value", "Value", 4, 0)
                    .referencesAndMacros()
                    .build());

    // B.14.6
    s.push_back(Builder(T::ExtendedInputAttributes, "ExtendedInputAttributes", "Extended Input Attributes",
                        VtVersion::V4)
                    .u8("validationType", "Validation type", 1, 0, 0, 1)
                    .count(L::CodePlanes)
                    .list(L::CodePlanes)
                    .build());

    // B.17
    s.push_back(Builder(T::ColourMap, "ColourMap", "Colour Map", VtVersion::V4)
                    .count(L::Data, 2)
                    .list(L::Data)
                    .build());

    // B.21
    s.push_back(Builder(T::ObjectLabelReferenceList, "ObjectLabelReferenceList", "Object Label Reference List",
                        VtVersion::V4)
                    .count(L::Labels, 2)
                    .list(L::Labels)
                    .build());

    // B.22 - the 64-bit NAME is stored as two 32-bit little endian halves, exactly as on the wire.
    s.push_back(Builder(T::ExternalObjectDefinition, "ExternalObjectDefinition", "External Object Definition",
                        VtVersion::V5)
                    .u8("options", "Options", 1, 0, 0, 1)
                    .u32("nameLow", "NAME (bytes 0-3)", 2, 0)
                    .u32("nameHigh", "NAME (bytes 4-7)", 3, 0)
                    .count(L::References)
                    .list(L::References)
                    .build());

    // B.23
    s.push_back(Builder(T::ExternalReferenceName, "ExternalReferenceName", "External Reference NAME", VtVersion::V5)
                    .u8("options", "Options", 1, 0, 0, 1)
                    .u32("nameLow", "NAME (bytes 0-3)", 2, 0)
                    .u32("nameHigh", "NAME (bytes 4-7)", 3, 0)
                    .build());

    // B.24 - the external object ID lives in another Working Set's pool, so it is a plain number here.
    s.push_back(Builder(T::ExternalObjectPointer, "ExternalObjectPointer", "External Object Pointer", VtVersion::V5)
                    .ref("defaultObjectId", "Default object ID", 1, {}, true)
                    .ref("externalReferenceName", "External reference NAME", 2, {T::ExternalReferenceName}, true)
                    .u16("externalObjectId", "External object ID", 3, kNullObjectId)
                    .build());

    // B.25
    s.push_back(Builder(T::Animation, "Animation", "Animation", VtVersion::V5)
                    .u16("width", "Width", 1, 50)
                    .u16("height", "Height", 2, 50)
                    .u16("refreshInterval", "Refresh interval", 3, 500)
                    .u8("value", "Value", 4, 0)
                    .boolean("enabled", "Enabled", 5, 1)
                    .u8("firstChildIndex", "First child index", 6, 0, 0, 254)
                    .u8("lastChildIndex", "Last child index", 7, 0, 0, 254)
                    .u8("defaultChildIndex", "Default child index", 8, 0, 0, 254)
                    .u8("options", "Options", 9, 0, 0, 7)
                    .childrenAndMacros()
                    .build());

    std::sort(s.begin(), s.end(),
              [](const ObjectSchema& a, const ObjectSchema& b) { return a.type < b.type; });
    return s;
}

const std::vector<ObjectSchema>& schemas()
{
    static const std::vector<ObjectSchema> table = buildSchemas();
    return table;
}

} // namespace

std::string_view listKindName(ListKind kind)
{
    switch (kind) {
    case ListKind::Children: return "children";
    case ListKind::References: return "references";
    case ListKind::Macros: return "macros";
    case ListKind::Points: return "points";
    case ListKind::Data: return "data";
    case ListKind::Languages: return "languages";
    case ListKind::Labels: return "labels";
    case ListKind::CodePlanes: return "codePlanes";
    }
    return "unknown";
}

int AttributeDef::byteSize() const
{
    switch (type) {
    case FieldType::UInt8: return 1;
    case FieldType::UInt16:
    case FieldType::Int16:
    case FieldType::ObjectRef: return 2;
    case FieldType::UInt32:
    case FieldType::Int32:
    case FieldType::Float32: return 4;
    }
    return 0;
}

bool ObjectSchema::hasList(ListKind kind) const
{
    return std::any_of(layout.begin(), layout.end(), [kind](const RecordItem& item) {
        return item.kind == RecordItem::Kind::List && item.list == kind;
    });
}

int ObjectSchema::attributeIndex(std::string_view attributeName) const
{
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        if (attributes[i].name == attributeName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const ObjectSchema* findSchema(ObjectType type)
{
    return findSchema(static_cast<std::uint8_t>(type));
}

const ObjectSchema* findSchema(std::uint8_t rawType)
{
    // The table is sorted and contiguous from type 0, so the type doubles as index.
    const auto& table = schemas();
    if (rawType < table.size() && static_cast<std::uint8_t>(table[rawType].type) == rawType) {
        return &table[rawType];
    }
    return nullptr;
}

const ObjectSchema& schemaFor(ObjectType type)
{
    if (const ObjectSchema* s = findSchema(type)) {
        return *s;
    }
    throw std::out_of_range("Unknown VT object type " + std::to_string(static_cast<int>(type)));
}

std::span<const ObjectSchema> allSchemas()
{
    return schemas();
}

std::string_view objectTypeName(ObjectType type)
{
    const ObjectSchema* s = findSchema(type);
    return s ? s->name : std::string_view("Unknown");
}

std::optional<ObjectType> objectTypeFromName(std::string_view name)
{
    for (const ObjectSchema& s : schemas()) {
        if (s.name == name) {
            return s.type;
        }
    }
    return std::nullopt;
}

std::optional<VtVersion> vtVersionFromInt(int value)
{
    if (value >= toInt(kMinSupportedVersion) && value <= toInt(kMaxSupportedVersion)) {
        return static_cast<VtVersion>(value);
    }
    return std::nullopt;
}

} // namespace vt
