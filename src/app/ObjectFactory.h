#pragma once

#include "vtcore/ObjectPool.h"
#include "vtrender/VtRenderer.h"

#include <QPoint>

#include <optional>
#include <set>
#include <vector>

class QWidget;

namespace ui {

// Creates a new object with useful defaults. Mandatory references (font, line attributes, active mask,
// designator, ...) point to existing objects where possible; otherwise helper objects are created and
// appended to `extra`. IDs already used by `extra` are avoided.
vt::VtObject createObject(const vt::ObjectPool& pool, vt::ObjectType type, std::vector<vt::VtObject>& extra);

// Lowest suitable free ID that is neither in the pool nor in `taken`.
vt::ObjectId allocateId(const vt::ObjectPool& pool, vt::ObjectType type, const std::set<vt::ObjectId>& taken);

// Whether an object of `child` type can be placed into `parent` via its children list.
bool canPlaceChild(vt::ObjectType parent, vt::ObjectType child, vt::VtVersion version);

// The deepest placement under `point` that may contain an object of `type` in its children list.
std::optional<vtr::Placement> dropTarget(const vt::ObjectPool& pool, const std::vector<vtr::Placement>& layout,
                                         QPoint point, vt::ObjectType type);

// Asks for an image file and conversion options and stores the image in the Picture Graphic object.
// Returns false if the user cancelled or the file could not be read.
bool importPicture(QWidget* parent, vt::VtObject& picture, vt::VtVersion version);

// Writes the Picture Graphic object as PNG.
bool exportPicture(QWidget* parent, const vt::VtObject& picture);

} // namespace ui
