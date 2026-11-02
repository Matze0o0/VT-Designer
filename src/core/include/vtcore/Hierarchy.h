#pragma once

#include "vtcore/Schema.h"
#include "vtcore/Types.h"

#include <optional>

namespace vt {

// Containment rules from ISO 11783-6, Table A.2 (Allowed hierarchical relationships of objects).

// The list of a parent that holds contained objects (Children or References), if the parent can
// contain objects at all.
std::optional<ListKind> containmentList(ObjectType parent);

// Minimum VT version in which `child` may be contained in `parent`; 0 if never allowed.
// The result can be 6 for relationships introduced with VT version 6.
int minVersionForChild(ObjectType parent, ObjectType child);

// Minimum VT version in which an object of this type may be labelled by an Object Label Reference
// List; 0 if never allowed.
int minVersionForLabel(ObjectType labelled);

// Minimum VT version in which an object of this type may serve as Object Label graphic
// representation (also used for Window Mask and Key Group icons); 0 if never allowed.
int minVersionForLabelGraphic(ObjectType graphic);

} // namespace vt
