#include "vtcore/IopCodec.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace vt {

namespace {

constexpr std::uint8_t kExtendedMacroEvent = 0xFF;

class Writer {
public:
    explicit Writer(const VtObject& object)
        : m_object(object)
    {
    }

    void u8(std::uint64_t value) { m_bytes.push_back(static_cast<std::uint8_t>(value & 0xFF)); }

    void u16(std::uint64_t value)
    {
        u8(value);
        u8(value >> 8);
    }

    void u32(std::uint64_t value)
    {
        u16(value);
        u16(value >> 16);
    }

    void unsignedValue(std::uint64_t value, int size)
    {
        switch (size) {
        case 1: u8(value); break;
        case 2: u16(value); break;
        default: u32(value); break;
        }
    }

    void bytes(const std::vector<std::uint8_t>& data) { m_bytes.insert(m_bytes.end(), data.begin(), data.end()); }

    [[noreturn]] void fail(const std::string& message) const { throw IopEncodeError(message, m_object.id); }

    std::vector<std::uint8_t> take() { return std::move(m_bytes); }

private:
    const VtObject& m_object;
    std::vector<std::uint8_t> m_bytes;
};

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::size_t offset)
        : m_bytes(bytes)
        , m_offset(offset)
    {
    }

    std::size_t offset() const { return m_offset; }

    std::uint8_t u8()
    {
        need(1);
        return m_bytes[m_offset++];
    }

    std::uint16_t u16()
    {
        const std::uint16_t low = u8();
        return static_cast<std::uint16_t>(low | (u8() << 8));
    }

    std::uint32_t u32()
    {
        const std::uint32_t low = u16();
        return low | (static_cast<std::uint32_t>(u16()) << 16);
    }

    std::uint32_t unsignedValue(int size)
    {
        switch (size) {
        case 1: return u8();
        case 2: return u16();
        default: return u32();
        }
    }

    void need(std::size_t count) const
    {
        if (m_bytes.size() - m_offset < count) {
            fail("Unexpected end of data (" + std::to_string(count) + " more bytes needed)");
        }
    }

    [[noreturn]] void fail(const std::string& message) const { throw IopDecodeError(message, m_offset); }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset;
};

std::uint64_t maxForSize(int size)
{
    return size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
}

std::size_t macroGroupings(const VtObject& object)
{
    std::size_t count = 0;
    for (const MacroRef& ref : object.macros) {
        count += ref.macro > 0xFF ? 2 : 1;
    }
    return count;
}

// Number written into a Count field. Macros are counted in 2-byte groupings, all other lists in elements.
std::size_t countFieldValue(const VtObject& object, ListKind kind)
{
    return kind == ListKind::Macros ? macroGroupings(object) : object.listSize(kind);
}

void writeAttribute(Writer& w, const AttributeDef& def, double value)
{
    if (def.type == FieldType::Float32) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
        w.u32(bits);
        return;
    }

    const double rounded = std::round(value);
    double min = 0;
    double max = 0;
    switch (def.type) {
    case FieldType::UInt8: max = 255; break;
    case FieldType::UInt16:
    case FieldType::ObjectRef: max = 65535; break;
    case FieldType::UInt32: max = 4294967295.0; break;
    case FieldType::Int16: min = -32768; max = 32767; break;
    case FieldType::Int32: min = -2147483648.0; max = 2147483647.0; break;
    case FieldType::Float32: break;
    }
    if (!(rounded >= min && rounded <= max)) {
        w.fail("Attribute '" + std::string(def.name) + "' value " + std::to_string(value) +
               " does not fit its field");
    }

    // Two's complement for the signed types falls out of the unsigned conversion of the masked value.
    const auto raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(rounded));
    w.unsignedValue(raw, def.byteSize());
}

