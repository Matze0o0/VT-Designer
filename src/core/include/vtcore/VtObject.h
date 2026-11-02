#pragma once

#include "vtcore/Schema.h"
#include "vtcore/Types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

// Object placed inside a parent. The position belongs to the parent's entry, so the same object
// can appear in several parents at different positions.
struct ChildRef {
    ObjectId id = kNullObjectId;
    std::int16_t x = 0;
    std::int16_t y = 0;

    bool operator==(const ChildRef&) const = default;
};

// Event/Macro grouping. Macro IDs above 255 are written as VT version 5 extended references
// (two groupings, see ISO 11783-6 4.6.22.3).
struct MacroRef {
    std::uint8_t event = 0;
    ObjectId macro = kNullObjectId;

    bool operator==(const MacroRef&) const = default;
};

struct Point {
    std::uint16_t x = 0;
    std::uint16_t y = 0;

    bool operator==(const Point&) const = default;
};

// One entry of an Object Label Reference List (B.21).
struct LabelRef {
    ObjectId object = kNullObjectId;
    ObjectId stringVariable = kNullObjectId;
    std::uint8_t fontType = 0;
    ObjectId graphic = kNullObjectId;

    bool operator==(const LabelRef&) const = default;
};

struct CharacterRange {
    std::uint16_t first = 0;
    std::uint16_t last = 0;

    bool operator==(const CharacterRange&) const = default;
};

// Character ranges of one Unicode plane in an Extended Input Attributes object (B.14.6).
struct CodePlane {
    std::uint8_t plane = 0;
    std::vector<CharacterRange> ranges;

    bool operator==(const CodePlane&) const = default;
};

// A single VT object. Fixed attributes are stored by schema index; variable length sections are
// stored in the list members (only the lists present in the type's schema are used).
struct VtObject {
    ObjectId id = 0;
    ObjectType type = ObjectType::WorkingSet;
    std::string name; // designer-only, not part of the binary pool

    std::vector<double> attributes;
    std::vector<ChildRef> children;
    std::vector<ObjectId> references;
    std::vector<MacroRef> macros;
    std::vector<Point> points;
    std::vector<std::uint8_t> data;
    std::vector<std::string> languages;
    std::vector<LabelRef> labels;
    std::vector<CodePlane> codePlanes;

    // Creates an object with all attributes set to the schema defaults.
    static VtObject create(ObjectId id, ObjectType type, std::string name = {});

    const ObjectSchema& schema() const { return schemaFor(type); }

    bool hasAttribute(std::string_view attribute) const;
    double get(std::string_view attribute) const;  // throws std::out_of_range for unknown names
    void set(std::string_view attribute, double value);

    std::size_t listSize(ListKind kind) const;

    bool operator==(const VtObject&) const = default;
};

} // namespace vt
