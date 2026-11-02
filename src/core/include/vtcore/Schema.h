#pragma once

#include "vtcore/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vt {

// Wire type of a fixed attribute. All values are little endian (ISO 11783-6, Annex B).
enum class FieldType : std::uint8_t {
    UInt8,
    UInt16,
    UInt32,
    Int16,
    Int32,
    Float32,
    ObjectRef, // UInt16 holding the ID of another object in the same pool (or NULL)
};

// Variable length sections of an object record. Each object type has at most one list of each kind.
enum class ListKind : std::uint8_t {
    Children,   // {Object ID, X, Y} - objects placed inside the parent
    References, // {Object ID} - soft keys, list items, window references, ...
    Macros,     // {Event ID, Macro ID} - counted in 2-byte groupings
    Points,     // {X, Y} - polygon points
    Data,       // raw bytes - string values, bitmap data, macro commands, colour map
    Languages,  // two-letter language codes
    Labels,     // Object Label Reference List entries
    CodePlanes, // Extended Input Attributes character ranges
};

inline constexpr int kListKindCount = 8;

std::string_view listKindName(ListKind kind);

struct AttributeDef {
    std::string_view name;  // stable identifier used in project files and the API
    std::string_view label; // human readable name from the standard
    FieldType type;
    double minValue;
    double maxValue;
    double defaultValue;
    int aid = -1; // attribute ID for Change Attribute / Get Attribute Value, -1 if none
    std::vector<ObjectType> refTypes; // ObjectRef only: allowed target types, empty means any type
    bool nullAllowed = true;          // ObjectRef only

    int byteSize() const;
};

struct RecordItem {
    enum class Kind : std::uint8_t {
        Attribute, // fixed attribute, index into ObjectSchema::attributes
        Count,     // element count of a list that follows later in the record
        List,      // the list elements themselves
    };

    Kind kind;
    std::uint8_t attribute = 0;
    ListKind list = ListKind::Children;
    std::uint8_t countSize = 1; // Count only: size of the count field in bytes (1, 2 or 4)
};

struct ObjectSchema {
    ObjectType type;
    std::string_view name;  // identifier, e.g. "DataMask"
    std::string_view label; // e.g. "Data Mask"
    VtVersion minVersion;
    std::vector<AttributeDef> attributes;
    std::vector<RecordItem> layout; // record order after Object ID and Type

    bool hasList(ListKind kind) const;
    int attributeIndex(std::string_view attributeName) const; // -1 if unknown
};

// Returns nullptr for object types the designer does not know (VT6, proprietary, reserved).
const ObjectSchema* findSchema(ObjectType type);
const ObjectSchema* findSchema(std::uint8_t rawType);
const ObjectSchema& schemaFor(ObjectType type);
std::span<const ObjectSchema> allSchemas();

std::string_view objectTypeName(ObjectType type);
std::optional<ObjectType> objectTypeFromName(std::string_view name);

} // namespace vt