void writeList(Writer& w, const VtObject& object, ListKind kind)
{
    switch (kind) {
    case ListKind::Children:
        for (const ChildRef& child : object.children) {
            w.u16(child.id);
            w.u16(static_cast<std::uint16_t>(child.x));
            w.u16(static_cast<std::uint16_t>(child.y));
        }
        break;
    case ListKind::References:
        for (ObjectId id : object.references) {
            w.u16(id);
        }
        break;
    case ListKind::Macros:
        for (const MacroRef& ref : object.macros) {
            if (ref.macro > 0xFF) {
                w.u8(kExtendedMacroEvent);
                w.u8(ref.macro & 0xFF);
                w.u8(ref.event);
                w.u8(ref.macro >> 8);
            } else {
                w.u8(ref.event);
                w.u8(ref.macro);
            }
        }
        break;
    case ListKind::Points:
        for (const Point& point : object.points) {
            w.u16(point.x);
            w.u16(point.y);
        }
        break;
    case ListKind::Data:
        w.bytes(object.data);
        break;
    case ListKind::Languages:
        for (const std::string& code : object.languages) {
            if (code.size() != 2) {
                w.fail("Language code '" + code + "' must have exactly two characters");
            }
            w.u8(static_cast<std::uint8_t>(code[0]));
            w.u8(static_cast<std::uint8_t>(code[1]));
        }
        break;
    case ListKind::Labels:
        for (const LabelRef& label : object.labels) {
            w.u16(label.object);
            w.u16(label.stringVariable);
            w.u8(label.fontType);
            w.u16(label.graphic);
        }
        break;
    case ListKind::CodePlanes:
        for (const CodePlane& plane : object.codePlanes) {
            if (plane.ranges.size() > 0xFF) {
                w.fail("Code plane " + std::to_string(plane.plane) + " has more than 255 character ranges");
            }
            w.u8(plane.plane);
            w.u8(plane.ranges.size());
            for (const CharacterRange& range : plane.ranges) {
                w.u16(range.first);
                w.u16(range.last);
            }
        }
        break;
    }
}

double readAttribute(Reader& r, const AttributeDef& def)
{
    switch (def.type) {
    case FieldType::UInt8: return r.u8();
    case FieldType::UInt16:
    case FieldType::ObjectRef: return r.u16();
    case FieldType::UInt32: return r.u32();
    case FieldType::Int16: return static_cast<std::int16_t>(r.u16());
    case FieldType::Int32: return static_cast<std::int32_t>(r.u32());
    case FieldType::Float32: return std::bit_cast<float>(r.u32());
    }
    return 0;
}

void readList(Reader& r, VtObject& object, ListKind kind, std::size_t count)
{
    switch (kind) {
    case ListKind::Children:
        object.children.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            ChildRef child;
            child.id = r.u16();
            child.x = static_cast<std::int16_t>(r.u16());
            child.y = static_cast<std::int16_t>(r.u16());
            object.children.push_back(child);
        }
        break;
    case ListKind::References:
        object.references.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            object.references.push_back(r.u16());
        }
        break;
    case ListKind::Macros:
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t event = r.u8();
            const std::uint8_t macro = r.u8();
            if (event == kExtendedMacroEvent) {
                if (i + 1 >= count) {
                    r.fail("Extended macro reference is missing its second grouping");
                }
                ++i;
                const std::uint8_t realEvent = r.u8();
                const std::uint8_t high = r.u8();
                object.macros.push_back({realEvent, static_cast<ObjectId>(macro | (high << 8))});
            } else {
                object.macros.push_back({event, macro});
            }
        }
        break;
    case ListKind::Points:
        object.points.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Point point;
            point.x = r.u16();
            point.y = r.u16();
            object.points.push_back(point);
        }
        break;
    case ListKind::Data:
        r.need(count);
        object.data.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            object.data.push_back(r.u8());
        }
        break;
    case ListKind::Languages:
        for (std::size_t i = 0; i < count; ++i) {
            std::string code(2, ' ');
            code[0] = static_cast<char>(r.u8());
            code[1] = static_cast<char>(r.u8());
            object.languages.push_back(std::move(code));
        }
        break;
    case ListKind::Labels:
        for (std::size_t i = 0; i < count; ++i) {
            LabelRef label;
            label.object = r.u16();
            label.stringVariable = r.u16();
            label.fontType = r.u8();
            label.graphic = r.u16();
            object.labels.push_back(label);
        }
        break;
    case ListKind::CodePlanes:
        for (std::size_t i = 0; i < count; ++i) {
            CodePlane plane;
            plane.plane = r.u8();
            const std::size_t ranges = r.u8();
            for (std::size_t j = 0; j < ranges; ++j) {
                CharacterRange range;
                range.first = r.u16();
                range.last = r.u16();
                plane.ranges.push_back(range);
            }
            object.codePlanes.push_back(std::move(plane));
        }
        break;
    }
}

} // namespace

