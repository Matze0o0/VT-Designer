#pragma once

#include "vtcore/ObjectPool.h"

#include <functional>
#include <vector>

namespace vt {

// Visits every object ID stored in an object: reference attributes, children, references,
// macro references and label entries. The non-const overload allows rewriting the IDs.
void forEachReference(const VtObject& object, const std::function<void(ObjectId)>& visit);
void forEachReference(VtObject& object, const std::function<void(ObjectId&)>& visit);

// Objects that reference target in any way, in pool order.
std::vector<ObjectId> usersOf(const ObjectPool& pool, ObjectId target);

// Removes every reference to target from object. List entries are dropped, except in Soft Key Masks
// and Input/Output Lists where a NULL placeholder keeps the positions of the remaining entries.
// Reference attributes are set to NULL. Returns true if the object changed.
bool stripReferences(VtObject& object, ObjectId target);

// Objects directly contained in object (children, and the references of containment lists).
std::vector<ObjectId> containedObjects(const VtObject& object);

// The root and every object reachable from it through containment, each listed once.
std::vector<ObjectId> containedSubtree(const ObjectPool& pool, ObjectId root);

// Copies root and its contained subtree with fresh IDs. References from the copies to objects outside
// the subtree (fonts, variables, macros, ...) are kept. Names get a unique "_copy" suffix.
// The first element of the result is the copy of root.
std::vector<VtObject> duplicateSubtree(const ObjectPool& pool, ObjectId root);

} // namespace vt
