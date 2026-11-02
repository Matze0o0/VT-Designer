#include "vtcore/ProjectFile.h"

#include "vtcore/Schema.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

namespace vt {

namespace {

using json = nlohmann::ordered_json;

constexpr const char* kFormatId = "vt-designer-project";
constexpr int kFormatVersion = 1;

// ---------------------------------------------------------------------------------------------
// Text encoding helpers

bool isTextData(ObjectType type)
{
    switch (type) {
    case ObjectType::InputString:
    case ObjectType::OutputString:
    case ObjectType::StringVariable:
    case ObjectType::InputAttributes: return true;
    default: return false;
    }
}

bool isWideString(const std::vector<std::uint8_t>& data)
{
    return data.size() >= 2 && data.size() % 2 == 0 && data[0] == 0xFF && data[1] == 0xFE;
}

void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::vector<std::uint32_t> decodeUtf8(std::string_view text)
{
    std::vector<std::uint32_t> codepoints;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        int extra = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            throw ProjectError("Invalid UTF-8 in text value");
        }
        if (i + static_cast<std::size_t>(extra) >= text.size()) {
            throw ProjectError("Truncated UTF-8 sequence in text value");
        }
        for (int k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80) {
                throw ProjectError("Invalid UTF-8 in text value");
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        codepoints.push_back(cp);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return codepoints;
}

// 8-bit strings: each byte is kept as the code point of the same value, which is lossless whatever
// font type (character set) the string is displayed with.
std::string bytesToLatin1Text(const std::vector<std::uint8_t>& data)
{
    std::string text;
    for (std::uint8_t byte : data) {
        appendUtf8(text, byte);
    }
    return text;
}

std::vector<std::uint8_t> latin1TextToBytes(std::string_view text)
{
    std::vector<std::uint8_t> data;
    for (std::uint32_t cp : decodeUtf8(text)) {
        if (cp > 0xFF) {
            throw ProjectError("Character U+" + std::to_string(cp) + " does not fit an 8-bit string; use wideText");
        }
        data.push_back(static_cast<std::uint8_t>(cp));
    }
    return data;
}

std::string wideBytesToText(const std::vector<std::uint8_t>& data)
{
    std::string text;
    for (std::size_t i = 2; i + 1 < data.size(); i += 2) {
        std::uint32_t unit = data[i] | (data[i + 1] << 8);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < data.size()) {
            const std::uint32_t low = data[i + 2] | (data[i + 3] << 8);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(text, unit);
    }
    return text;
}

std::vector<std::uint8_t> wideTextToBytes(std::string_view text)
{
    std::vector<std::uint8_t> data = {0xFF, 0xFE};
    auto put = [&data](std::uint32_t unit) {
        data.push_back(static_cast<std::uint8_t>(unit & 0xFF));
        data.push_back(static_cast<std::uint8_t>(unit >> 8));
    };
    for (std::uint32_t cp : decodeUtf8(text)) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put(0xD800 + (cp >> 10));
            put(0xDC00 + (cp & 0x3FF));
        } else {
            put(cp);
        }
    }
    return data;
}

// ---------------------------------------------------------------------------------------------
// Base64

constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<std::uint8_t>& data)
{
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::uint32_t chunk = (data[i] << 16) | ((i + 1 < data.size() ? data[i + 1] : 0) << 8) |
                                    (i + 2 < data.size() ? data[i + 2] : 0);
        out += kBase64Alphabet[(chunk >> 18) & 0x3F];
        out += kBase64Alphabet[(chunk >> 12) & 0x3F];
        out += i + 1 < data.size() ? kBase64Alphabet[(chunk >> 6) & 0x3F] : '=';
        out += i + 2 < data.size() ? kBase64Alphabet[chunk & 0x3F] : '=';
    }
    return out;
}

