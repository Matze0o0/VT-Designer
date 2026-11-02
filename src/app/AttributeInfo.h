#pragma once

#include "vtcore/Schema.h"
#include "vtcore/VtObject.h"

#include <QIcon>
#include <QString>

#include <vector>

// Presentation details of object types and attributes for the editor UI.
namespace ui {

enum class EditorKind {
    Integer,
    Float,
    Boolean,
    Colour,
    Enum,
    Flags,
    ObjectRef,
};

struct NamedValue {
    int value;
    QString name;
};

QString typeLabel(vt::ObjectType type);
QString attributeLabel(const vt::AttributeDef& attribute);

// "dmMain (1000)" or "Data Mask 1000" for unnamed objects.
QString objectDisplayName(const vt::VtObject& object);

EditorKind editorKind(vt::ObjectType type, const vt::AttributeDef& attribute);
std::vector<NamedValue> enumValues(vt::ObjectType type, const vt::AttributeDef& attribute);
std::vector<NamedValue> flagBits(vt::ObjectType type, const vt::AttributeDef& attribute);

QString colourName(int index);
QIcon colourIcon(int index);

// Event IDs of ISO 11783-6 Table A.3 that can trigger macros.
std::vector<NamedValue> macroEvents();
QString eventName(int event);

} // namespace ui
