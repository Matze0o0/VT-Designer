#pragma once

#include "vtcore/ObjectPool.h"

#include <cstdint>
#include <span>
#include <string>

namespace vt {

enum class HeaderStyle {
    Define,    // #define NAME 1000u          (C and C++)
    Enum,      // enum { NAME = 1000 };       (C and C++)
    Constexpr, // inline constexpr std::uint16_t NAME = 1000;  (C++17)
};

struct ExportOptions {
    std::string poolName = "pool";   // base name of generated files and symbols
    std::string symbolPrefix;        // prepended to every object ID symbol
    HeaderStyle headerStyle = HeaderStyle::Define;
};

// Symbol used for an object in generated code: the object name if it is a valid C identifier,
// otherwise "<Type>_<ID>" (e.g. "DataMask_1000"), always with the symbol prefix.
std::string objectSymbol(const VtObject& object, const ExportOptions& options);

// Header with one constant per object ID, sorted by ID.
std::string exportIdHeader(const ObjectPool& pool, const ExportOptions& options);

// Header and source that embed the binary pool as a byte array (<poolName>_data, <poolName>_size).
std::string exportPoolArrayHeader(const ExportOptions& options);
std::string exportPoolArraySource(std::span<const std::uint8_t> iop, const ExportOptions& options);

// File names the exporters refer to, e.g. "pool_ids.h".
std::string idHeaderFileName(const ExportOptions& options);
std::string arrayHeaderFileName(const ExportOptions& options);
std::string arraySourceFileName(const ExportOptions& options);
std::string iopFileName(const ExportOptions& options);

} // namespace vt
