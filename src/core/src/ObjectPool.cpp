#include "vtcore/ObjectPool.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace vt {

namespace {

constexpr ObjectId kFirstMacroId = 0;
constexpr ObjectId kLastEightBitMacroId = 255;
constexpr ObjectId kFirstGeneralId = 256;

} // namespace

ObjectPool::ObjectPool(VtVersion version)
    : m_version(version)
{
}

const VtObject* ObjectPool::find(ObjectId id) const
{
    const auto it = m_index.find(id);
    return it == m_index.end() ? nullptr : &m_objects[it->second];
}

VtObject* ObjectPool::find(ObjectId id)
{
    const auto it = m_index.find(id);
    return it == m_index.end() ? nullptr : &m_objects[it->second];
}

const VtObject* ObjectPool::findByName(std::string_view name) const
{
    for (const VtObject& object : m_objects) {
        if (!name.empty() && object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

const VtObject* ObjectPool::workingSet() const
{
    for (const VtObject& object : m_objects) {
        if (object.type == ObjectType::WorkingSet) {
            return &object;
        }
    }
    return nullptr;
}

std::optional<std::size_t> ObjectPool::indexOf(ObjectId id) const
{
    const auto it = m_index.find(id);
    return it == m_index.end() ? std::nullopt : std::optional<std::size_t>(it->second);
}

VtObject& ObjectPool::add(VtObject object)
{
    return insert(m_objects.size(), std::move(object));
}

VtObject& ObjectPool::insert(std::size_t index, VtObject object)
{
    if (object.id == kNullObjectId) {
        throw std::invalid_argument("Object ID 65535 is reserved as NULL Object ID");
    }
    if (contains(object.id)) {
        throw std::invalid_argument("Object ID " + std::to_string(object.id) + " is already in use");
    }
    index = std::min(index, m_objects.size());
    m_objects.insert(m_objects.begin() + static_cast<std::ptrdiff_t>(index), std::move(object));
    if (index + 1 == m_objects.size()) {
        m_index.emplace(m_objects.back().id, index);
    } else {
        rebuildIndex();
    }
    return m_objects[index];
}

ObjectId ObjectPool::suggestId(ObjectType type) const
{
    if (type == ObjectType::WorkingSet && !contains(0)) {
        return 0;
    }
    if (type == ObjectType::Macro) {
        const ObjectId candidate = nextFreeId(kFirstMacroId);
        if (candidate <= kLastEightBitMacroId) {
            return candidate;
        }
    }
    return nextFreeId(kFirstGeneralId);
}

VtObject& ObjectPool::create(ObjectType type, std::string name)
{
    const ObjectId id = suggestId(type);
    if (id == kNullObjectId) {
        throw std::length_error("No free object ID left in the pool");
    }
    return add(VtObject::create(id, type, std::move(name)));
}

bool ObjectPool::remove(ObjectId id)
{
    const auto it = m_index.find(id);
    if (it == m_index.end()) {
        return false;
    }
    m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(it->second));
    rebuildIndex();
    return true;
}

ObjectId ObjectPool::nextFreeId(ObjectId start) const
{
    for (std::uint32_t id = start; id < kNullObjectId; ++id) {
        if (!contains(static_cast<ObjectId>(id))) {
            return static_cast<ObjectId>(id);
        }
    }
    return kNullObjectId;
}

void ObjectPool::rebuildIndex()
{
    m_index.clear();
    for (std::size_t i = 0; i < m_objects.size(); ++i) {
        m_index.emplace(m_objects[i].id, i);
    }
}

} // namespace vt
