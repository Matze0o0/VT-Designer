#pragma once

#include "vtcore/ObjectPool.h"

#include <string>
#include <vector>

namespace vt::test {

inline std::vector<std::uint8_t> latin1(const std::string& text)
{
    return {text.begin(), text.end()};
}

// Smallest pool that passes validation without errors: Working Set with a text designator and an
// active Data Mask that shows the same text.
inline ObjectPool minimalPool(VtVersion version = VtVersion::V4)
{
    ObjectPool pool(version);

    VtObject& font = pool.add(VtObject::create(100, ObjectType::FontAttributes, "fontNormal"));
    font.set("fontSize", 1);

    VtObject& text = pool.add(VtObject::create(200, ObjectType::OutputString, "txtTitle"));
    text.set("fontAttributes", 100);
    text.data = latin1("Sprayer");

    VtObject& mask = pool.add(VtObject::create(1000, ObjectType::DataMask, "dmMain"));
    mask.children.push_back({200, 10, 20});

    VtObject& ws = pool.add(VtObject::create(0, ObjectType::WorkingSet, "ws"));
    ws.set("activeMask", 1000);
    ws.children.push_back({200, 0, 0});
    ws.languages = {"de", "en"};

    return pool;
}

} // namespace vt::test