std::vector<std::uint8_t> base64Decode(std::string_view text)
{
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
            continue;
        }
        const int v = value(c);
        if (v < 0) {
            throw ProjectError("Invalid base64 data");
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Attribute values

// Float attributes are written in the shortest form that reads back to the same 32-bit float.
json attributeToJson(const AttributeDef& def, double value)
{
    if (def.type == FieldType::Float32) {
        const auto f = static_cast<float>(value);
        char buffer[32];
        const auto written = std::to_chars(buffer, buffer + sizeof(buffer), f);
        double shortest = f;
        std::from_chars(buffer, written.ptr, shortest);
        return json(shortest);
    }
    return json(static_cast<long long>(std::llround(value)));
}

std::string_view headerStyleName(HeaderStyle style)
{
    switch (style) {
    case HeaderStyle::Define: return "define";
    case HeaderStyle::Enum: return "enum";
    case HeaderStyle::Constexpr: return "constexpr";
    }
    return "define";
}

HeaderStyle headerStyleFromName(const std::string& name)
{
    if (name == "define") return HeaderStyle::Define;
    if (name == "enum") return HeaderStyle::Enum;
    if (name == "constexpr") return HeaderStyle::Constexpr;
    throw ProjectError("Unknown header style '" + name + "'");
}

// ---------------------------------------------------------------------------------------------
// Objects

json objectToJson(const VtObject& object)
{
    const ObjectSchema& schema = object.schema();
    json j;
    j["id"] = object.id;
    j["type"] = schema.name;
    if (!object.name.empty()) {
        j["name"] = object.name;
    }

    json attributes = json::object();
    for (std::size_t i = 0; i < schema.attributes.size(); ++i) {
        attributes[std::string(schema.attributes[i].name)] = attributeToJson(schema.attributes[i], object.attributes[i]);
    }
    if (!attributes.empty()) {
        j["attributes"] = attributes;
    }

    if (!object.children.empty()) {
        json list = json::array();
        for (const ChildRef& child : object.children) {
            list.push_back({{"id", child.id}, {"x", child.x}, {"y", child.y}});
        }
        j["children"] = list;
    }
    if (!object.references.empty()) {
        j["references"] = object.references;
    }
    if (!object.macros.empty()) {
        json list = json::array();
        for (const MacroRef& ref : object.macros) {
            list.push_back({{"event", ref.event}, {"macro", ref.macro}});
        }
        j["macros"] = list;
    }
    if (!object.points.empty()) {
        json list = json::array();
        for (const Point& point : object.points) {
            list.push_back({{"x", point.x}, {"y", point.y}});
        }
        j["points"] = list;
    }
    if (schema.hasList(ListKind::Data)) {
        if (isTextData(object.type)) {
            if (isWideString(object.data)) {
                j["wideText"] = wideBytesToText(object.data);
            } else {
                j["text"] = bytesToLatin1Text(object.data);
            }
        } else {
            j["data"] = base64Encode(object.data);
        }
    }
    if (!object.languages.empty()) {
        j["languages"] = object.languages;
    }
    if (!object.labels.empty()) {
        json list = json::array();
        for (const LabelRef& label : object.labels) {
            list.push_back({{"object", label.object},
                            {"stringVariable", label.stringVariable},
                            {"fontType", label.fontType},
                            {"graphic", label.graphic}});
        }
        j["labels"] = list;
    }
    if (!object.codePlanes.empty()) {
        json list = json::array();
        for (const CodePlane& plane : object.codePlanes) {
            json ranges = json::array();
            for (const CharacterRange& range : plane.ranges) {
                ranges.push_back({range.first, range.last});
            }
            list.push_back({{"plane", plane.plane}, {"ranges", ranges}});
        }
        j["codePlanes"] = list;
    }
    return j;
}

template <typename T>
T checkedInteger(const json& value, const std::string& what)
{
    if (!value.is_number_integer()) {
        throw ProjectError(what + " is not a whole number");
    }
    const auto n = value.get<long long>();
    if (n < static_cast<long long>(std::numeric_limits<T>::min()) ||
        n > static_cast<long long>(std::numeric_limits<T>::max())) {
        throw ProjectError(what + " is out of range");
    }
    return static_cast<T>(n);
}

template <typename T>
T number(const json& j, const char* key, const std::string& context)
{
    if (!j.is_object() || !j.contains(key)) {
        throw ProjectError(context + ": '" + key + "' is missing");
    }
    return checkedInteger<T>(j[key], context + ": '" + key + "'");
}

VtObject objectFromJson(const json& j)
{
    if (!j.is_object()) {
        throw ProjectError("Object entry is not a JSON object");
    }
    const std::string typeName = j.value("type", "");
    const std::optional<ObjectType> type = objectTypeFromName(typeName);
    if (!type) {
        throw ProjectError("Unknown object type '" + typeName + "'");
    }
    const auto id = number<ObjectId>(j, "id", typeName);
    const std::string context = typeName + " " + std::to_string(id);

    VtObject object = VtObject::create(id, *type, j.value("name", ""));
    const ObjectSchema& schema = object.schema();

    if (j.contains("attributes")) {
        for (const auto& [key, value] : j["attributes"].items()) {
            const int index = schema.attributeIndex(key);
            if (index < 0) {
                throw ProjectError(context + ": unknown attribute '" + key + "'");
            }
            if (!value.is_number()) {
                throw ProjectError(context + ": attribute '" + key + "' is not a number");
            }
            object.attributes[static_cast<std::size_t>(index)] = value.get<double>();
        }
    }

    for (const json& child : j.value("children", json::array())) {
        object.children.push_back({number<ObjectId>(child, "id", context), number<std::int16_t>(child, "x", context),
                                    number<std::int16_t>(child, "y", context)});
    }
    for (const json& ref : j.value("references", json::array())) {
        object.references.push_back(checkedInteger<ObjectId>(ref, context + ": reference"));
    }
    for (const json& ref : j.value("macros", json::array())) {
        object.macros.push_back({number<std::uint8_t>(ref, "event", context), number<ObjectId>(ref, "macro", context)});
    }
    for (const json& point : j.value("points", json::array())) {
        object.points.push_back({number<std::uint16_t>(point, "x", context), number<std::uint16_t>(point, "y", context)});
    }
    if (j.contains("wideText")) {
        object.data = wideTextToBytes(j["wideText"].get<std::string>());
    } else if (j.contains("text")) {
        object.data = latin1TextToBytes(j["text"].get<std::string>());
    } else if (j.contains("data")) {
        object.data = base64Decode(j["data"].get<std::string>());
    }
    for (const json& code : j.value("languages", json::array())) {
        object.languages.push_back(code.get<std::string>());
    }
    for (const json& label : j.value("labels", json::array())) {
        object.labels.push_back({number<ObjectId>(label, "object", context),
                                 number<ObjectId>(label, "stringVariable", context),
                                 number<std::uint8_t>(label, "fontType", context),
                                 number<ObjectId>(label, "graphic", context)});
    }
    for (const json& plane : j.value("codePlanes", json::array())) {
        CodePlane codePlane;
        codePlane.plane = number<std::uint8_t>(plane, "plane", context);
        for (const json& range : plane.value("ranges", json::array())) {
            if (!range.is_array() || range.size() != 2) {
                throw ProjectError(context + ": character range must be [first, last]");
            }
            codePlane.ranges.push_back({checkedInteger<std::uint16_t>(range[0], context + ": range start"),
                                        checkedInteger<std::uint16_t>(range[1], context + ": range end")});
        }
        object.codePlanes.push_back(std::move(codePlane));
    }
    return object;
}

} // namespace

