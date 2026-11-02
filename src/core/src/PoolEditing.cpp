#include "vtcore/PoolEditing.h"

#include "vtcore/Hierarchy.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace vt {

namespace {

bool keepsPlaceholders(ObjectType type)
{
    return type == ObjectType::SoftKeyMask || type == ObjectType::InputList || type == ObjectType::OutputList;
}

} // namespace

void forEachReference(const VtObject& object, const std::function<void(ObjectId)>& visit)
{
    VtObject copy = object;
    forEachReference(copy, [&visit](ObjectId& id) { visit(id); });
}

void forEachReference(VtObject& object, const std::function<void(ObjectId&)>& visit)
{
    const ObjectSchema& schema = object.schema();
    for (std::size_t i = 0; i < schema.attributes.size() && i < object.attributes.size(); ++i) {
        if (schema.attributes[i].type != FieldType::ObjectRef) {
            continue;
        }
        const double value = object.attributes[i];
        if (value < 0 || value > kNullObjectId || value != std::floor(value)) {
            continue;
        }
        auto id = static_cast<ObjectId>(value);
        visit(id);
        object.attributes[i] = id;
    }
    for (ChildRef& child : object.children) {
        visit(child.id);
    }
    for (ObjectId& id : object.references) {
        visit(id);
    }
    for (MacroRef& ref : object.macros) {
        visit(ref.macro);
    }
    for (LabelRef& label : object.labels) {
        visit(label.object);
        visit(label.stringVariable);
        visit(label.graphic);
    }
}

std::vector<ObjectId> usersOf(const ObjectPool& pool, ObjectId target)
{
    std::vector<ObjectId> users;
    if (target == kNullObjectId) {
        return users;
    }
    for (const VtObject& object : pool.objects()) {
        bool uses = false;
        forEachReference(object, [&](ObjectId id) { uses = uses || id == target; });
        if (uses) {
            users.push_back(object.id);
        }
    }
    return users;
}

bool stripReferences(VtObject& object, ObjectId target)
{
    if (target == kNullObjectId) {
        return false;
    }
    bool changed = false;

    const ObjectSchema& schema = object.schema();
    for (std::size_t i = 0; i < schema.attributes.size() && i < object.attributes.size(); ++i) {
        if (schema.attributes[i].type == FieldType::ObjectRef && object.attributes[i] == target) {
            object.attributes[i] = kNullObjectId;
            changed = true;
        }
    }

    const auto childEnd = std::remove_if(object.children.begin(), object.children.end(),
                                         [target](const ChildRef& c) { return c.id == target; });
    changed = changed || childEnd != object.children.end();
    object.children.erase(childEnd, object.children.end());

    if (keepsPlaceholders(object.type)) {
        for (ObjectId& id : object.references) {
            if (id == target) {
                id = kNullObjectId;
                changed = true;
            }
        }
    } else {
        const auto refEnd = std::remove(object.references.begin(), object.references.end(), target);
        changed = changed || refEnd != object.references.end();
        object.references.erase(refEnd, object.references.end());
    }

    const auto macroEnd = std::remove_if(object.macros.begin(), object.macros.end(),
                                         [target](const MacroRef& m) { return m.macro == target; });
    changed = changed || macroEnd != object.macros.end();
    object.macros.erase(macroEnd, object.macros.end());

    const auto labelEnd = std::remove_if(object.labels.begin(), object.labels.end(),
                                         [target](const LabelRef& l) { return l.object == target; });
    changed = changed || labelEnd != object.labels.end();
    object.labels.erase(labelEnd, object.labels.end());
    for (LabelRef& label : object.labels) {
        if (label.stringVariable == target) {
            label.stringVariable = kNullObjectId;
            changed = true;
        }
        if (label.graphic == target) {
            label.graphic = kNullObjectId;
            changed = true;
        }
    }
    return changed;
}

std::vector<ObjectId> containedObjects(const VtObject& object)
{
    std::vector<ObjectId> ids;
    for (const ChildRef& child : object.children) {
        ids.push_back(child.id);
    }
    if (containmentList(object.type) == ListKind::References) {
        for (ObjectId id : object.references) {
            if (id != kNullObjectId) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

std::vector<ObjectId> containedSubtree(const ObjectPool& pool, ObjectId root)
{
    std::vector<ObjectId> order;
    std::set<ObjectId> seen;
    std::vector<ObjectId> stack = {root};
    while (!stack.empty()) {
        const ObjectId id = stack.back();
        stack.pop_back();
        const VtObject* object = pool.find(id);
        if (object == nullptr || !seen.insert(id).second) {
            continue;
        }
        order.push_back(id);
        const std::vector<ObjectId> contained = containedObjects(*object);
        stack.insert(stack.end(), contained.rbegin(), contained.rend());
    }
    return order;
}

std::vector<VtObject> duplicateSubtree(const ObjectPool& pool, ObjectId root)
{
    const std::vector<ObjectId> originals = containedSubtree(pool, root);

    std::set<ObjectId> taken;
    std::set<std::string> names;
    for (const VtObject& object : pool.objects()) {
        taken.insert(object.id);
        if (!object.name.empty()) {
            names.insert(object.name);
        }
    }

    std::map<ObjectId, ObjectId> newIds;
    for (ObjectId id : originals) {
        const VtObject* object = pool.find(id);
        ObjectId candidate = pool.suggestId(object->type);
        while (taken.count(candidate) != 0 && candidate != kNullObjectId) {
            ++candidate;
        }
        if (candidate == kNullObjectId) {
            throw std::length_error("No free object ID left in the pool");
        }
        taken.insert(candidate);
        newIds[id] = candidate;
    }

    std::vector<VtObject> copies;
    for (ObjectId id : originals) {
        VtObject copy = *pool.find(id);
        copy.id = newIds[id];
        if (!copy.name.empty()) {
            std::string base = copy.name + "_copy";
            std::string name = base;
            for (int n = 2; names.count(name) != 0; ++n) {
                name = base + std::to_string(n);
            }
            names.insert(name);
            copy.name = name;
        }
        forEachReference(copy, [&newIds](ObjectId& ref) {
            const auto it = newIds.find(ref);
            if (it != newIds.end()) {
                ref = it->second;
            }
        });
        copies.push_back(std::move(copy));
    }
    return copies;
}

} // namespace vt
