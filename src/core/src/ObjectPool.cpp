#include "vtcore/ObjectPool.h"

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

VtObject& ObjectPool::add(VtObject object)
{
    if (object.id == kNullObjectId) {
        throw std::invalid_argument("Object ID 65535 is reserved as NULL Object ID");
    }
    if (contains(object.id)) {
        throw std::invalid_argument("Object ID " + std::to_string(object.id) + " is already in use");
    }
    m_index.emplace(object.id, m_objects.size());
    m_objects.push_back(std::move(object));
    return m_objects.back();
}

VtObject& ObjectPool::create(ObjectType type, std::string name)
{
    ObjectId id = kNullObjectId;
    if (type == ObjectType::WorkingSet && !contains(0)) {
        id = 0;
    } else if (type == ObjectType::Macro) {
        const ObjectId candidate = nextFreeId(kFirstMacroId);
        id = candidate <= kLastEightBitMacroId ? candidate : nextFreeId(kFirstGeneralId);
    } else {
        id = nextFreeId(kFirstGeneralId);
    }
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