std::string projectToJson(const Project& project)
{
    json j;
    j["format"] = kFormatId;
    j["formatVersion"] = kFormatVersion;
    j["vtVersion"] = toInt(project.pool.version());
    j["export"] = {{"poolName", project.exportOptions.poolName},
                   {"symbolPrefix", project.exportOptions.symbolPrefix},
                   {"headerStyle", headerStyleName(project.exportOptions.headerStyle)}};
    json objects = json::array();
    for (const VtObject& object : project.pool.objects()) {
        objects.push_back(objectToJson(object));
    }
    j["objects"] = objects;
    return j.dump(2) + "\n";
}

Project projectFromJson(std::string_view text)
{
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        throw ProjectError(std::string("Project file is not valid JSON: ") + e.what());
    }

    try {
        if (j.value("format", "") != kFormatId) {
            throw ProjectError("Not a VT-Designer project file");
        }
        if (j.value("formatVersion", 0) > kFormatVersion) {
            throw ProjectError("Project file was written by a newer VT-Designer version");
        }
        const std::optional<VtVersion> version = vtVersionFromInt(j.value("vtVersion", 0));
        if (!version) {
            throw ProjectError("Unsupported VT version " + std::to_string(j.value("vtVersion", 0)));
        }

        Project project{ObjectPool(*version), {}};
        if (j.contains("export")) {
            const json& e = j["export"];
            project.exportOptions.poolName = e.value("poolName", project.exportOptions.poolName);
            project.exportOptions.symbolPrefix = e.value("symbolPrefix", "");
            project.exportOptions.headerStyle = headerStyleFromName(e.value("headerStyle", "define"));
        }
        for (const json& object : j.value("objects", json::array())) {
            try {
                project.pool.add(objectFromJson(object));
            } catch (const std::invalid_argument& e) {
                throw ProjectError(e.what());
            }
        }
        return project;
    } catch (const json::exception& e) {
        throw ProjectError(std::string("Malformed project file: ") + e.what());
    }
}

void saveProject(const Project& project, const std::filesystem::path& path)
{
    writeTextFile(path, projectToJson(project));
}

Project loadProject(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ProjectError("Cannot open " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return projectFromJson(text.str());
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBinaryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace vt
