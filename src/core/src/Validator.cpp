#include "vtcore/Validator.h"

#include "vtcore/Hierarchy.h"
#include "vtcore/PictureCodec.h"
#include "vtcore/Schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vt {

namespace {

// ISO 11783-6, 4.6.1.3: VT version 5 and later shall support a hierarchy depth of at least 30.
constexpr int kGuaranteedHierarchyDepth = 30;
constexpr std::size_t kMaxSoftKeys = 64;

// Objects that are activated or referenced by the Working Set at runtime rather than by
// other objects, so being unreferenced in the pool is normal for them.
bool isStandaloneType(ObjectType type)
{
    switch (type) {
    case ObjectType::WorkingSet:
    case ObjectType::DataMask:
    case ObjectType::AlarmMask:
    case ObjectType::SoftKeyMask:
    case ObjectType::WindowMask:
    case ObjectType::KeyGroup:
    case ObjectType::Macro:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1:
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2:
    case ObjectType::ObjectLabelReferenceList:
    case ObjectType::ExternalObjectDefinition:
    case ObjectType::ExternalReferenceName:
    case ObjectType::ColourMap: return true;
    default: return false;
    }
}

bool isValidIdentifier(const std::string& name)
{
    if (name.empty() || (std::isdigit(static_cast<unsigned char>(name[0])) != 0)) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

bool isLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::uint64_t maxForCountSize(int size)
{
    return size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
}

// Object reference held by a pointer object, NULL if the attribute list is malformed.
ObjectId pointerTarget(const VtObject& object)
{
    std::string_view attribute;
    if (object.type == ObjectType::ObjectPointer) {
        attribute = "value";
    } else if (object.type == ObjectType::ExternalObjectPointer) {
        attribute = "defaultObjectId";
    } else {
        return kNullObjectId;
    }
    const int index = object.schema().attributeIndex(attribute);
    if (index < 0 || static_cast<std::size_t>(index) >= object.attributes.size()) {
        return kNullObjectId;
    }
    const double value = object.attributes[static_cast<std::size_t>(index)];
    return value >= 0 && value <= kNullObjectId ? static_cast<ObjectId>(value) : kNullObjectId;
}

class Validation {
public:
    explicit Validation(const ObjectPool& pool)
        : m_pool(pool)
        , m_version(toInt(pool.version()))
    {
    }

    std::vector<Diagnostic> run()
    {
        checkWorkingSet();
        for (const VtObject& object : m_pool.objects()) {
            checkObject(object);
        }
        checkNames();
        checkUnreferenced();
        checkCycles();
        return std::move(m_diagnostics);
    }

private:
    void error(ObjectId id, std::string message) { m_diagnostics.push_back({Severity::Error, id, std::move(message)}); }

    void warning(ObjectId id, std::string message)
    {
        m_diagnostics.push_back({Severity::Warning, id, std::move(message)});
    }

    std::string describe(ObjectId id) const
    {
        const VtObject* object = m_pool.find(id);
        if (object == nullptr) {
            return "object " + std::to_string(id);
        }
        std::string text = std::string(object->schema().label) + " " + std::to_string(id);
        if (!object->name.empty()) {
            text += " (" + object->name + ")";
        }
        return text;
    }

    void checkWorkingSet()
    {
        const auto count = std::count_if(m_pool.objects().begin(), m_pool.objects().end(),
                                         [](const VtObject& o) { return o.type == ObjectType::WorkingSet; });
        if (count == 0) {
            error(kNullObjectId, "The pool has no Working Set object");
        } else if (count > 1) {
            error(kNullObjectId, "The pool has more than one Working Set object");
        }
    }

    void checkObject(const VtObject& object)
    {
        const ObjectSchema& schema = object.schema();
        if (m_version < toInt(schema.minVersion)) {
            error(object.id, describe(object.id) + " requires VT version " + std::to_string(toInt(schema.minVersion)));
        }
        if (object.attributes.size() != schema.attributes.size()) {
            error(object.id, describe(object.id) + " has an inconsistent attribute list");
            return;
        }

        checkAttributes(object, schema);
        checkCountLimits(object, schema);
        checkMacros(object);
        checkContainment(object);
        checkTypeSpecific(object);
    }

    void checkAttributes(const VtObject& object, const ObjectSchema& schema)
    {
        for (std::size_t i = 0; i < schema.attributes.size(); ++i) {
            const AttributeDef& def = schema.attributes[i];
            const double value = object.attributes[i];
            const std::string what = describe(object.id) + ": " + std::string(def.label);

            if (def.type == FieldType::ObjectRef) {
                checkObjectRef(object, def, value, what);
                continue;
            }
            if (def.type != FieldType::Float32 && value != std::floor(value)) {
                error(object.id, what + " must be a whole number");
            }
            if (value < def.minValue || value > def.maxValue) {
                error(object.id, what + " is " + formatNumber(value) + ", allowed range is " +
                                     formatNumber(def.minValue) + " to " + formatNumber(def.maxValue));
            }
        }
    }

    void checkObjectRef(const VtObject& object, const AttributeDef& def, double value, const std::string& what)
    {
        if (value < 0 || value > kNullObjectId || value != std::floor(value)) {
            error(object.id, what + " is not a valid object ID");
            return;
        }
        const auto id = static_cast<ObjectId>(value);
        if (id == kNullObjectId) {
            if (!def.nullAllowed) {
                error(object.id, what + " must reference an object");
            }
            return;
        }
        const VtObject* target = m_pool.find(id);
        if (target == nullptr) {
            error(object.id, what + " references missing object " + std::to_string(id));
            return;
        }
        if (!def.refTypes.empty() &&
            std::find(def.refTypes.begin(), def.refTypes.end(), target->type) == def.refTypes.end()) {
            error(object.id, what + " must not reference " + describe(id));
        }
    }

    void checkCountLimits(const VtObject& object, const ObjectSchema& schema)
    {
        for (const RecordItem& item : schema.layout) {
            if (item.kind != RecordItem::Kind::Count) {
                continue;
            }
            std::size_t count = object.listSize(item.list);
            if (item.list == ListKind::Macros) {
                count = 0;
                for (const MacroRef& ref : object.macros) {
                    count += ref.macro > 0xFF ? 2 : 1;
                }
            }
            if (count > maxForCountSize(item.countSize)) {
                error(object.id, describe(object.id) + " has too many " + std::string(listKindName(item.list)) +
                                     " entries (" + std::to_string(count) + ", maximum " +
                                     std::to_string(maxForCountSize(item.countSize)) + ")");
            }
        }
    }

    void checkMacros(const VtObject& object)
    {
        for (const MacroRef& ref : object.macros) {
            const std::string what = describe(object.id) + ": macro reference for event " + std::to_string(ref.event);
            // Pointing events (27, 28) exist from VT version 5 (Table A.3).
            const bool reserved = ref.event == 0 || ref.event == 0xFF || (ref.event > 28 && ref.event < 240) ||
                                  (ref.event >= 27 && ref.event <= 28 && m_version < 5);
            if (reserved) {
                error(object.id, what + " uses a reserved event ID");
            } else if (ref.event >= 240) {
                warning(object.id, what + " uses a proprietary event ID");
            }

            const VtObject* macro = m_pool.find(ref.macro);
            if (macro == nullptr) {
                error(object.id, what + " references missing macro " + std::to_string(ref.macro));
            } else if (macro->type != ObjectType::Macro) {
                error(object.id, what + " references " + describe(ref.macro) + ", which is not a Macro");
            }
            if (ref.macro > 0xFF && m_version < 5) {
                error(object.id, what + " uses a 16-bit macro ID, which requires VT version 5");
            }
        }
    }

    void checkContainment(const VtObject& parent)
    {
        const std::optional<ListKind> list = containmentList(parent.type);
        if (!list) {
            return;
        }

        const bool nullAllowed = parent.type == ObjectType::SoftKeyMask || parent.type == ObjectType::InputList ||
                                 parent.type == ObjectType::OutputList;
        if (*list == ListKind::Children) {
            for (const ChildRef& child : parent.children) {
                checkContained(parent, child.id, false);
            }
        } else {
            for (ObjectId id : parent.references) {
                checkContained(parent, id, nullAllowed);
            }
        }
    }

    void checkContained(const VtObject& parent, ObjectId childId, bool nullAllowed)
    {
        if (childId == kNullObjectId) {
            if (!nullAllowed) {
                error(parent.id, describe(parent.id) + " contains the NULL object ID");
            }
            return;
        }
        const VtObject* child = m_pool.find(childId);
        if (child == nullptr) {
            error(parent.id, describe(parent.id) + " contains missing object " + std::to_string(childId));
            return;
        }
        checkAllowedIn(parent, *child, describe(child->id));

        // Pointers must not smuggle in objects the parent may not contain (Table A.2, note 3).
        const ObjectId target = pointerTarget(*child);
        if (target != kNullObjectId) {
            if (const VtObject* pointed = m_pool.find(target)) {
                checkAllowedIn(parent, *pointed, describe(target) + " (via " + describe(child->id) + ")");
            }
        }
    }

    void checkAllowedIn(const VtObject& parent, const VtObject& child, const std::string& childText)
    {
        const int minVersion = minVersionForChild(parent.type, child.type);
        if (minVersion == 0) {
            error(parent.id, describe(parent.id) + " cannot contain " + childText);
        } else if (minVersion > m_version) {
            error(parent.id, describe(parent.id) + " can contain " + childText + " only from VT version " +
                                 std::to_string(minVersion));
        }
    }

    void requireExisting(const VtObject& owner, ObjectId id, const std::string& what)
    {
        if (id != kNullObjectId && !m_pool.contains(id)) {
            error(owner.id, describe(owner.id) + ": " + what + " references missing object " + std::to_string(id));
        }
    }

    void checkTypeSpecific(const VtObject& object)
    {
        const std::string self = describe(object.id);
        switch (object.type) {
        case ObjectType::WorkingSet:
            if (object.children.empty()) {
                error(object.id, self + " needs at least one object for its designator");
            }
            for (const std::string& code : object.languages) {
                if (code.size() != 2 || !isLetter(code[0]) || !isLetter(code[1])) {
                    error(object.id, self + ": language code '" + code + "' must be two letters");
                }
            }
            break;
        case ObjectType::SoftKeyMask:
            if (object.references.size() > kMaxSoftKeys) {
                error(object.id, self + " has more than " + std::to_string(kMaxSoftKeys) + " soft keys");
            }
            break;
        case ObjectType::KeyGroup:
            if (object.references.empty() || object.references.size() > 4) {
                error(object.id, self + " must contain 1 to 4 keys");
            }
            break;
        case ObjectType::OutputPolygon:
            if (object.points.size() < 3) {
                error(object.id, self + " needs at least three points");
            }
            break;
        case ObjectType::AuxiliaryFunctionType1:
        case ObjectType::AuxiliaryInputType1:
        case ObjectType::AuxiliaryFunctionType2:
        case ObjectType::AuxiliaryInputType2:
            if (object.children.empty()) {
                error(object.id, self + " needs at least one object for its designator");
            }
            break;
        case ObjectType::WindowMask:
            if (object.get("windowType") != 0 && !object.children.empty()) {
                error(object.id, self + ": only free form windows (type 0) may contain child objects");
            }
            if (object.references.size() > 2) {
                error(object.id, self + " has more than two object references");
            }
            for (ObjectId id : object.references) {
                requireExisting(object, id, "window object reference");
            }
            break;
        case ObjectType::ExternalObjectDefinition:
            for (ObjectId id : object.references) {
                requireExisting(object, id, "external object");
            }
            break;
        case ObjectType::Macro:
            if (object.id > 0xFF && m_version < 5) {
                error(object.id, self + ": macro IDs above 255 require VT version 5");
            }
            if (object.data.size() % 8 != 0) {
                warning(object.id, self + ": command data is not a multiple of 8 bytes (pad with 0xFF)");
            }
            break;
        case ObjectType::ColourMap: {
            const std::size_t n = object.data.size();
            if (n != 2 && n != 16 && n != 256) {
                error(object.id, self + " must have 2, 16 or 256 entries");
            }
            break;
        }
        case ObjectType::PictureGraphic:
            checkPicture(object);
            break;
        case ObjectType::ObjectLabelReferenceList:
            checkLabels(object);
            break;
        case ObjectType::ExtendedInputAttributes:
            if (object.codePlanes.empty() || object.codePlanes.size() > 17) {
                error(object.id, self + " must define 1 to 17 code planes");
            }
            for (const CodePlane& plane : object.codePlanes) {
                if (plane.plane > 16) {
                    error(object.id, self + ": code plane " + std::to_string(plane.plane) + " does not exist");
                }
                if (plane.ranges.empty()) {
                    error(object.id, self + ": code plane " + std::to_string(plane.plane) + " has no ranges");
                }
                for (const CharacterRange& range : plane.ranges) {
                    if (range.first > range.last) {
                        error(object.id, self + ": character range start is after its end");
                    }
                }
            }
            break;
        default:
            break;
        }
    }

    void checkPicture(const VtObject& picture)
    {
        const double format = picture.get("format");
        if (format < 0 || format > 2) {
            return; // reported by the range check
        }
        try {
            (void)pictureIndices(picture);
        } catch (const std::invalid_argument&) {
            error(picture.id, describe(picture.id) + ": bitmap data is shorter than the actual width and height require");
        }
    }

    void checkLabels(const VtObject& list)
    {
        for (const LabelRef& label : list.labels) {
            const VtObject* labelled = m_pool.find(label.object);
            if (labelled == nullptr) {
                error(list.id, describe(list.id) + " labels missing object " + std::to_string(label.object));
            } else {
                const int minVersion = minVersionForLabel(labelled->type);
                if (minVersion == 0 || minVersion > m_version) {
                    error(list.id, describe(list.id) + " cannot label " + describe(label.object) +
                                       " in VT version " + std::to_string(m_version));
                }
            }
            if (label.stringVariable != kNullObjectId) {
                const VtObject* text = m_pool.find(label.stringVariable);
                if (text == nullptr || text->type != ObjectType::StringVariable) {
                    error(list.id, describe(list.id) + ": label text must reference a String Variable");
                }
            }
            if (label.graphic != kNullObjectId) {
                const VtObject* graphic = m_pool.find(label.graphic);
                if (graphic == nullptr) {
                    error(list.id, describe(list.id) + ": label graphic references missing object " +
                                       std::to_string(label.graphic));
                } else if (minVersionForLabelGraphic(graphic->type) == 0) {
                    error(list.id, describe(list.id) + ": " + describe(label.graphic) +
                                       " cannot be used as label graphic");
                }
            }
        }
    }

    void checkNames()
    {
        std::map<std::string, ObjectId> seen;
        for (const VtObject& object : m_pool.objects()) {
            if (object.name.empty()) {
                continue;
            }
            if (!isValidIdentifier(object.name)) {
                warning(object.id, describe(object.id) + ": name is not a valid C identifier");
            }
            const auto [it, inserted] = seen.emplace(object.name, object.id);
            if (!inserted) {
                error(object.id, "Name '" + object.name + "' is used by " + describe(it->second) + " and " +
                                     describe(object.id));
            }
        }
    }

    void checkUnreferenced()
    {
        std::unordered_set<ObjectId> used;
        for (const VtObject& object : m_pool.objects()) {
            const ObjectSchema& schema = object.schema();
            for (std::size_t i = 0; i < schema.attributes.size() && i < object.attributes.size(); ++i) {
                if (schema.attributes[i].type == FieldType::ObjectRef) {
                    used.insert(static_cast<ObjectId>(object.attributes[i]));
                }
            }
            for (const ChildRef& child : object.children) {
                used.insert(child.id);
            }
            used.insert(object.references.begin(), object.references.end());
            for (const MacroRef& ref : object.macros) {
                used.insert(ref.macro);
            }
            for (const LabelRef& label : object.labels) {
                used.insert(label.object);
                used.insert(label.stringVariable);
                used.insert(label.graphic);
            }
        }
        for (const VtObject& object : m_pool.objects()) {
            if (!isStandaloneType(object.type) && used.count(object.id) == 0) {
                warning(object.id, describe(object.id) + " is not used by any other object");
            }
        }
    }

    // Detects circular containment and hierarchies deeper than the guaranteed minimum.
    void checkCycles()
    {
        std::unordered_map<ObjectId, int> state; // 0 = unvisited, 1 = in progress, 2 = done
        std::unordered_map<ObjectId, int> depth;
        std::set<ObjectId> reported;

        std::function<int(ObjectId)> visit = [&](ObjectId id) -> int {
            const VtObject* object = m_pool.find(id);
            if (object == nullptr) {
                return 0;
            }
            int& s = state[id];
            if (s == 1) {
                if (reported.insert(id).second) {
                    error(id, describe(id) + " contains itself (circular object hierarchy)");
                }
                return 0;
            }
            if (s == 2) {
                return depth[id];
            }
            s = 1;
            int deepest = 0;
            for (ObjectId next : containedIds(*object)) {
                deepest = std::max(deepest, visit(next));
            }
            state[id] = 2;
            depth[id] = deepest + 1;
            return deepest + 1;
        };

        for (const VtObject& object : m_pool.objects()) {
            if (object.type == ObjectType::DataMask || object.type == ObjectType::AlarmMask ||
                object.type == ObjectType::WindowMask || object.type == ObjectType::SoftKeyMask ||
                object.type == ObjectType::WorkingSet || object.type == ObjectType::KeyGroup) {
                const int d = visit(object.id);
                if (d > kGuaranteedHierarchyDepth) {
                    warning(object.id, describe(object.id) + " has a hierarchy depth of " + std::to_string(d) +
                                           "; VTs only guarantee " + std::to_string(kGuaranteedHierarchyDepth));
                }
            }
        }
        for (const VtObject& object : m_pool.objects()) {
            visit(object.id);
        }
    }

    std::vector<ObjectId> containedIds(const VtObject& object) const
    {
        std::vector<ObjectId> ids;
        for (const ChildRef& child : object.children) {
            ids.push_back(child.id);
        }
        if (containmentList(object.type) == ListKind::References) {
            ids.insert(ids.end(), object.references.begin(), object.references.end());
        }
        if (const ObjectId target = pointerTarget(object); target != kNullObjectId) {
            ids.push_back(target);
        }
        return ids;
    }

    static std::string formatNumber(double value)
    {
        if (value == std::floor(value) && std::abs(value) < 1e15) {
            return std::to_string(static_cast<long long>(value));
        }
        return std::to_string(value);
    }

    const ObjectPool& m_pool;
    int m_version;
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace

std::vector<Diagnostic> validatePool(const ObjectPool& pool)
{
    return Validation(pool).run();
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

} // namespace vt
