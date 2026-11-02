#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace vt {

using ObjectId = std::uint16_t;

// ISO 11783-6, 4.6.1.2: Object ID 0xFFFF is the "NULL" Object ID.
inline constexpr ObjectId kNullObjectId = 0xFFFF;

// Object type IDs from ISO 11783-6, Table A.1. Only types up to VT version 5 are listed;
// VT version 6 types (Colour Palette, graphic objects, Working Set Special Controls) follow later.
enum class ObjectType : std::uint8_t {
    WorkingSet = 0,
    DataMask = 1,
    AlarmMask = 2,
    Container = 3,
    SoftKeyMask = 4,
    Key = 5,
    Button = 6,
    InputBoolean = 7,
    InputString = 8,
    InputNumber = 9,
    InputList = 10,
    OutputString = 11,
    OutputNumber = 12,
    OutputLine = 13,
    OutputRectangle = 14,
    OutputEllipse = 15,
    OutputPolygon = 16,
    OutputMeter = 17,
    OutputLinearBarGraph = 18,
    OutputArchedBarGraph = 19,
    PictureGraphic = 20,
    NumberVariable = 21,
    StringVariable = 22,
    FontAttributes = 23,
    LineAttributes = 24,
    FillAttributes = 25,
    InputAttributes = 26,
    ObjectPointer = 27,
    Macro = 28,
    AuxiliaryFunctionType1 = 29,
    AuxiliaryInputType1 = 30,
    AuxiliaryFunctionType2 = 31,
    AuxiliaryInputType2 = 32,
    AuxiliaryControlDesignatorType2 = 33,
    WindowMask = 34,
    KeyGroup = 35,
    GraphicsContext = 36,
    OutputList = 37,
    ExtendedInputAttributes = 38,
    ColourMap = 39,
    ObjectLabelReferenceList = 40,
    ExternalObjectDefinition = 41,
    ExternalReferenceName = 42,
    ExternalObjectPointer = 43,
    Animation = 44,
};

inline constexpr int kObjectTypeCount = 45;

// VT version a pool is designed for. The designer currently targets versions 3 to 5.
enum class VtVersion : std::uint8_t {
    V3 = 3,
    V4 = 4,
    V5 = 5,
};

inline constexpr VtVersion kMinSupportedVersion = VtVersion::V3;
inline constexpr VtVersion kMaxSupportedVersion = VtVersion::V5;

constexpr int toInt(VtVersion v) { return static_cast<int>(v); }

std::optional<VtVersion> vtVersionFromInt(int value);

} // namespace vt
