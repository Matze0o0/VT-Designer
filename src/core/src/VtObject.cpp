#include "vtcore/VtObject.h"

#include <stdexcept>
#include <string>

namespace vt {

namespace {

int requireAttribute(const VtObject& object, std::string_view attribute)
{
    const int index = object.schema().attributeIndex(attribute);
    if (index < 0) {
        throw std::out_of_range(std::string(objectTypeName(object.type)) + " has no attribute '" +
                                std::string(attribute) + "'");
    }
    return index;
}

} // namespace

VtObject VtObject::create(ObjectId id, ObjectType type, std::string name)
{
    VtObject object;
    object.id = id;
    object.type = type;
    object.name = std::move(name);
    for (const AttributeDef& attribute : schemaFor(type).attributes) {
        object.attributes.push_back(attribute.defaultValue);
    }
    return object;
}

bool VtObject::hasAttribute(std::string_view attribute) const
{
    return schema().attributeIndex(attribute) >= 0;
}

double VtObject::get(std::string_view attribute) const
{
    return attributes.at(static_cast<std::size_t>(requireAttribute(*this, attribute)));
}

void VtObject::set(std::string_view attribute, double value)
{
    attributes.at(static_cast<std::size_t>(requireAttribute(*this, attribute))) = value;
}

std::size_t VtObject::listSize(ListKind kind) const
{
    switch (kind) {
    case ListKind::Children: return children.size();
    case ListKind::References: return references.size();
    case ListKind::Macros: return macros.size();
    case ListKind::Points: return points.size();
    case ListKind::Data: return data.size();
    case ListKind::Languages: return languages.size();
    case ListKind::Labels: return labels.size();
    case ListKind::CodePlanes: return codePlanes.size();
    }
    return 0;
}

} // namespace vt
