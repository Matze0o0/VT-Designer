#pragma once

#include "vtcore/Exporters.h"
#include "vtcore/ObjectPool.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

// A designer project: the object pool plus everything that is not part of the binary pool.
struct Project {
    ObjectPool pool;
    ExportOptions exportOptions;
};

class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Project files (.vtp) are UTF-8 JSON, so they diff well in version control.
// Text values of strings are stored readable: 8-bit strings byte-for-byte as ISO 8859-1 code points,
// WideStrings (UTF-16 with byte order mark) under "wideText". Other binary data is base64 encoded.
std::string projectToJson(const Project& project);
Project projectFromJson(std::string_view json); // throws ProjectError

void saveProject(const Project& project, const std::filesystem::path& path);
Project loadProject(const std::filesystem::path& path);

// Helpers shared with the CLI and the editor.
std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);
void writeBinaryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
void writeTextFile(const std::filesystem::path& path, std::string_view text);

} // namespace vt