IopDecodeError::IopDecodeError(const std::string& message, std::size_t offset)
    : std::runtime_error(message + " (at byte " + std::to_string(offset) + ")")
    , m_offset(offset)
{
}

IopEncodeError::IopEncodeError(const std::string& message, ObjectId object)
    : std::runtime_error("Object " + std::to_string(object) + ": " + message)
    , m_object(object)
{
}

std::vector<std::uint8_t> encodeObject(const VtObject& object)
{
    const ObjectSchema& schema = object.schema();
    Writer w(object);

    if (object.attributes.size() != schema.attributes.size()) {
        w.fail("Attribute count does not match the object schema");
    }

    w.u16(object.id);
    w.u8(static_cast<std::uint8_t>(object.type));

    for (const RecordItem& item : schema.layout) {
        switch (item.kind) {
        case RecordItem::Kind::Attribute:
            writeAttribute(w, schema.attributes[item.attribute], object.attributes[item.attribute]);
            break;
        case RecordItem::Kind::Count: {
            const std::size_t count = countFieldValue(object, item.list);
            if (count > maxForSize(item.countSize)) {
                w.fail("Too many entries in " + std::string(listKindName(item.list)) + " (" +
                       std::to_string(count) + ")");
            }
            w.unsignedValue(count, item.countSize);
            break;
        }
        case RecordItem::Kind::List:
            writeList(w, object, item.list);
            break;
        }
    }
    return w.take();
}

std::vector<std::uint8_t> encodePool(const ObjectPool& pool)
{
    std::vector<std::uint8_t> bytes;
    for (const VtObject& object : pool.objects()) {
        const std::vector<std::uint8_t> record = encodeObject(object);
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    return bytes;
}

VtObject decodeObject(std::span<const std::uint8_t> bytes, std::size_t& offset)
{
    Reader r(bytes, offset);
    const ObjectId id = r.u16();
    const std::uint8_t rawType = r.u8();

    const ObjectSchema* schema = findSchema(rawType);
    if (schema == nullptr) {
        r.fail("Object " + std::to_string(id) + " has unsupported object type " + std::to_string(rawType));
    }

    VtObject object;
    object.id = id;
    object.type = schema->type;
    object.attributes.resize(schema->attributes.size());

    std::array<std::size_t, kListKindCount> counts{};
    for (const RecordItem& item : schema->layout) {
        switch (item.kind) {
        case RecordItem::Kind::Attribute:
            object.attributes[item.attribute] = readAttribute(r, schema->attributes[item.attribute]);
            break;
        case RecordItem::Kind::Count:
            counts[static_cast<std::size_t>(item.list)] = r.unsignedValue(item.countSize);
            break;
        case RecordItem::Kind::List:
            readList(r, object, item.list, counts[static_cast<std::size_t>(item.list)]);
            break;
        }
    }

    offset = r.offset();
    return object;
}

ObjectPool decodePool(std::span<const std::uint8_t> bytes, VtVersion version)
{
    ObjectPool pool(version);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t start = offset;
        VtObject object = decodeObject(bytes, offset);
        if (pool.contains(object.id)) {
            throw IopDecodeError("Duplicate object ID " + std::to_string(object.id), start);
        }
        if (object.id == kNullObjectId) {
            throw IopDecodeError("Object uses the reserved NULL Object ID 65535", start);
        }
        pool.add(std::move(object));
    }
    return pool;
}

} // namespace vt
