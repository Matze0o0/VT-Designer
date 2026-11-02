#pragma once

#include "vtcore/Types.h"

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace ui {

// Drawn icon for an object type; the accent colour shows the category (input, output, shape, ...).
QIcon objectIcon(vt::ObjectType type);
QPixmap objectPixmap(vt::ObjectType type, int size, qreal devicePixelRatio = 1.0);
QColor categoryColour(vt::ObjectType type);

// One sentence describing what the object is for (translated).
QString objectDescription(vt::ObjectType type);

} // namespace ui
