#pragma once

#include "vtcore/Types.h"
#include "vtcore/VtObject.h"

#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vt {

// Ordered collection of VT objects with unique IDs. The order is kept so that a pool read from
// an .iop file is written back in the same order.
//
// References returned by add()/create() stay valid only until the next insertion or removal.
class ObjectPool {
public:
    explicit ObjectPool(VtVersion version = VtVersion::V4);

    VtVersion version() const { return m_version; }
    void setVersion(VtVersion version) { m_version = version; }

    std::size_t size() const { return m_objects.size(); }
    bool empty() const { return m_objects.empty(); }

    std::span<const VtObject> objects() const { return m_objects; }
    std::span<VtObject> objects() { return m_objects; }

    bool contains(ObjectId id) const { return m_index.count(id) != 0; }
    const VtObject* find(ObjectId id) const;
    VtObject* find(ObjectId id);
    const VtObject* findByName(std::string_view name) const;
    const VtObject* workingSet() const;
    std::optional<std::size_t> indexOf(ObjectId id) const;

    // Throws std::invalid_argument if the ID is NULL or already used.
    VtObject& add(VtObject object);
    VtObject& insert(std::size_t index, VtObject object); // index is clamped to the pool size

    // ID for a new object of the given type:
    // Working Set -> 0, Macro -> lowest free ID in 0..255 (VT version 4 limit), others -> from 256.
    ObjectId suggestId(ObjectType type) const;

    // Creates an object with default attributes and the ID from suggestId().
    VtObject& create(ObjectType type, std::string name = {});

    bool remove(ObjectId id);

    // Lowest unused ID >= start, or NULL if the ID space is exhausted.
    ObjectId nextFreeId(ObjectId start = 0) const;

private:
    void rebuildIndex();

    VtVersion m_version;
    std::vector<VtObject> m_objects;
    std::unordered_map<ObjectId, std::size_t> m_index;
};

} // namespace vt
